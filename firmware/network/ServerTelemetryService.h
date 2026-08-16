#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <cstddef>
#include <cstdint>

#include "../core/Logger.h"
#include "../telemetry/TelemetryItem.h"
#include "WifiService.h"

namespace nova {

/** State of the persistent server telemetry stream. */
enum class ServerTelemetryState : uint8_t {
  Unconfigured,
  Offline,
  Live,
  Degraded,
  Stale,
  Error,
};

/** Latest host and GPU values decoded from the server stream. */
struct ServerTelemetrySnapshot {
  bool valid = false;
  bool gpuAvailable = false;
  float cpuPercent = -1.0F;
  float gpuUtilizationPercent = -1.0F;
  float gpuTemperatureC = -1.0F;
  uint64_t gpuVramUsedBytes = 0;
  uint64_t gpuVramTotalBytes = 0;
  uint32_t uptimeSeconds = 0;
  uint32_t errorCount = 0;
  uint32_t sequence = 0;
  uint64_t timestampSeconds = 0;
  uint32_t receivedAtMs = 0;
  char gpuName[64] = {};
  char dependencyStatus[12] = {};
  char error[64] = {};
};

/** Stream compact host telemetry and expose its latest decoded item. */
class ServerTelemetryService {
 public:
  /** Construct a server telemetry client sharing the board Wi-Fi service. */
  ServerTelemetryService(Logger& logger, WifiService& wifi);

  /** Prepare the secure client and verify that a trust anchor is available. */
  bool begin();

  /** Pump a bounded amount of stream data without blocking the main loop. */
  void update();

  /** Return the most recently received host and GPU metrics. */
  const ServerTelemetrySnapshot& snapshot() const;

  /** Return the complete item that produced the current snapshot. */
  const TelemetryItem& latestItem() const;

  /** Return the current stream availability state. */
  ServerTelemetryState state() const;

  /** Return a concise state name suitable for diagnostics. */
  const char* stateName() const;

 private:
  static constexpr uint32_t kReconnectPeriodMs = 5000;
  static constexpr uint32_t kStaleAfterMs = 15000;
  static constexpr uint32_t kRequestTimeoutMs = 2500;
  static constexpr size_t kMaxBytesPerUpdate = 256;

  bool openStream();
  void closeStream();
  void pumpStream(uint32_t now);
  void processFrame(const TelemetryItem& item, uint32_t now);
  void discardLeadingByte();
  void setError(const char* message);

  Logger& logger_;
  WifiService& wifi_;
  WiFiClientSecure tls_;
  HTTPClient http_;
  ServerTelemetrySnapshot snapshot_ = {};
  TelemetryItem latestItem_ = {};
  ServerTelemetryState state_ = ServerTelemetryState::Unconfigured;
  uint8_t frameBuffer_[kTelemetryFrameSize] = {};
  size_t frameBytes_ = 0;
  uint32_t lastConnectAttemptAt_ = 0;
  uint32_t lastFrameAt_ = 0;
  bool streamOpen_ = false;
  bool ready_ = false;
};

}  // namespace nova
