#include <Arduino.h>

#if __has_include("config/Provisioning.h")
#include "config/Provisioning.h"
#else
#include "config/ProvisioningBuild.h"
#endif

#include "hardware/BoardDisplay.h"
#include "hardware/BoardTouch.h"
#include "network/HttpsPollTask.h"
#include "network/StatusClient.h"
#include "network/WifiManager.h"
#include "status/SentinelModel.h"
#include "ui/Dashboard.h"
#include "ui/DashboardPresenter.h"

namespace {

constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kWifiUpdateIntervalMs = 20;
constexpr uint32_t kStatusDispatchIntervalMs = 5;
constexpr uint32_t kPresentationUpdateIntervalMs = 100;
constexpr uint32_t kUiProcessIntervalMs = 5;
constexpr uint32_t kSerialReportIntervalMs = 1000;
constexpr TickType_t kIdleSchedulerWaitTicks = 1;

nova::BoardDisplay gDisplay;
nova::BoardTouch gTouch;
nova::WifiManager gWifi;
nova::StatusClient gStatusClient;
nova::HttpsPollTask gStatusTransport;
nova::SentinelModel gModel;
nova::Dashboard gDashboard;
nova::WifiManagerState gPreviousWifiState = nova::WifiManagerState::StorageError;
nova::DeviceState gPreviousDeviceState = nova::DeviceState::Starting;
uint32_t gLastSerialReportAt = 0;
uint32_t gLastWifiUpdateAt = 0;
uint32_t gLastStatusDispatchAt = 0;
uint32_t gLastPresentationUpdateAt = 0;
uint32_t gLastUiProcessAt = 0;
uint32_t gUiProcessCount = 0;
bool gConfigured = false;

bool cadenceDue(uint32_t nowMs, uint32_t& lastRunAtMs,
                uint32_t intervalMs) {
  if (nowMs - lastRunAtMs < intervalMs) {
    return false;
  }
  lastRunAtMs = nowMs;
  return true;
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

void updatePresentation(uint32_t nowMs) {
  const nova::DeviceView view = gModel.view(nowMs);
  const nova::DashboardContent content = nova::DashboardPresenter::present(view);
  if (view.state != gPreviousDeviceState) {
    gPreviousDeviceState = view.state;
    Serial.printf("[NOVA] STATE=%s\n", content.title);
  }
  gDashboard.update(view, nowMs);

  if (nowMs - gLastSerialReportAt >= kSerialReportIntervalMs) {
    gLastSerialReportAt = nowMs;
    Serial.printf("[NOVA] VIEW state=%s snapshot=%s age_ms=%lu rssi=%ld "
                  "heap=%lu ui_loops=%lu\n",
                  content.title, view.hasSnapshot ? "yes" : "no",
                  static_cast<unsigned long>(view.snapshotAgeMs),
                  static_cast<long>(gWifi.rssi()),
                  static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned long>(gUiProcessCount));
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
  const bool dashboardReady = displayReady && gDashboard.begin(gDisplay, gTouch);
  Serial.printf("[NOVA] DISPLAY=%s geometry=%ux%u TOUCH=%s\n",
                displayReady ? "ready" : "failed", gDisplay.width(),
                gDisplay.height(), touchReady ? "ready" : "failed");
  Serial.printf("[NOVA] UI=%s\n", dashboardReady ? "ready" : "failed");

  const bool wifiStorageReady = gWifi.begin(nova::provisioning::kWifiSsid,
                                            nova::provisioning::kWifiPassword,
                                            nowMs);
  const nova::StatusClientConfig statusConfig{
      nova::provisioning::kStatusTlsName,
      nova::provisioning::kStatusPath,
      nova::provisioning::kDeviceToken};
  const nova::HttpsPollTaskConfig transportConfig{
      nova::provisioning::kStatusAddress,
      nova::provisioning::kStatusPort,
      nova::provisioning::kStatusTlsName,
      nova::provisioning::kTlsCaPem};
  const bool statusReady = gStatusClient.begin(statusConfig) &&
                           gStatusTransport.begin(transportConfig);
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
  if (gConfigured &&
      cadenceDue(nowMs, gLastWifiUpdateAt, kWifiUpdateIntervalMs)) {
    gWifi.update(nowMs);
    if (gWifi.state() != gPreviousWifiState) {
      gPreviousWifiState = gWifi.state();
      gModel.setWifiState(modelWifiState(gPreviousWifiState), nowMs);
    }
  }

  if (gConfigured &&
      cadenceDue(nowMs, gLastStatusDispatchAt, kStatusDispatchIntervalMs)) {
    nova::PollOutcome outcome{};
    if (gStatusClient.update(nowMs, gWifi.connected(), gStatusTransport,
                             outcome)) {
      gModel.apply(outcome, nowMs);
    }
  }

  if (cadenceDue(nowMs, gLastUiProcessAt, kUiProcessIntervalMs)) {
    ++gUiProcessCount;
    gDashboard.process();
  }
  if (cadenceDue(nowMs, gLastPresentationUpdateAt,
                 kPresentationUpdateIntervalMs)) {
    updatePresentation(nowMs);
  }

  // Block for one scheduler tick so lower-priority idle work gets CPU while
  // staying below every named application cadence.
  vTaskDelay(kIdleSchedulerWaitTicks);
}
