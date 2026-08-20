#include "DashboardPresenter.h"

#include <cstdio>

namespace nova {
namespace {

ConnectionLayerContent layer(const char* name, const char* status,
                             const char* glyph, UiTone tone) {
  return {name, status, glyph, tone};
}

DashboardContent freshContent(const char* title, const char* glyph,
                              const char* fallbackSummary, UiTone tone) {
  return {title,
          glyph,
          fallbackSummary,
          tone,
          false,
          {layer("Wi-Fi", "Connected", "OK", UiTone::Healthy),
           layer("Server", "Reachable", "OK", UiTone::Healthy),
           layer("Monitor", "Validated", "OK", UiTone::Healthy)}};
}

}  // namespace

DashboardContent DashboardPresenter::present(const DeviceView& view) {
  DashboardContent result{"Starting", "+", "Preparing the server sentinel",
                          UiTone::Neutral, false,
                          {layer("Wi-Fi", "Not evaluated", "-",
                                 UiTone::Neutral),
                           layer("Server", "Not evaluated", "-",
                                 UiTone::Neutral),
                           layer("Monitor", "Not evaluated", "-",
                                 UiTone::Neutral)}};
  switch (view.state) {
    case DeviceState::Starting: break;
    case DeviceState::SetupRequired:
      result = {
          "Setup required", "+",
          "Install Wi-Fi and server configuration, then restart",
          UiTone::Neutral, true,
          {layer("Wi-Fi", "Not configured", "-", UiTone::Neutral),
           layer("Server", "Not configured", "-", UiTone::Neutral),
           layer("Monitor", "Not configured", "-", UiTone::Neutral)}};
      break;
    case DeviceState::WifiConnecting:
      result = {"Wi-Fi connecting", "-", "Connecting to the local network",
                UiTone::Neutral, true,
                {layer("Wi-Fi", "Connecting", "-", UiTone::Neutral),
                 layer("Server", "Not evaluated", "-", UiTone::Neutral),
                 layer("Monitor", "Not evaluated", "-", UiTone::Neutral)}};
      break;
    case DeviceState::WifiOffline:
      result = {"Wi-Fi offline", "X", "NOVA cannot reach the local network",
                UiTone::Critical, true,
                {layer("Wi-Fi", "Disconnected", "X", UiTone::Critical),
                 layer("Server", "Not evaluated", "-", UiTone::Neutral),
                 layer("Monitor", "Not evaluated", "-", UiTone::Neutral)}};
      break;
    case DeviceState::ServerConnecting:
      result = {"Server connecting", "-",
                "Waiting for the first server snapshot", UiTone::Neutral,
                true,
                {layer("Wi-Fi", "Connected", "OK", UiTone::Healthy),
                 layer("Server", "Connecting", "-", UiTone::Neutral),
                 layer("Monitor", "Not evaluated", "-", UiTone::Neutral)}};
      break;
    case DeviceState::Healthy:
      result = freshContent("Healthy", "OK", "All monitored systems normal",
                            UiTone::Healthy);
      break;
    case DeviceState::Warning:
      result = freshContent("Warning", "!", "Server needs attention",
                            UiTone::Warning);
      break;
    case DeviceState::Critical:
      result = freshContent("Critical", "X",
                            "Server needs immediate attention",
                            UiTone::Critical);
      break;
    case DeviceState::Stale:
      result = {"Data stale", "",
                "Last update is taking longer than expected", UiTone::Stale,
                true,
                {layer("Wi-Fi", "Connected", "OK", UiTone::Healthy),
                 layer("Server", "Unknown", "-", UiTone::Neutral),
                 layer("Monitor", "Not evaluated", "-", UiTone::Neutral)}};
      break;
    case DeviceState::ServerOffline:
      result = {"Server offline", "X", "No server response for 30 seconds",
                UiTone::Critical, true,
                {layer("Wi-Fi", "Connected", "OK", UiTone::Healthy),
                 layer("Server", "No response", "X", UiTone::Critical),
                 layer("Monitor", "Not evaluated", "-", UiTone::Neutral)}};
      break;
    case DeviceState::MonitorError:
      result = {"Monitor error", "!", monitorError(view.monitorError),
                UiTone::Error, true,
                {layer("Wi-Fi", "Connected", "OK", UiTone::Healthy),
                 layer("Server", "Unknown", "-", UiTone::Neutral),
                 layer("Monitor", "Not evaluated", "-", UiTone::Neutral)}};
      break;
  }
  return result;
}

void DashboardPresenter::formatFreshness(const DeviceView& view, char* output,
                                         size_t capacity) {
  const unsigned long ageSeconds =
      static_cast<unsigned long>(view.snapshotAgeMs / 1000U);
  if (view.state == DeviceState::SetupRequired) {
    std::snprintf(output, capacity, "NOT CONFIGURED");
  } else if (view.lastKnown) {
    std::snprintf(output, capacity, "LAST KNOWN | %lus", ageSeconds);
  } else if (view.hasSnapshot) {
    std::snprintf(output, capacity, "LIVE | %lus", ageSeconds);
  } else {
    switch (view.state) {
      case DeviceState::Starting:
        std::snprintf(output, capacity, "STARTING");
        break;
      case DeviceState::WifiConnecting:
        std::snprintf(output, capacity, "WI-FI CONNECTING");
        break;
      case DeviceState::WifiOffline:
        std::snprintf(output, capacity, "WI-FI OFFLINE");
        break;
      case DeviceState::ServerConnecting:
        std::snprintf(output, capacity, "WAITING FOR SERVER");
        break;
      case DeviceState::ServerOffline:
        std::snprintf(output, capacity, "SERVER OFFLINE");
        break;
      case DeviceState::MonitorError:
        std::snprintf(output, capacity, "MONITOR ERROR");
        break;
      default:
        std::snprintf(output, capacity, "NO CURRENT DATA");
        break;
    }
  }
}

void DashboardPresenter::formatMetric(Nullable<uint16_t> metric, char* output,
                                      size_t capacity) {
  if (!metric.available) {
    std::snprintf(output, capacity, "Unavailable");
    return;
  }
  std::snprintf(output, capacity, "%u.%u%%",
                static_cast<unsigned>(metric.value / 10U),
                static_cast<unsigned>(metric.value % 10U));
}

void DashboardPresenter::formatUptime(Nullable<uint32_t> uptime, char* output,
                                      size_t capacity) {
  if (!uptime.available) {
    std::snprintf(output, capacity, "Unavailable");
    return;
  }
  const uint32_t days = uptime.value / 86400U;
  const uint32_t hours = (uptime.value / 3600U) % 24U;
  const uint32_t minutes = (uptime.value / 60U) % 60U;
  const uint32_t seconds = uptime.value % 60U;
  if (days > 0U) {
    std::snprintf(output, capacity, "%lud %luh %lum",
                  static_cast<unsigned long>(days),
                  static_cast<unsigned long>(hours),
                  static_cast<unsigned long>(minutes));
  } else if (hours > 0U) {
    std::snprintf(output, capacity, "%luh %lum",
                  static_cast<unsigned long>(hours),
                  static_cast<unsigned long>(minutes));
  } else {
    std::snprintf(output, capacity, "%lum %lus",
                  static_cast<unsigned long>(minutes),
                  static_cast<unsigned long>(seconds));
  }
}

ServiceContent DashboardPresenter::service(ServiceState state) {
  switch (state) {
    case ServiceState::Healthy: return {"OK", "HEALTHY", UiTone::Healthy};
    case ServiceState::Warning: return {"!", "WARNING", UiTone::Warning};
    case ServiceState::Critical: return {"X", "CRITICAL", UiTone::Critical};
    case ServiceState::Unknown: return {"-", "UNKNOWN", UiTone::Neutral};
  }
  return {"-", "UNKNOWN", UiTone::Neutral};
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
