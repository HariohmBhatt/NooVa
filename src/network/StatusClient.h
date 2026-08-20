#pragma once

#include <cstddef>
#include <cstdint>

#include "status/StatusSnapshot.h"

namespace nova {

constexpr size_t kMaxHttpsRequestBytes = 512;
constexpr size_t kMaxHttpsResponseChunkBytes = 256;
constexpr size_t kMaxRawHttpResponseBytes = 5376;

struct StatusClientConfig {
  const char* tlsServerName = nullptr;
  const char* path = nullptr;
  const char* bearerToken = nullptr;
};

struct HttpsRequest {
  char bytes[kMaxHttpsRequestBytes]{};
  uint16_t length = 0;
};

enum class HttpsTransportEventType : uint8_t {
  ResponseBytes,
  ResponseComplete,
  Timeout,
  ConnectFailure,
  TlsValidationFailure,
  ResponseTooLarge,
};

struct HttpsTransportEvent {
  HttpsTransportEventType type = HttpsTransportEventType::ConnectFailure;
  const uint8_t* bytes = nullptr;
  size_t length = 0;
};

/** System boundary implemented by the ESP32 HTTPS worker and host test fakes. */
class StatusTransport {
 public:
  virtual ~StatusTransport() = default;
  virtual bool submit(const HttpsRequest& request) = 0;
  virtual bool take(HttpsTransportEvent& event) = 0;
  virtual void cancel() = 0;
};

/**
 * Portable status polling state machine.
 *
 * This deep module owns request generation, scheduling, HTTP framing, response
 * classification, and schema decoding. The injected transport only moves raw
 * HTTPS bytes and may perform its blocking work on another execution context.
 */
class StatusClient {
 public:
  bool begin(const StatusClientConfig& config);
  bool configured() const;

  /** Perform only fixed-memory, non-blocking work on the caller's thread. */
  bool update(uint32_t nowMs, bool wifiConnected, StatusTransport& transport,
              PollOutcome& outcome);

 private:
  static constexpr uint32_t kPollIntervalMs = 5000;
  static constexpr uint32_t kNoRetryAfter = UINT32_MAX;
  static constexpr size_t kMaxResponseHeaderBytes = 1024;
  static constexpr size_t kMaxHeaderLineBytes = 191;

  bool buildRequest(HttpsRequest& request) const;
  PollOutcome processResponse(uint32_t& retryAfterMs) const;
  bool complete(PollOutcome result, uint32_t retryAfterMs, uint32_t nowMs,
                PollOutcome& outcome);

  char tlsServerName_[96]{};
  char path_[65]{};
  char token_[129]{};
  uint8_t responseBytes_[kMaxRawHttpResponseBytes]{};
  size_t responseLength_ = 0;
  uint32_t completedAtMs_ = 0;
  uint32_t waitDurationMs_ = 0;
  bool configured_ = false;
  bool inFlight_ = false;
  bool waiting_ = false;
  bool wifiWasConnected_ = false;
};

}  // namespace nova
