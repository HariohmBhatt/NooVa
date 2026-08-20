#include "SentinelModel.h"

namespace nova {

void SentinelModel::setConfigured(bool configured, uint32_t nowMs) {
  startup_ = false;
  configured_ = configured;
  if (configured_) {
    wifiState_ = WifiConnectionState::Connecting;
    connectedAtMs_ = nowMs;
  }
}

void SentinelModel::setWifiState(WifiConnectionState state, uint32_t nowMs) {
  startup_ = false;
  if (state == WifiConnectionState::Connected &&
      wifiState_ != WifiConnectionState::Connected) {
    connectedAtMs_ = nowMs;
  }
  wifiState_ = state;
}

void SentinelModel::apply(const PollOutcome& outcome, uint32_t nowMs) {
  if (outcome.disposition == PollDisposition::Accepted) {
    lastSnapshot_ = outcome.snapshot;
    acceptedAtMs_ = nowMs;
    hasSnapshot_ = true;
    hasMonitorError_ = false;
    monitorError_ = PollError::None;
  } else if (outcome.disposition == PollDisposition::MonitorError) {
    hasMonitorError_ = true;
    monitorError_ = outcome.error;
  }
  // Transient errors intentionally leave all state untouched. Freshness alone
  // moves a retained snapshot through stale and server-offline.
}

DeviceView SentinelModel::view(uint32_t nowMs) const {
  DeviceView result{};
  result.hasSnapshot = hasSnapshot_;
  result.snapshot = lastSnapshot_;
  result.monitorError = monitorError_;
  result.snapshotAgeMs = hasSnapshot_ ? nowMs - acceptedAtMs_ : 0;

  if (startup_) {
    result.state = DeviceState::Starting;
  } else if (!configured_) {
    result.state = DeviceState::SetupRequired;
  } else if (wifiState_ == WifiConnectionState::Connecting) {
    result.state = DeviceState::WifiConnecting;
  } else if (wifiState_ == WifiConnectionState::Offline) {
    result.state = DeviceState::WifiOffline;
  } else if (hasMonitorError_) {
    result.state = DeviceState::MonitorError;
  } else if (!hasSnapshot_) {
    result.state = nowMs - connectedAtMs_ >= kFirstSnapshotTimeoutMs
                       ? DeviceState::ServerOffline
                       : DeviceState::ServerConnecting;
  } else if (result.snapshotAgeMs >= kOfflineAfterMs) {
    result.state = DeviceState::ServerOffline;
  } else if (result.snapshotAgeMs >= kStaleAfterMs) {
    result.state = DeviceState::Stale;
  } else {
    result.state = reportedState(lastSnapshot_.overall);
  }

  result.lastKnown = result.hasSnapshot &&
                     result.state != DeviceState::Healthy &&
                     result.state != DeviceState::Warning &&
                     result.state != DeviceState::Critical;
  return result;
}

DeviceState SentinelModel::reportedState(ReportedSeverity severity) {
  switch (severity) {
    case ReportedSeverity::Healthy:
      return DeviceState::Healthy;
    case ReportedSeverity::Warning:
      return DeviceState::Warning;
    case ReportedSeverity::Critical:
      return DeviceState::Critical;
  }
  return DeviceState::MonitorError;
}

}  // namespace nova
