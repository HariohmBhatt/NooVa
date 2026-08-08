#pragma once

#include <Arduino.h>

#include <cstddef>
#include <cstdint>

namespace nova {

enum class LogLevel : uint8_t {
  Debug,
  Info,
  Warning,
  Error,
};

struct LogEntry {
  uint32_t timestampMs;
  LogLevel level;
  char message[96];
};

class Logger {
 public:
  /** Store and mirror a diagnostic message without requiring a serial link. */
  void write(LogLevel level, const char* message);

  /** Format, store, and mirror a diagnostic message without heap allocation. */
  void writef(LogLevel level, const char* format, ...);

  /** Copy the oldest available entries into caller-owned storage. */
  size_t copy(LogEntry* entries, size_t capacity) const;

  /** Return the number of entries currently retained. */
  size_t size() const;

 private:
  static constexpr size_t kCapacity = 64;
  static constexpr size_t kMessageLength = 96;

  LogEntry entries_[kCapacity] = {};
  size_t nextIndex_ = 0;
  size_t entryCount_ = 0;
  mutable portMUX_TYPE mutex_ = portMUX_INITIALIZER_UNLOCKED;
};

}  // namespace nova
