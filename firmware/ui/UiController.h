#pragma once

#include <lvgl.h>

#include <cstddef>
#include <cstdint>

#include "../core/Logger.h"
#include "../hardware/BoardDisplay.h"
#include "../hardware/BoardPins.h"
#include "../hardware/BoardTouch.h"
#include "../network/WifiService.h"

namespace nova {

class UiController {
 public:
  /** Connect LVGL to board services and build the application UI. */
  UiController(BoardDisplay& display, BoardTouch& touch, Logger& logger,
               WifiService& wifi);

  /** Initialize LVGL and create the offline-capable application screens. */
  bool begin();

  /** Advance LVGL and refresh the visible diagnostic information. */
  void update();

  /** Return whether the UI has been initialized successfully. */
  bool isReady() const;

 private:
  enum class Page : uint8_t {
    Home,
    Diagnostics,
    Wifi,
    Logs,
  };

  static constexpr uint8_t kBufferLines = 40;
  static constexpr uint32_t kLogRefreshPeriodMs = 500;
  static constexpr size_t kLogSnapshotCapacity = 64;
  static constexpr size_t kLogTextCapacity = 64 * 112;

  static void flushDisplay(lv_disp_drv_t* driver, const lv_area_t* area,
                           lv_color_t* color);
  static void readTouch(lv_indev_drv_t* driver, lv_indev_data_t* data);
  static void handleNavigation(lv_event_t* event);
  static void handleWifiControls(lv_event_t* event);
  static void handleWifiKeyboard(lv_event_t* event);

  void buildUi();
  void showPage(Page page);
  void showWifiPassword(size_t networkIndex);
  void hideWifiPassword();
  void updateHomeView();
  void updateDiagnosticsView();
  void updateWifiView();
  void updateLogView();
  void preparePage(lv_obj_t* page);
  lv_obj_t* createNavigationButton(const char* text, int16_t x);
  const char* levelName(LogLevel level) const;

  BoardDisplay& display_;
  BoardTouch& touch_;
  Logger& logger_;
  WifiService& wifi_;
  lv_disp_draw_buf_t drawBuffer_ = {};
  lv_disp_drv_t displayDriver_ = {};
  lv_indev_drv_t inputDriver_ = {};
  lv_color_t drawPixels_[board::kDisplayWidth * kBufferLines] = {};
  lv_obj_t* homePage_ = nullptr;
  lv_obj_t* diagnosticsPage_ = nullptr;
  lv_obj_t* wifiPage_ = nullptr;
  lv_obj_t* logsPage_ = nullptr;
  lv_obj_t* logText_ = nullptr;
  lv_obj_t* homeStatus_ = nullptr;
  lv_obj_t* diagnosticsStatus_ = nullptr;
  lv_obj_t* wifiStatus_ = nullptr;
  lv_obj_t* wifiList_ = nullptr;
  lv_obj_t* wifiScanButton_ = nullptr;
  lv_obj_t* wifiPassword_ = nullptr;
  lv_obj_t* wifiKeyboard_ = nullptr;
  lv_obj_t* navigationHome_ = nullptr;
  lv_obj_t* navigationDiagnostics_ = nullptr;
  lv_obj_t* navigationWifi_ = nullptr;
  lv_obj_t* navigationLogs_ = nullptr;
  lv_obj_t* wifiNetworkButtons_[WifiService::kMaxNetworks] = {};
  size_t selectedNetworkIndex_ = WifiService::kMaxNetworks;
  size_t renderedNetworkCount_ = 0;
  LogEntry logSnapshot_[kLogSnapshotCapacity] = {};
  char logTextBuffer_[kLogTextCapacity] = {};
  uint32_t lastTickAt_ = 0;
  uint32_t lastLogRefreshAt_ = 0;
  bool ready_ = false;
};

}  // namespace nova
