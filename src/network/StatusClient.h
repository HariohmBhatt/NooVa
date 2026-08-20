#pragma once

#include <WiFiClientSecure.h>

#include <cstddef>
#include <cstdint>

#include "StatusCodec.h"

namespace nova {

struct StatusClientConfig {
  const char* address = nullptr;
  const char* tlsServerName = nullptr;
  uint16_t port = 0;
  const char* path = nullptr;
  const char* bearerToken = nullptr;
  const char* caCertificatePem = nullptr;
};

/**
 * Bounded HTTPS polling adapter for StatusDecoder.
 *
 * Header/body reads are incremental and capped per update. TLS connect itself
 * is bounded by the secure client's timeout because Arduino's TLS stack does
 * not expose a portable incremental handshake API.
 */
class StatusClient {
 public:
  bool begin(const StatusClientConfig& config);
  bool configured() const;

  /** Advance at most one bounded unit of work; return true when outcome is set. */
  bool update(uint32_t nowMs, bool wifiConnected, PollOutcome& outcome);
  void cancel();

 private:
  enum class State : uint8_t { Idle, StatusLine, Headers, Body };

  static constexpr uint32_t kPollIntervalMs = 5000;
  static constexpr uint32_t kReadTimeoutMs = 3000;
  static constexpr uint64_t kMinimumTlsEpochSeconds = 1704067200ULL;
  static constexpr size_t kMaxBytesPerUpdate = 256;
  static constexpr size_t kMaxHeaderBytes = 1024;
  static constexpr size_t kMaxLineBytes = 191;

  bool startRequest(uint32_t nowMs, PollOutcome& outcome);
  bool consumeLineByte(char byte, uint32_t nowMs, PollOutcome& outcome);
  bool processCompleteLine(uint32_t nowMs, PollOutcome& outcome);
  bool complete(PollOutcome result, uint32_t nowMs, PollOutcome& outcome);
  bool fail(PollError error, bool transient, uint32_t nowMs,
            PollOutcome& outcome);
  PollError connectFailure();
  void resetResponse();

  WiFiClientSecure tls_;
  StatusDecoder decoder_;
  HttpResponseMetadata metadata_{};
  char address_[96]{};
  char tlsServerName_[96]{};
  char path_[65]{};
  char token_[129]{};
  const char* caCertificatePem_ = nullptr;
  uint16_t port_ = 0;
  char line_[kMaxLineBytes + 1]{};
  size_t lineLength_ = 0;
  size_t headerBytes_ = 0;
  size_t bodyBytes_ = 0;
  uint32_t phaseActivityAtMs_ = 0;
  uint32_t completedAtMs_ = 0;
  uint32_t waitDurationMs_ = 0;
  uint32_t retryAfterMs_ = 0;
  State state_ = State::Idle;
  bool configured_ = false;
  bool waiting_ = false;
  bool timeSyncRequested_ = false;
  bool contentLengthSeen_ = false;
  bool contentTypeSeen_ = false;
};

}  // namespace nova
