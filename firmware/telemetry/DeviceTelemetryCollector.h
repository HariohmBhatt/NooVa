#pragma once

#include <cstddef>
#include <cstdint>

#include "../hardware/BoardTouch.h"
#include "../network/WifiService.h"

namespace nova {

/** One bounded device telemetry sample ready for protocol serialization. */
struct DeviceTelemetrySnapshot {
  static constexpr size_t kAudioStateLength = 12;
  static constexpr int32_t kUnavailableRssiDbm = -127;
  static constexpr char kUnavailableAudioState[] = "unavailable";

  uint32_t sequence = 0;
  uint32_t uptimeSeconds = 0;
  int32_t wifiRssiDbm = kUnavailableRssiDbm;
  uint32_t freeHeapBytes = 0;
  bool touchReady = false;
  bool touchActive = false;
  char audioState[kAudioStateLength] = "unavailable";
};

class DeviceTelemetryCollector {
 public:
  /** Publish one device sample every fifteen seconds after session approval. */
  static constexpr uint32_t kCadenceMs = 15000;

  /** Construct a sampler over the shared Wi-Fi and board touch services. */
  DeviceTelemetryCollector(WifiService& wifi, BoardTouch& touch);

  /** Fill a fixed-size snapshot and advance its in-memory sequence. */
  void collect(DeviceTelemetrySnapshot& snapshot);

 private:
  WifiService& wifi_;
  BoardTouch& touch_;
  uint32_t sequence_ = 0;
};

}  // namespace nova
