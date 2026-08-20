#include <Arduino.h>

#if __has_include("config/Provisioning.h")
#include "config/Provisioning.h"
#else
#include "config/ProvisioningBuild.h"
#endif

#include "hardware/BoardDisplay.h"
#include "hardware/BoardTouch.h"
#include "network/StatusClient.h"
#include "network/WifiManager.h"
#include "status/SentinelModel.h"

namespace {

constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kSerialReportIntervalMs = 1000;
constexpr uint16_t kColorStarting = 0x0861;
constexpr uint16_t kColorHealthy = 0x0328;
constexpr uint16_t kColorWarning = 0xB420;
constexpr uint16_t kColorCritical = 0x7800;
constexpr uint16_t kColorOffline = 0x2104;
constexpr uint16_t kColorMonitorError = 0x500F;

nova::BoardDisplay gDisplay;
nova::BoardTouch gTouch;
nova::WifiManager gWifi;
nova::StatusClient gStatusClient;
nova::SentinelModel gModel;
nova::WifiManagerState gPreviousWifiState = nova::WifiManagerState::StorageError;
nova::DeviceState gPreviousDeviceState = nova::DeviceState::Starting;
uint32_t gLastSerialReportAt = 0;
bool gConfigured = false;
bool gWasTouched = false;

const char* stateName(nova::DeviceState state) {
  switch (state) {
    case nova::DeviceState::Starting:
      return "starting";
    case nova::DeviceState::SetupRequired:
      return "setup_required";
    case nova::DeviceState::WifiConnecting:
      return "wifi_connecting";
    case nova::DeviceState::WifiOffline:
      return "wifi_offline";
    case nova::DeviceState::ServerConnecting:
      return "server_connecting";
    case nova::DeviceState::Healthy:
      return "healthy";
    case nova::DeviceState::Warning:
      return "warning";
    case nova::DeviceState::Critical:
      return "critical";
    case nova::DeviceState::Stale:
      return "stale";
    case nova::DeviceState::ServerOffline:
      return "server_offline";
    case nova::DeviceState::MonitorError:
      return "monitor_error";
  }
  return "unknown";
}

uint16_t stateColor(nova::DeviceState state) {
  switch (state) {
    case nova::DeviceState::Healthy:
      return kColorHealthy;
    case nova::DeviceState::Warning:
    case nova::DeviceState::Stale:
      return kColorWarning;
    case nova::DeviceState::Critical:
      return kColorCritical;
    case nova::DeviceState::MonitorError:
      return kColorMonitorError;
    case nova::DeviceState::WifiOffline:
    case nova::DeviceState::ServerOffline:
      return kColorOffline;
    default:
      return kColorStarting;
  }
}

nova::WifiConnectionState modelWifiState(nova::WifiManagerState state) {
  switch (state) {
    case nova::WifiManagerState::Connecting:
      return nova::WifiConnectionState::Connecting;
    case nova::WifiManagerState::Connected:
      return nova::WifiConnectionState::Connected;
    default:
      return nova::WifiConnectionState::Offline;
  }
}

void updateTouch() {
  nova::TouchPoint point{};
  if (!gTouch.read(point)) {
    return;
  }
  if (point.pressed && !gWasTouched) {
    Serial.printf("[NOVA] TOUCH press x=%d y=%d\n", point.x, point.y);
  } else if (!point.pressed && gWasTouched) {
    Serial.println("[NOVA] TOUCH release");
  }
  gWasTouched = point.pressed;
}

void updatePresentation(uint32_t nowMs) {
  const nova::DeviceView view = gModel.view(nowMs);
  if (view.state != gPreviousDeviceState) {
    gPreviousDeviceState = view.state;
    gDisplay.clear(stateColor(view.state));
    Serial.printf("[NOVA] STATE=%s\n", stateName(view.state));
  }

  if (nowMs - gLastSerialReportAt >= kSerialReportIntervalMs) {
    gLastSerialReportAt = nowMs;
    Serial.printf("[NOVA] VIEW state=%s snapshot=%s age_ms=%lu rssi=%ld heap=%lu\n",
                  stateName(view.state), view.hasSnapshot ? "yes" : "no",
                  static_cast<unsigned long>(view.snapshotAgeMs),
                  static_cast<long>(gWifi.rssi()),
                  static_cast<unsigned long>(ESP.getFreeHeap()));
  }
}

}  // namespace

void setup() {
  Serial.begin(kSerialBaud);
  const uint32_t nowMs = millis();
  Serial.println("[NOVA] SENTINEL_0_1_BOOT");
  Serial.printf("[NOVA] CHIP=%s FLASH=%lu PSRAM=%s\n", ESP.getChipModel(),
                static_cast<unsigned long>(ESP.getFlashChipSize()),
                psramFound() ? "ready" : "missing");

  const bool displayReady = gDisplay.begin();
  const bool touchReady = displayReady && gTouch.begin();
  Serial.printf("[NOVA] DISPLAY=%s geometry=%ux%u TOUCH=%s\n",
                displayReady ? "ready" : "failed", gDisplay.width(),
                gDisplay.height(), touchReady ? "ready" : "failed");

  const bool wifiStorageReady = gWifi.begin(nova::provisioning::kWifiSsid,
                                            nova::provisioning::kWifiPassword,
                                            nowMs);
  const nova::StatusClientConfig statusConfig{
      nova::provisioning::kStatusAddress,
      nova::provisioning::kStatusTlsName,
      nova::provisioning::kStatusPort,
      nova::provisioning::kStatusPath,
      nova::provisioning::kDeviceToken,
      nova::provisioning::kTlsCaPem};
  const bool statusReady = gStatusClient.begin(statusConfig);
  gConfigured = wifiStorageReady && gWifi.hasCredentials() && statusReady;
  gModel.setConfigured(gConfigured, nowMs);
  if (gConfigured) {
    gPreviousWifiState = gWifi.state();
    gModel.setWifiState(modelWifiState(gPreviousWifiState), nowMs);
  }

  Serial.printf("[NOVA] CONFIGURATION=%s\n",
                gConfigured ? "ready" : "setup_required");
  updatePresentation(nowMs);
}

void loop() {
  const uint32_t nowMs = millis();
  if (gConfigured) {
    gWifi.update(nowMs);
    if (gWifi.state() != gPreviousWifiState) {
      gPreviousWifiState = gWifi.state();
      gModel.setWifiState(modelWifiState(gPreviousWifiState), nowMs);
      if (!gWifi.connected()) {
        gStatusClient.cancel();
      }
    }

    nova::PollOutcome outcome{};
    if (gStatusClient.update(nowMs, gWifi.connected(), outcome)) {
      gModel.apply(outcome, nowMs);
    }
  }

  updateTouch();
  updatePresentation(nowMs);
  // Yield to the Arduino/FreeRTOS networking tasks while maintaining responsive
  // touch polling. No hardware or network module depends on this delay.
  delay(2);
}
