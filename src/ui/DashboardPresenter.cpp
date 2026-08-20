#include "DashboardPresenter.h"

#include <cstdio>

namespace nova {
namespace {

ConnectionLayerContent layer(const char* name, const char* status,
                             const char* glyph) {
  return {name, status, glyph};
}

}  // namespace

DashboardContent DashboardPresenter::present(const DeviceView& view) {
  DashboardContent result{"Starting", "+", "Preparing the server sentinel",
                          UiTone::Neutral, false, view.lastKnown,
                          {layer("Wi-Fi", "Not evaluated", "-"),
                           layer("Server", "Not evaluated", "-"),
                           layer("Monitor", "Not evaluated", "-")}};
  switch (view.state) {
    case DeviceState::Starting: break;
    case DeviceState::SetupRequired:
      result = {"Setup required", "+", "Install Wi-Fi and server configuration, then restart", UiTone::Neutral, true, false,
                {layer("Wi-Fi", "Not configured", "-"), layer("Server", "Not configured", "-"), layer("Monitor", "Not configured", "-")}};
      break;
    case DeviceState::WifiConnecting:
      result = {"Wi-Fi connecting", "-", "Connecting to the local network", UiTone::Neutral, true, view.lastKnown,
                {layer("Wi-Fi", "Connecting", "-"), layer("Server", "Not evaluated", "-"), layer("Monitor", "Not evaluated", "-")}};
      break;
    case DeviceState::WifiOffline:
      result = {"Wi-Fi offline", "X", "NOVA cannot reach the local network", UiTone::Critical, true, view.lastKnown,
                {layer("Wi-Fi", "Disconnected", "X"), layer("Server", "Not evaluated", "-"), layer("Monitor", "Not evaluated", "-")}};
      break;
    case DeviceState::ServerConnecting:
      result = {"Server connecting", "-", "Waiting for the first server snapshot", UiTone::Neutral, true, false,
                {layer("Wi-Fi", "Connected", "OK"), layer("Server", "Connecting", "-"), layer("Monitor", "Not evaluated", "-")}};
      break;
    case DeviceState::Healthy:
      result = {"Healthy", "OK", "All monitored systems normal", UiTone::Healthy, false, false, {}};
      break;
    case DeviceState::Warning:
      result = {"Warning", "!", "Server needs attention", UiTone::Warning, false, false, {}};
      break;
    case DeviceState::Critical:
      result = {"Critical", "X", "Server needs immediate attention", UiTone::Critical, false, false, {}};
      break;
    case DeviceState::Stale:
      result = {"Data stale", "", "Last update is taking longer than expected", UiTone::Stale, true, true,
                {layer("Wi-Fi", "Connected", "OK"), layer("Server", "Unknown", "-"), layer("Monitor", "Not evaluated", "-")}};
      break;
    case DeviceState::ServerOffline:
      result = {"Server offline", "X", "No server response for 30 seconds", UiTone::Critical, true, view.lastKnown,
                {layer("Wi-Fi", "Connected", "OK"), layer("Server", "No response", "X"), layer("Monitor", "Not evaluated", "-")}};
      break;
    case DeviceState::MonitorError:
      result = {"Monitor error", "!", monitorError(view.monitorError), UiTone::Error, true, view.lastKnown,
                {layer("Wi-Fi", "Connected", "OK"), layer("Server", "Unknown", "-"), layer("Monitor", "Not evaluated", "-")}};
      break;
  }
  return result;
}

void DashboardPresenter::formatMetric(Nullable<uint16_t> metric, char* output,
                                      size_t capacity) {
  if (!metric.available) {
    std::snprintf(output, capacity, "Unavailable");
    return;
  }
  std::snprintf(output, capacity, "%u.%u%%", metric.value / 10,
                metric.value % 10);
}

const char* DashboardPresenter::serviceGlyph(ServiceState state) {
  switch (state) {
    case ServiceState::Healthy: return "OK";
    case ServiceState::Warning: return "!";
    case ServiceState::Critical: return "X";
    case ServiceState::Unknown: return "-";
  }
  return "-";
}

const char* DashboardPresenter::serviceName(ServiceState state) {
  switch (state) {
    case ServiceState::Healthy: return "HEALTHY";
    case ServiceState::Warning: return "WARNING";
    case ServiceState::Critical: return "CRITICAL";
    case ServiceState::Unknown: return "UNKNOWN";
  }
  return "UNKNOWN";
}

const char* DashboardPresenter::monitorError(PollError error) {
  switch (error) {
    case PollError::Authentication: return "Device credentials were rejected";
    case PollError::UnsupportedSchema: return "Server schema is not supported";
    case PollError::TlsValidation: return "Secure server identity could not be validated";
    case PollError::OversizedBody: return "Server response exceeded the safe limit";
    case PollError::InvalidContract: return "Server response was invalid";
    default: return "Monitoring contract needs attention";
  }
}

}  // namespace nova
