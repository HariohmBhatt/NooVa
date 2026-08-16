#include "TelemetryRecorder.h"

#include <Arduino.h>

namespace nova {

TelemetryRecorder::TelemetryRecorder(Logger& logger, TelemetryStorage& storage,
                                     ServerTelemetryService& server,
                                     EspHealthService& esp)
    : logger_(logger), storage_(storage), server_(server), esp_(esp) {}

bool TelemetryRecorder::begin() {
  lastRecordAt_ = millis();
  return true;
}

void TelemetryRecorder::update() {
  const uint32_t now = millis();
  const ServerTelemetrySnapshot& server = server_.snapshot();
  const bool newServerItem =
      server.valid &&
      (!serverRecorded_ || server.sequence != lastServerSequence_ ||
       server.timestampSeconds != lastServerTimestamp_);
  const bool retryDue = !serverAttempted_ ||
                        now - lastServerAttemptAt_ >= kRecordPeriodMs;
  if (newServerItem && retryDue) {
    lastServerAttemptAt_ = now;
    serverAttempted_ = true;
    if (storage_.append(server_.latestItem())) {
      serverRecorded_ = true;
      lastServerSequence_ = server.sequence;
      lastServerTimestamp_ = server.timestampSeconds;
    } else {
      logger_.write(LogLevel::Warning, "Server telemetry item not stored");
    }
  }

  if (now - lastRecordAt_ < kRecordPeriodMs) {
    return;
  }
  lastRecordAt_ = now;
  const TelemetryItem local = esp_.makeItem(
      localSequence_++, server.timestampSeconds == 0
          ? static_cast<uint64_t>(now / 1000)
          : server.timestampSeconds);
  if (!storage_.append(local)) {
    logger_.write(LogLevel::Warning, "ESP health item not stored");
  }
}

}  // namespace nova
