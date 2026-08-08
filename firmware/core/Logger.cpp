#include "Logger.h"

#include <cstdarg>
#include <cstdio>

namespace nova {
namespace {

const char* levelName(LogLevel level) {
  switch (level) {
    case LogLevel::Debug:
      return "DEBUG";
    case LogLevel::Info:
      return "INFO";
    case LogLevel::Warning:
      return "WARN";
    case LogLevel::Error:
      return "ERROR";
  }
  return "UNKNOWN";
}

}  // namespace

void Logger::write(LogLevel level, const char* message) {
  if (message == nullptr) {
    return;
  }

  portENTER_CRITICAL(&mutex_);
  LogEntry& entry = entries_[nextIndex_];
  entry.timestampMs = millis();
  entry.level = level;
  snprintf(entry.message, kMessageLength, "%s", message);
  nextIndex_ = (nextIndex_ + 1) % kCapacity;
  if (entryCount_ < kCapacity) {
    ++entryCount_;
  }
  portEXIT_CRITICAL(&mutex_);

  Serial.printf("[%lu][%s] %s\n", static_cast<unsigned long>(entry.timestampMs),
                levelName(level), message);
}

void Logger::writef(LogLevel level, const char* format, ...) {
  if (format == nullptr) {
    return;
  }

  char message[kMessageLength] = {};
  va_list arguments;
  va_start(arguments, format);
  vsnprintf(message, sizeof(message), format, arguments);
  va_end(arguments);
  write(level, message);
}

size_t Logger::copy(LogEntry* entries, size_t capacity) const {
  if (entries == nullptr || capacity == 0) {
    return 0;
  }

  portENTER_CRITICAL(&mutex_);
  const size_t copied = entryCount_ < capacity ? entryCount_ : capacity;
  const size_t first = (nextIndex_ + kCapacity - entryCount_) % kCapacity;
  for (size_t index = 0; index < copied; ++index) {
    entries[index] = entries_[(first + index) % kCapacity];
  }
  portEXIT_CRITICAL(&mutex_);
  return copied;
}

size_t Logger::size() const {
  portENTER_CRITICAL(&mutex_);
  const size_t count = entryCount_;
  portEXIT_CRITICAL(&mutex_);
  return count;
}

}  // namespace nova
