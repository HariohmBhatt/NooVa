#pragma once

#include <cstdint>

#include "EspHealthService.h"
#include "../core/Logger.h"
#include "../network/ServerTelemetryService.h"
#include "../storage/TelemetryStorage.h"

namespace nova {

/** Persists server stream items and local ESP health at a bounded cadence. */
class TelemetryRecorder {
 public:
  /** Construct the coordinator at the storage/transport seam. */
  TelemetryRecorder(Logger& logger, TelemetryStorage& storage,
                    ServerTelemetryService& server, EspHealthService& esp);

  /** Prime recorder state without writing an uninitialized item. */
  bool begin();

  /** Store newly received server data and periodic local health data. */
  void update();

 private:
  static constexpr uint32_t kRecordPeriodMs = 5000;

  Logger& logger_;
  TelemetryStorage& storage_;
  ServerTelemetryService& server_;
  EspHealthService& esp_;
  uint32_t lastRecordAt_ = 0;
  uint32_t lastServerAttemptAt_ = 0;
  uint32_t localSequence_ = 0;
  uint32_t lastServerSequence_ = 0;
  uint64_t lastServerTimestamp_ = 0;
  bool serverAttempted_ = false;
  bool serverRecorded_ = false;
};

}  // namespace nova
