#pragma once

#include <cstdint>

#include "../core/Logger.h"
#include "../storage/TelemetryStorage.h"
#include "TelemetryItem.h"

namespace nova {

/** Latest board-local health values used by the UI and journal. */
struct EspHealthSnapshot {
  bool ready = false;
  bool storageMounted = false;
  int16_t cpuPercent = -1;
  uint64_t storageAvailableBytes = 0;
  uint64_t storageQuotaBytes = 0;
  uint32_t uptimeSeconds = 0;
};

/** Samples ESP32 health behind a small stable interface. */
class EspHealthService {
 public:
  /** Construct a local health sampler backed by the telemetry journal. */
  EspHealthService(Logger& logger, TelemetryStorage& storage);

  /** Register IRAM-safe FreeRTOS hooks and prime the first sample. */
  bool begin();

  /** Refresh CPU and storage values without blocking on the SD card. */
  void update();

  /** Return the most recent local health values. */
  const EspHealthSnapshot& snapshot() const;

  /** Create a fixed-width local-health journal item. */
  TelemetryItem makeItem(uint32_t sequence, uint64_t timestampSeconds) const;

 private:
  static constexpr uint8_t kCpuCoreCount = 2;
  static constexpr uint32_t kSamplePeriodMs = 500;

  bool registerCpuHooks();
  void sampleCpu();

  Logger& logger_;
  TelemetryStorage& storage_;
  EspHealthSnapshot snapshot_ = {};
  uint32_t lastSampleAt_ = 0;
  uint32_t lastIdleTicks_[kCpuCoreCount] = {};
  uint32_t lastTotalTicks_[kCpuCoreCount] = {};
  int16_t localCpuPercent_ = -1;
  bool hooksReady_ = false;
  bool sampleReady_ = false;
};

}  // namespace nova
