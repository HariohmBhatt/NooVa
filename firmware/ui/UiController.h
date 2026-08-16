#pragma once

#include <lvgl.h>

#include <cstddef>
#include <cstdint>

#include "../core/Logger.h"
#include "../hardware/BoardDisplay.h"
#include "../hardware/BoardPins.h"
#include "../hardware/BoardTouch.h"
#include "../network/ServerTelemetryService.h"
#include "../telemetry/EspHealthService.h"
#include "../network/WifiService.h"

namespace nova {

/**
 * Owns the single device-status view and its transient Wi-Fi setup sheet.
 *
 * The hub supplies server CPU/GPU telemetry while board-local services supply
 * connectivity, ESP CPU, storage, memory, temperature, and uptime.
 */
class UiController {
 public:
  /** Connect LVGL to board services and build the device status screen. */
  UiController(BoardDisplay& display, BoardTouch& touch, Logger& logger,
               WifiService& wifi, ServerTelemetryService& telemetry,
               EspHealthService& espHealth);

  /** Initialize LVGL and create the dashboard and Wi-Fi setup sheet. */
  bool begin();

  /** Advance LVGL and refresh the visible device statistics. */
  void update();

  /** Return whether the UI has been initialized successfully. */
  bool isReady() const;

 private:
  enum class View : uint8_t {
    Dashboard,
    WifiSetup,
  };

  enum class Stat : uint8_t {
    Wifi,
    IpAddress,
    ServerCpu,
    EspCpu,
    Gpu,
    GpuTemperature,
    GpuVram,
    Storage,
    Uptime,
    MemoryFree,
    Temperature,
    Count,
  };

  static constexpr uint8_t kBufferLines = 40;
  static constexpr uint32_t kRefreshPeriodMs = 500;
  static constexpr uint32_t kChartPeriodMs = 60000;
  static constexpr size_t kChartPointCount = 11;
  static constexpr size_t kStatCount = static_cast<size_t>(Stat::Count);
  static constexpr int16_t kMetricUnavailable = -1;
  static constexpr uint32_t kBytesPerMegabyte = 1024U * 1024U;

  static void flushDisplay(lv_disp_drv_t* driver, const lv_area_t* area,
                           lv_color_t* color);
  static void readTouch(lv_indev_drv_t* driver, lv_indev_data_t* data);
  static void handleDashboardControls(lv_event_t* event);
  static void handleWifiControls(lv_event_t* event);
  static void handleWifiKeyboard(lv_event_t* event);

  void buildUi();
  void buildDashboard();
  void buildTelemetry(lv_obj_t* telemetry);
  void buildStats(lv_obj_t* stats);
  void buildWifiSheet();
  void buildWifiPasswordControls();
  void refreshDashboard();
  void refreshStats();
  void refreshChart();
  void refreshWifiSheet();
  int16_t currentGpuPercent() const;
  void setStatValue(Stat stat, const char* value);
  void showWifiSheet();
  void hideWifiSheet();
  void showWifiPassword(size_t networkIndex);
  void hideWifiPassword();
  void setWifiPasswordMode(bool visible);
  void renderWifiNetworks();

  BoardDisplay& display_;
  BoardTouch& touch_;
  Logger& logger_;
  WifiService& wifi_;
  ServerTelemetryService& telemetry_;
  EspHealthService& espHealth_;
  lv_disp_draw_buf_t drawBuffer_ = {};
  lv_disp_drv_t displayDriver_ = {};
  lv_indev_drv_t inputDriver_ = {};
  lv_color_t drawPixels_[board::kDisplayWidth * kBufferLines] = {};

  lv_obj_t* telemetryChart_ = nullptr;
  lv_chart_series_t* cpuSeries_ = nullptr;
  lv_chart_series_t* gpuSeries_ = nullptr;
  lv_obj_t* cpuLegend_ = nullptr;
  lv_obj_t* gpuLegend_ = nullptr;
  lv_obj_t* statValues_[kStatCount] = {};
  lv_obj_t* wifiActionButton_ = nullptr;
  lv_obj_t* wifiSheet_ = nullptr;
  lv_obj_t* wifiStatus_ = nullptr;
  lv_obj_t* wifiList_ = nullptr;
  lv_obj_t* wifiScanButton_ = nullptr;
  lv_obj_t* wifiCloseButton_ = nullptr;
  lv_obj_t* wifiBackButton_ = nullptr;
  lv_obj_t* wifiPassword_ = nullptr;
  lv_obj_t* wifiKeyboard_ = nullptr;
  lv_obj_t* wifiNetworkButtons_[WifiService::kMaxNetworks] = {};

  int16_t cpuHistory_[kChartPointCount] = {};
  int16_t gpuHistory_[kChartPointCount] = {};
  uint32_t lastRefreshAt_ = 0;
  uint32_t lastDashboardAt_ = 0;
  uint32_t lastChartAt_ = 0;
  int16_t serverCpuPercent_ = kMetricUnavailable;
  int16_t cpuUsagePercent_ = kMetricUnavailable;
  int16_t gpuUsagePercent_ = kMetricUnavailable;
  size_t selectedNetworkIndex_ = WifiService::kMaxNetworks;
  size_t renderedNetworkCount_ = WifiService::kMaxNetworks;
  bool cpuChartSeeded_ = false;
  bool gpuChartSeeded_ = false;
  bool wifiListDirty_ = true;
  bool renderedScanInProgress_ = false;
  View view_ = View::Dashboard;
  bool ready_ = false;
};

}  // namespace nova
