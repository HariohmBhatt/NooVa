#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>

#include <cstddef>
#include <cstdint>

#include "StatusClient.h"

namespace nova {

struct HttpsPollTaskConfig {
  const char* address = nullptr;
  uint16_t port = 0;
  const char* tlsServerName = nullptr;
  const char* caCertificatePem = nullptr;
};

/**
 * ESP32 transport adapter that isolates blocking network calls on core 0.
 *
 * Both queues, the task stack, and the response slot are statically allocated.
 * WiFiClientSecure internally allocates its mbedTLS context because the Arduino
 * framework offers no static-context API; this adapter constructs it only once.
 */
class HttpsPollTask final : public StatusTransport {
 public:
  bool begin(const HttpsPollTaskConfig& config);

  bool submit(const HttpsRequest& request) override;
  bool take(HttpsTransportEvent& event) override;
  void cancel() override;

 private:
  struct WorkerCommand {
    HttpsRequest request{};
    uint32_t generation = 0;
  };

  struct WorkerResult {
    HttpsTransportEventType type = HttpsTransportEventType::ConnectFailure;
    uint32_t generation = 0;
    uint16_t length = 0;
  };

  static constexpr uint32_t kMinimumTlsEpochSeconds = 1704067200UL;
  static constexpr uint32_t kTransactionDeadlineMs = 4500;
  static constexpr uint32_t kTlsHandshakeTimeoutSeconds = 3;
  static constexpr uint32_t kSocketTimeoutSeconds = 3;
  static constexpr TickType_t kSocketWaitTicks = 1;
  static constexpr TickType_t kClockWaitTicks = pdMS_TO_TICKS(10);
  static constexpr uint32_t kTaskStackBytes = 12288;
  static constexpr UBaseType_t kTaskPriority = 1;
  static constexpr BaseType_t kTaskCore = 0;

  static void taskEntry(void* context);
  void run();
  WorkerResult perform(const WorkerCommand& command);
  HttpsTransportEventType connect();
  PollError connectError();
  uint32_t generation();

  WiFiClientSecure tls_;
  QueueHandle_t commandQueue_ = nullptr;
  QueueHandle_t resultQueue_ = nullptr;
  TaskHandle_t taskHandle_ = nullptr;
  StaticQueue_t commandQueueControl_{};
  StaticQueue_t resultQueueControl_{};
  StaticTask_t taskControl_{};
  uint8_t commandQueueStorage_[sizeof(WorkerCommand)]{};
  uint8_t resultQueueStorage_[sizeof(WorkerResult)]{};
  static_assert(kTaskStackBytes % sizeof(StackType_t) == 0,
                "HTTPS task stack must contain whole StackType_t entries");
  StackType_t taskStack_[kTaskStackBytes / sizeof(StackType_t)]{};
  uint8_t responseBytes_[kMaxRawHttpResponseBytes]{};
  char address_[96]{};
  char tlsServerName_[96]{};
  const char* caCertificatePem_ = nullptr;
  uint16_t port_ = 0;
  portMUX_TYPE generationMux_ = portMUX_INITIALIZER_UNLOCKED;
  uint32_t generation_ = 1;
  size_t deliveryOffset_ = 0;
  size_t deliveryLength_ = 0;
  bool timeSyncRequested_ = false;
  bool deliveryActive_ = false;
  bool ready_ = false;
};

}  // namespace nova
