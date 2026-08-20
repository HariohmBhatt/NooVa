#pragma once

#include <cstdint>

#include "StatusSnapshot.h"

namespace nova {

enum class WifiConnectionState : uint8_t { Connecting, Connected, Offline };

enum class DeviceState : uint8_t {
  Starting,
  SetupRequired,
  WifiConnecting,
  WifiOffline,
  ServerConnecting,
  Healthy,
  Warning,
  Critical,
  Stale,
  ServerOffline,
  MonitorError,
};

/** Immutable presentation input shared with the dedicated UI module. */
struct DeviceView {
  DeviceState state = DeviceState::Starting;
  StatusSnapshot snapshot{};
  PollError monitorError = PollError::None;
  uint32_t snapshotAgeMs = 0;
  bool hasSnapshot = false;
  bool lastKnown = false;
};

/**
 * Applies local connectivity and freshness precedence to server snapshots.
 *
 * The model deliberately has no Arduino, network, or display dependencies, so
 * every state transition is host-testable through this public interface.
 */
class SentinelModel {
 public:
  void setConfigured(bool configured, uint32_t nowMs);
  void setWifiState(WifiConnectionState state, uint32_t nowMs);
  void apply(const PollOutcome& outcome, uint32_t nowMs);
  DeviceView view(uint32_t nowMs) const;

 private:
  static constexpr uint32_t kFirstSnapshotTimeoutMs = 10000;
  static constexpr uint32_t kStaleAfterMs = 15000;
  static constexpr uint32_t kOfflineAfterMs = 30000;

  static DeviceState reportedState(ReportedSeverity severity);

  StatusSnapshot lastSnapshot_{};
  PollError monitorError_ = PollError::None;
  uint32_t connectedAtMs_ = 0;
  uint32_t acceptedAtMs_ = 0;
  WifiConnectionState wifiState_ = WifiConnectionState::Connecting;
  bool startup_ = true;
  bool configured_ = false;
  bool hasSnapshot_ = false;
  bool hasMonitorError_ = false;
};

}  // namespace nova
