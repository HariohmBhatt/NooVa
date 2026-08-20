#include "StatusClient.h"

#include <mbedtls/ssl.h>
#include <mbedtls/x509.h>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <time.h>

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

const char* skipSpaces(const char* value) {
  while (*value == ' ' || *value == '\t') {
    ++value;
  }
  return value;
}

bool parseNonnegativeInteger(const char* value, long maximum, long& parsed) {
  value = skipSpaces(value);
  errno = 0;
  char* end = nullptr;
  const long result = std::strtol(value, &end, 10);
  while (end != nullptr && (*end == ' ' || *end == '\t')) {
    ++end;
  }
  if (errno != 0 || end == value || end == nullptr || *end != '\0' ||
      result < 0 || result > maximum) {
    return false;
  }
  parsed = result;
  return true;
}

}  // namespace

bool StatusClient::begin(const StatusClientConfig& config) {
  configured_ =
      copyConfigString(config.address, address_, sizeof(address_)) &&
      copyConfigString(config.tlsServerName, tlsServerName_,
                       sizeof(tlsServerName_)) &&
      config.port != 0 &&
      copyConfigString(config.path, path_, sizeof(path_)) && path_[0] == '/' &&
      copyConfigString(config.bearerToken, token_, sizeof(token_)) &&
      config.caCertificatePem != nullptr && config.caCertificatePem[0] != '\0';
  if (!configured_) {
    return false;
  }

  metadata_.statusCode = 0;
  port_ = config.port;
  caCertificatePem_ = config.caCertificatePem;
  // Keep CA verification and SNI/hostname validation enabled for every request.
  tls_.setCACert(caCertificatePem_);
  tls_.setHandshakeTimeout(3);
  tls_.setTimeout(kReadTimeoutMs / 1000U);
  return true;
}

bool StatusClient::configured() const { return configured_; }

bool StatusClient::update(uint32_t nowMs, bool wifiConnected,
                          PollOutcome& outcome) {
  if (!configured_) {
    return false;
  }
  if (!wifiConnected) {
    cancel();
    return false;
  }

  // X.509 validity periods require a plausible wall clock. SNTP proceeds in
  // the ESP networking task; the main loop remains responsive while waiting.
  if (static_cast<uint64_t>(time(nullptr)) < kMinimumTlsEpochSeconds) {
    if (!timeSyncRequested_) {
      configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
      timeSyncRequested_ = true;
    }
    return false;
  }

  if (state_ == State::Idle) {
    if (waiting_ && nowMs - completedAtMs_ < waitDurationMs_) {
      return false;
    }
    waiting_ = false;
    if (startRequest(nowMs, outcome)) {
      return true;
    }
  }

  size_t budget = kMaxBytesPerUpdate;
  while (budget > 0 && tls_.available() > 0) {
    phaseActivityAtMs_ = nowMs;
    if (state_ == State::Body) {
      const size_t remaining = static_cast<size_t>(metadata_.contentLength) - bodyBytes_;
      uint8_t chunk[kMaxBytesPerUpdate];
      const size_t count = std::min(
          {budget, remaining, static_cast<size_t>(tls_.available())});
      const int read = tls_.read(chunk, count);
      if (read <= 0) {
        break;
      }
      if (!decoder_.append(chunk, static_cast<size_t>(read))) {
        return complete(decoder_.finish(), nowMs, outcome);
      }
      bodyBytes_ += static_cast<size_t>(read);
      budget -= static_cast<size_t>(read);
      if (bodyBytes_ == static_cast<size_t>(metadata_.contentLength)) {
        return complete(decoder_.finish(), nowMs, outcome);
      }
    } else {
      const int next = tls_.read();
      if (next < 0) {
        break;
      }
      --budget;
      if (consumeLineByte(static_cast<char>(next), nowMs, outcome)) {
        return true;
      }
    }
  }

  if (state_ != State::Idle && !tls_.connected() && tls_.available() == 0) {
    // A closed 200 response before the declared byte count is a broken success
    // contract, not a snapshot and never a freshness refresh.
    return fail(PollError::InvalidContract, false, nowMs, outcome);
  }
  if (state_ != State::Idle && nowMs - phaseActivityAtMs_ >= kReadTimeoutMs) {
    return fail(PollError::Timeout, true, nowMs, outcome);
  }
  return false;
}

void StatusClient::cancel() {
  tls_.stop();
  state_ = State::Idle;
  waiting_ = false;
  resetResponse();
}

bool StatusClient::startRequest(uint32_t nowMs, PollOutcome& outcome) {
  resetResponse();
  tls_.stop();
  tls_.setCACert(caCertificatePem_);
  IPAddress connectAddress;
  if (!connectAddress.fromString(address_) &&
      WiFi.hostByName(address_, connectAddress) != 1) {
    return fail(PollError::DnsOrConnect, true, nowMs, outcome);
  }
  if (!tls_.connect(connectAddress, port_, tlsServerName_, caCertificatePem_,
                    nullptr, nullptr)) {
    const PollError error = connectFailure();
    return fail(error, error != PollError::TlsValidation, nowMs, outcome);
  }

  tls_.print("GET ");
  tls_.print(path_);
  tls_.print(" HTTP/1.1\r\nHost: ");
  tls_.print(tlsServerName_);
  tls_.print("\r\nAccept: application/json\r\nAuthorization: Bearer ");
  tls_.print(token_);
  tls_.print("\r\nX-Nova-Schema: 1\r\nConnection: close\r\n\r\n");
  state_ = State::StatusLine;
  phaseActivityAtMs_ = nowMs;
  return false;
}

bool StatusClient::consumeLineByte(char byte, uint32_t nowMs,
                                   PollOutcome& outcome) {
  ++headerBytes_;
  if (headerBytes_ > kMaxHeaderBytes) {
    return fail(PollError::InvalidContract, false, nowMs, outcome);
  }
  if (byte == '\n') {
    if (lineLength_ > 0 && line_[lineLength_ - 1] == '\r') {
      --lineLength_;
    }
    line_[lineLength_] = '\0';
    return processCompleteLine(nowMs, outcome);
  }
  if (lineLength_ >= kMaxLineBytes) {
    return fail(PollError::InvalidContract, false, nowMs, outcome);
  }
  line_[lineLength_++] = byte;
  return false;
}

bool StatusClient::processCompleteLine(uint32_t nowMs, PollOutcome& outcome) {
  if (state_ == State::StatusLine) {
    char protocol[16]{};
    int status = 0;
    const bool valid = std::sscanf(line_, "%15s %d", protocol, &status) == 2 &&
                       std::strncmp(protocol, "HTTP/1.", 7) == 0 && status >= 100 &&
                       status <= 599;
    lineLength_ = 0;
    if (!valid) {
      return fail(PollError::InvalidContract, false, nowMs, outcome);
    }
    metadata_.statusCode = status;
    state_ = State::Headers;
    return false;
  }

  if (lineLength_ == 0) {
    decoder_.reset(metadata_);
    if (metadata_.statusCode != 200 || metadata_.contentLength <= 0 ||
        metadata_.contentLength > static_cast<int32_t>(kMaxStatusBodyBytes) ||
        !contentLengthSeen_ || !contentTypeSeen_ ||
        std::strcmp(metadata_.contentType, "application/json") != 0) {
      PollOutcome result = decoder_.finish();
      return complete(result, nowMs, outcome);
    }
    state_ = State::Body;
    return false;
  }

  constexpr char kLengthHeader[] = "Content-Length:";
  constexpr char kTypeHeader[] = "Content-Type:";
  constexpr char kTransferHeader[] = "Transfer-Encoding:";
  constexpr char kRetryHeader[] = "Retry-After:";
  if (strncasecmp(line_, kLengthHeader, sizeof(kLengthHeader) - 1) == 0) {
    if (contentLengthSeen_) {
      return fail(PollError::InvalidContract, false, nowMs, outcome);
    }
    long parsed = 0;
    if (!parseNonnegativeInteger(line_ + sizeof(kLengthHeader) - 1, INT32_MAX,
                                 parsed)) {
      return fail(PollError::InvalidContract, false, nowMs, outcome);
    }
    metadata_.contentLength = static_cast<int32_t>(parsed);
    contentLengthSeen_ = true;
  } else if (strncasecmp(line_, kTypeHeader, sizeof(kTypeHeader) - 1) == 0) {
    if (contentTypeSeen_) {
      return fail(PollError::InvalidContract, false, nowMs, outcome);
    }
    const char* value = skipSpaces(line_ + sizeof(kTypeHeader) - 1);
    std::snprintf(metadata_.contentType, sizeof(metadata_.contentType), "%s", value);
    contentTypeSeen_ = true;
  } else if (strncasecmp(line_, kTransferHeader,
                         sizeof(kTransferHeader) - 1) == 0) {
    return fail(PollError::InvalidContract, false, nowMs, outcome);
  } else if (strncasecmp(line_, kRetryHeader, sizeof(kRetryHeader) - 1) == 0) {
    long seconds = 0;
    if (parseNonnegativeInteger(line_ + sizeof(kRetryHeader) - 1, 60, seconds)) {
      retryAfterMs_ = static_cast<uint32_t>(seconds) * 1000U;
    }
  }
  lineLength_ = 0;
  return false;
}

bool StatusClient::complete(PollOutcome result, uint32_t nowMs,
                            PollOutcome& outcome) {
  tls_.stop();
  state_ = State::Idle;
  completedAtMs_ = nowMs;
  waitDurationMs_ = result.error == PollError::RateLimited && retryAfterMs_ > 0
                        ? retryAfterMs_
                        : kPollIntervalMs;
  waiting_ = true;
  outcome = result;
  return true;
}

bool StatusClient::fail(PollError error, bool transient, uint32_t nowMs,
                        PollOutcome& outcome) {
  return complete(transient ? PollOutcome::transientError(error)
                            : PollOutcome::monitorError(error),
                  nowMs, outcome);
}

PollError StatusClient::connectFailure() {
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

void StatusClient::resetResponse() {
  metadata_ = {};
  metadata_.contentLength = -1;
  lineLength_ = 0;
  headerBytes_ = 0;
  bodyBytes_ = 0;
  retryAfterMs_ = 0;
  contentLengthSeen_ = false;
  contentTypeSeen_ = false;
}

}  // namespace nova
