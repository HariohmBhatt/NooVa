#pragma once

#include <lvgl.h>

#include <cstddef>
#include <cstdint>

#include "../core/Logger.h"
#include "../hardware/BoardDisplay.h"
#include "../hardware/BoardPins.h"
#include "../hardware/BoardTouch.h"
#include "../network/SshService.h"
#include "../network/WifiService.h"

namespace nova {

class UiController {
 public:
  /** Connect LVGL to board services and build the device status screen. */
  UiController(BoardDisplay& display, BoardTouch& touch, Logger& logger,
               WifiService& wifi, SshService& ssh);

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

  static constexpr uint8_t kBufferLines = 40;
  static constexpr uint8_t kCpuCoreCount = 2;
  static constexpr uint32_t kRefreshPeriodMs = 500;
  static constexpr uint32_t kChartPeriodMs = 5000;
  static constexpr size_t kChartPointCount = 8;
  static constexpr size_t kStatCount = 8;
  static constexpr int16_t kMetricUnavailable = -1;

  static void flushDisplay(lv_disp_drv_t* driver, const lv_area_t* area,
                           lv_color_t* color);
  static void readTouch(lv_indev_drv_t* driver, lv_indev_data_t* data);
  static void handleDashboardControls(lv_event_t* event);
  static void handleWifiControls(lv_event_t* event);
  static void handleWifiKeyboard(lv_event_t* event);

  void buildUi();
  void buildDashboard();
  void buildWifiSheet();
  void refreshDashboard();
  void refreshStats();
  void refreshChart();
  void refreshWifiSheet();
  void sampleCpuUsage();
  void setStatValue(size_t index, const char* value);
  void showWifiSheet();
  void hideWifiSheet();
  void showWifiPassword(size_t networkIndex);
  void hideWifiPassword();
  void renderWifiNetworks();

  BoardDisplay& display_;
  BoardTouch& touch_;
  Logger& logger_;
  WifiService& wifi_;
  SshService& ssh_;
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
  uint32_t lastIdleRuntime_[kCpuCoreCount] = {};
  uint32_t lastCpuSampleAt_ = 0;
  uint32_t lastRefreshAt_ = 0;
  uint32_t lastChartAt_ = 0;
  int16_t cpuUsagePercent_ = kMetricUnavailable;
  size_t selectedNetworkIndex_ = WifiService::kMaxNetworks;
  size_t renderedNetworkCount_ = WifiService::kMaxNetworks;
  bool cpuSampleReady_ = false;
  bool chartSeeded_ = false;
  bool wifiListDirty_ = true;
  bool renderedScanInProgress_ = false;
  View view_ = View::Dashboard;
  bool ready_ = false;
};

}  // namespace nova
