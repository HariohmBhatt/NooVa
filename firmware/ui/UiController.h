#pragma once

#include <lvgl.h>

#include <cstddef>
#include <cstdint>

#include "../core/Logger.h"
#include "../hardware/BoardDisplay.h"
#include "../hardware/BoardPins.h"
#include "../hardware/BoardTouch.h"
#include "../network/HubConnectionService.h"
#include "../network/SshService.h"
#include "../network/WifiService.h"

namespace nova {

class UiController {
 public:
  /** Connect LVGL to board services and build the dashboard UI. */
  UiController(BoardDisplay& display, BoardTouch& touch, Logger& logger,
               WifiService& wifi, SshService& ssh, HubConnectionService& hub);

  /** Initialize LVGL and create the dashboard and recovery screens. */
  bool begin();

  /** Advance LVGL and refresh server, setup, and diagnostic state. */
  void update();

  /** Return whether the UI has been initialized successfully. */
  bool isReady() const;

 private:
  enum class Page : uint8_t {
    Home,
    Server,
    Setup,
    Diagnostics,
    Wifi,
    Ssh,
    Logs,
  };

  static constexpr uint8_t kBufferLines = 40;
  static constexpr uint32_t kRefreshPeriodMs = 500;
  static constexpr size_t kLogSnapshotCapacity = 64;
  static constexpr size_t kLogTextCapacity = 64 * 112;

  static void flushDisplay(lv_disp_drv_t* driver, const lv_area_t* area,
                           lv_color_t* color);
  static void readTouch(lv_indev_drv_t* driver, lv_indev_data_t* data);
  static void handleNavigation(lv_event_t* event);
  static void handleHubSetup(lv_event_t* event);
  static void handleWifiControls(lv_event_t* event);
  static void handleWifiKeyboard(lv_event_t* event);
  static void handleSshControls(lv_event_t* event);
  static void handleSshKeyboard(lv_event_t* event);

  void buildUi();
  void buildWifiPage();
  void buildSshPage();
  void buildLogPage();
  void showPage(Page page);
  void setNavigationVisible(bool visible);
  void restoreNavigationAfterTouchRelease();
  void preparePage(lv_obj_t* page);
  void showWifiPassword(size_t networkIndex);
  void hideWifiPassword();
  void showSshPassword();
  void hideSshPassword();
  void updateHomeView();
  void updateServerView();
  void updateSetupView();
  void updateDiagnosticsView();
  void updateWifiView();
  void updateSshView();
  void updateLogView();
  lv_obj_t* createNavigationButton(const char* text, int16_t x);
  const char* levelName(LogLevel level) const;

  BoardDisplay& display_;
  BoardTouch& touch_;
  Logger& logger_;
  WifiService& wifi_;
  SshService& ssh_;
  HubConnectionService& hub_;
  lv_disp_draw_buf_t drawBuffer_ = {};
  lv_disp_drv_t displayDriver_ = {};
  lv_indev_drv_t inputDriver_ = {};
  lv_color_t drawPixels_[board::kDisplayWidth * kBufferLines] = {};
  lv_obj_t* homePage_ = nullptr;
  lv_obj_t* serverPage_ = nullptr;
  lv_obj_t* setupPage_ = nullptr;
  lv_obj_t* diagnosticsPage_ = nullptr;
  lv_obj_t* wifiPage_ = nullptr;
  lv_obj_t* sshPage_ = nullptr;
  lv_obj_t* logsPage_ = nullptr;
  lv_obj_t* homeClock_ = nullptr;
  lv_obj_t* homeState_ = nullptr;
  lv_obj_t* homeLatency_ = nullptr;
  lv_obj_t* homeCpu_ = nullptr;
  lv_obj_t* homeMemory_ = nullptr;
  lv_obj_t* homeDisk_ = nullptr;
  lv_obj_t* homeNetwork_ = nullptr;
  lv_obj_t* serverStatus_ = nullptr;
  lv_obj_t* serverMetrics_ = nullptr;
  lv_obj_t* setupStatus_ = nullptr;
  lv_obj_t* setupWifiButton_ = nullptr;
  lv_obj_t* setupSshButton_ = nullptr;
  lv_obj_t* diagnosticsStatus_ = nullptr;
  lv_obj_t* wifiStatus_ = nullptr;
  lv_obj_t* wifiList_ = nullptr;
  lv_obj_t* wifiScanButton_ = nullptr;
  lv_obj_t* wifiPassword_ = nullptr;
  lv_obj_t* wifiKeyboard_ = nullptr;
  lv_obj_t* wifiBackButton_ = nullptr;
  lv_obj_t* sshStatus_ = nullptr;
  lv_obj_t* sshActionButton_ = nullptr;
  lv_obj_t* sshPassword_ = nullptr;
  lv_obj_t* sshKeyboard_ = nullptr;
  lv_obj_t* sshInstructions_ = nullptr;
  lv_obj_t* sshBackButton_ = nullptr;
  lv_obj_t* logText_ = nullptr;
  lv_obj_t* navigationHome_ = nullptr;
  lv_obj_t* navigationServer_ = nullptr;
  lv_obj_t* navigationSetup_ = nullptr;
  lv_obj_t* navigationDiagnostics_ = nullptr;
  lv_obj_t* navigationLogs_ = nullptr;
  lv_obj_t* wifiNetworkButtons_[WifiService::kMaxNetworks] = {};
  size_t selectedNetworkIndex_ = WifiService::kMaxNetworks;
  size_t renderedNetworkCount_ = 0;
  LogEntry logSnapshot_[kLogSnapshotCapacity] = {};
  char logTextBuffer_[kLogTextCapacity] = {};
  uint32_t lastTickAt_ = 0;
  uint32_t lastRefreshAt_ = 0;
  bool touchPressed_ = false;
  bool navigationRestorePending_ = false;
  bool ready_ = false;
};

}  // namespace nova
