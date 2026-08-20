#pragma once

#include <cstddef>
#include <cstdint>

#include "status/StatusSnapshot.h"

namespace nova {

constexpr size_t kMaxStatusBodyBytes = 4096;

struct HttpResponseMetadata {
  int statusCode = 0;
  int32_t contentLength = -1;
  char contentType[40]{};
};

/**
 * Incremental, fixed-capacity decoder for one schema-v1 HTTP response.
 *
 * Transport code may append arbitrarily small chunks. No snapshot becomes
 * visible until finish() validates the complete body and all cross-field
 * invariants.
 */
class StatusDecoder {
 public:
  StatusDecoder() = default;
  explicit StatusDecoder(const HttpResponseMetadata& metadata);

  void reset(const HttpResponseMetadata& metadata);
  bool append(const uint8_t* data, size_t length);
  PollOutcome finish();

 private:
  PollOutcome classifyHttpError() const;
  bool decodeSnapshot(StatusSnapshot& snapshot);

  HttpResponseMetadata metadata_{};
  char body_[kMaxStatusBodyBytes + 1]{};
  size_t received_ = 0;
  PollError framingError_ = PollError::None;
};

}  // namespace nova
