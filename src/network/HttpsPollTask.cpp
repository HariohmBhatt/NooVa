#include "HttpsPollTask.h"

#include <mbedtls/ssl.h>
#include <mbedtls/x509.h>
#include <time.h>

#include <algorithm>
#include <cstring>

namespace nova {
namespace {

bool copyConfigString(const char* source, char* destination, size_t capacity) {
  if (source == nullptr) {
    return false;
  }
  const size_t length = std::strlen(source);
  if (length == 0 || length >= capacity) {
    return false;
  }
  std::memcpy(destination, source, length + 1);
  return true;
}

}  // namespace

bool HttpsPollTask::begin(const HttpsPollTaskConfig& config) {
  if (ready_) {
    return true;
  }
  if (!copyConfigString(config.address, address_, sizeof(address_)) ||
      !copyConfigString(config.tlsServerName, tlsServerName_,
                        sizeof(tlsServerName_)) ||
      config.port == 0 || config.caCertificatePem == nullptr ||
      config.caCertificatePem[0] == '\0') {
    return false;
  }
  port_ = config.port;
  caCertificatePem_ = config.caCertificatePem;
  // Configure the reusable client before its first socket is opened. Repeating
  // setTimeout after stop() triggers a noisy descriptor bug in Arduino-ESP32
  // 2.x even though the timeout value itself persists across connections.
  tls_.setCACert(caCertificatePem_);
  tls_.setHandshakeTimeout(kTlsHandshakeTimeoutSeconds);
  tls_.setTimeout(kSocketTimeoutSeconds);

  commandQueue_ = xQueueCreateStatic(1, sizeof(WorkerCommand),
                                     commandQueueStorage_, &commandQueueControl_);
  resultQueue_ = xQueueCreateStatic(1, sizeof(WorkerResult), resultQueueStorage_,
                                    &resultQueueControl_);
  if (commandQueue_ == nullptr || resultQueue_ == nullptr) {
    return false;
  }

  // ESP-IDF measures this argument in bytes, unlike upstream FreeRTOS.
  taskHandle_ = xTaskCreateStaticPinnedToCore(
      taskEntry, "nova_https", kTaskStackBytes, this, kTaskPriority, taskStack_,
      &taskControl_, kTaskCore);
  ready_ = taskHandle_ != nullptr;
  return ready_;
}

bool HttpsPollTask::submit(const HttpsRequest& request) {
  if (!ready_ || request.length == 0 ||
      request.length >= sizeof(request.bytes)) {
    return false;
  }
  WorkerCommand command{};
  command.request = request;
  command.generation = generation();
  return xQueueSend(commandQueue_, &command, 0) == pdTRUE;
}

bool HttpsPollTask::take(HttpsTransportEvent& event) {
  if (!ready_) {
    return false;
  }
  if (!deliveryActive_) {
    WorkerResult result{};
    for (;;) {
      if (xQueueReceive(resultQueue_, &result, 0) != pdTRUE) {
        return false;
      }
      if (result.generation == generation()) {
        break;
      }
    }
    if (result.type != HttpsTransportEventType::ResponseComplete) {
      event.type = result.type;
      return true;
    }
    deliveryOffset_ = 0;
    deliveryLength_ = result.length;
    deliveryActive_ = true;
  }

  if (deliveryOffset_ < deliveryLength_) {
    const size_t remaining = deliveryLength_ - deliveryOffset_;
    event.type = HttpsTransportEventType::ResponseBytes;
    event.bytes = responseBytes_ + deliveryOffset_;
    event.length = std::min(remaining, kMaxHttpsResponseChunkBytes);
    deliveryOffset_ += event.length;
    return true;
  }
  event.type = HttpsTransportEventType::ResponseComplete;
  deliveryActive_ = false;
  return true;
}

void HttpsPollTask::cancel() {
  if (!ready_) {
    return;
  }
  portENTER_CRITICAL(&generationMux_);
  ++generation_;
  portEXIT_CRITICAL(&generationMux_);
  xQueueReset(commandQueue_);
  xQueueReset(resultQueue_);
  deliveryActive_ = false;
  deliveryOffset_ = 0;
  deliveryLength_ = 0;
  xTaskNotifyGive(taskHandle_);
}

uint32_t HttpsPollTask::stackHeadroomBytes() const {
  return ready_ ? static_cast<uint32_t>(uxTaskGetStackHighWaterMark(taskHandle_))
                : 0;
}

void HttpsPollTask::taskEntry(void* context) {
  static_cast<HttpsPollTask*>(context)->run();
}

void HttpsPollTask::run() {
  WorkerCommand command{};
  for (;;) {
    if (xQueueReceive(commandQueue_, &command, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    const WorkerResult result = perform(command);
    // A length-one overwrite queue publishes the newest completed transaction
    // without ever blocking this worker or the Arduino loop.
    xQueueOverwrite(resultQueue_, &result);
  }
}

HttpsPollTask::WorkerResult HttpsPollTask::perform(
    const WorkerCommand& command) {
  WorkerResult result{};
  result.generation = command.generation;
  const uint32_t startedAtMs = millis();

  // SNTP startup and the clock wait live on the worker so even an unavailable
  // time service cannot stall touch, rendering, or model freshness updates.
  if (static_cast<uint32_t>(time(nullptr)) < kMinimumTlsEpochSeconds &&
      !timeSyncRequested_) {
    configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
    timeSyncRequested_ = true;
  }
  while (static_cast<uint32_t>(time(nullptr)) < kMinimumTlsEpochSeconds) {
    if (millis() - startedAtMs >= kTransactionDeadlineMs) {
      result.type = HttpsTransportEventType::Timeout;
      return result;
    }
    ulTaskNotifyTake(pdTRUE, kClockWaitTicks);
  }

  result.type = connect();
  if (result.type != HttpsTransportEventType::ResponseComplete) {
    return result;
  }

  if (tls_.write(reinterpret_cast<const uint8_t*>(command.request.bytes),
                 command.request.length) != command.request.length) {
    result.type = HttpsTransportEventType::ConnectFailure;
    tls_.stop();
    return result;
  }

  size_t received = 0;
  for (;;) {
    const int available = tls_.available();
    if (available > 0) {
      const size_t capacity = sizeof(responseBytes_) - received;
      if (capacity == 0) {
        result.type = HttpsTransportEventType::ResponseTooLarge;
        break;
      }
      const size_t count =
          std::min(capacity, static_cast<size_t>(available));
      const int read = tls_.read(responseBytes_ + received, count);
      if (read > 0) {
        received += static_cast<size_t>(read);
      }
      continue;
    }
    if (!tls_.connected()) {
      result.type = HttpsTransportEventType::ResponseComplete;
      result.length = static_cast<uint16_t>(received);
      break;
    }
    if (millis() - startedAtMs >= kTransactionDeadlineMs) {
      result.type = HttpsTransportEventType::Timeout;
      break;
    }
    ulTaskNotifyTake(pdTRUE, kSocketWaitTicks);
  }
  tls_.stop();
  return result;
}

HttpsTransportEventType HttpsPollTask::connect() {
  IPAddress connectAddress;
  if (!connectAddress.fromString(address_) &&
      WiFi.hostByName(address_, connectAddress) != 1) {
    return HttpsTransportEventType::ConnectFailure;
  }

  if (tls_.connect(connectAddress, port_, tlsServerName_, caCertificatePem_,
                   nullptr, nullptr)) {
    return HttpsTransportEventType::ResponseComplete;
  }
  return connectError() == PollError::TlsValidation
             ? HttpsTransportEventType::TlsValidationFailure
             : HttpsTransportEventType::ConnectFailure;
}

PollError HttpsPollTask::connectError() {
  char ignoredMessage[80]{};
  const int error = tls_.lastError(ignoredMessage, sizeof(ignoredMessage));
  const bool x509Error = error <= MBEDTLS_ERR_X509_FEATURE_UNAVAILABLE &&
                         error >= MBEDTLS_ERR_X509_FATAL_ERROR;
  const bool tlsCertificateError =
      error == MBEDTLS_ERR_SSL_PEER_VERIFY_FAILED ||
      error == MBEDTLS_ERR_SSL_CA_CHAIN_REQUIRED ||
      error == MBEDTLS_ERR_SSL_CERTIFICATE_REQUIRED ||
      error == MBEDTLS_ERR_SSL_CERTIFICATE_TOO_LARGE ||
      error == MBEDTLS_ERR_SSL_BAD_HS_CERTIFICATE;
  return x509Error || tlsCertificateError ? PollError::TlsValidation
                                         : PollError::DnsOrConnect;
}

uint32_t HttpsPollTask::generation() {
  portENTER_CRITICAL(&generationMux_);
  const uint32_t value = generation_;
  portEXIT_CRITICAL(&generationMux_);
  return value;
}

}  // namespace nova
