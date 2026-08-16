#pragma once

#include <Arduino.h>

#include <cstdint>

#include "../core/Logger.h"
#include "WifiService.h"

namespace nova {

/** State of the last server telemetry request. */
enum class ServerTelemetryState : uint8_t {
  Unconfigured,
  Offline,
  Live,
  Degraded,
  Stale,
  Error,
};

/** Latest host and GPU values returned by the NOVA hub API. */
struct ServerTelemetrySnapshot {
  bool valid = false;
  bool gpuAvailable = false;
  float cpuPercent = -1.0F;
  float gpuUtilizationPercent = -1.0F;
  float gpuTemperatureC = -1.0F;
  uint64_t gpuVramUsedBytes = 0;
  uint64_t gpuVramTotalBytes = 0;
  uint32_t receivedAtMs = 0;
  char gpuName[64] = {};
  char dependencyStatus[12] = {};
  char error[64] = {};
};

/** Poll the hub's read-only telemetry API over the configured CA. */
class ServerTelemetryService {
 public:
  /** Construct a server telemetry client sharing the board Wi-Fi service. */
  ServerTelemetryService(Logger& logger, WifiService& wifi);

  /** Prepare the secure client and verify that a trust anchor is available. */
  bool begin();

  /** Poll the hub when due and update the latest snapshot. */
  void update();

  /** Return the most recently received host and GPU metrics. */
  const ServerTelemetrySnapshot& snapshot() const;

  /** Return the current request/availability state. */
  ServerTelemetryState state() const;

  /** Return a concise state name suitable for diagnostics. */
  const char* stateName() const;

 private:
  static constexpr uint32_t kPollPeriodMs = 5000;
  static constexpr uint32_t kStaleAfterMs = 15000;
  static constexpr uint32_t kRequestTimeoutMs = 2500;

  bool fetch(uint32_t now);
  bool parseResponse(Stream& body, uint32_t now);
  void setError(const char* message);
  static void copyText(char* destination, size_t capacity, const char* source);

  Logger& logger_;
  WifiService& wifi_;
  ServerTelemetrySnapshot snapshot_ = {};
  ServerTelemetryState state_ = ServerTelemetryState::Unconfigured;
  uint32_t lastPollAt_ = 0;
  bool ready_ = false;
};

}  // namespace nova
