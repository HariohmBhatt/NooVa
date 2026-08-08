#pragma once

#include <lvgl.h>

#include <cstddef>
#include <cstdint>

#include "../core/Logger.h"
#include "../hardware/BoardDisplay.h"
#include "../hardware/BoardPins.h"
#include "../hardware/BoardTouch.h"

namespace nova {

class UiController {
 public:
  /** Connect LVGL to the verified display and touch drivers and build the UI. */
  UiController(BoardDisplay& display, BoardTouch& touch, Logger& logger);

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

  void buildUi();
  void showPage(Page page);
  void updateLogView();
  void preparePage(lv_obj_t* page);
  lv_obj_t* createNavigationButton(const char* text, int16_t x);
  const char* levelName(LogLevel level) const;

  BoardDisplay& display_;
  BoardTouch& touch_;
  Logger& logger_;
  lv_disp_draw_buf_t drawBuffer_ = {};
  lv_disp_drv_t displayDriver_ = {};
  lv_indev_drv_t inputDriver_ = {};
  lv_color_t drawPixels_[board::kDisplayWidth * kBufferLines] = {};
  lv_obj_t* homePage_ = nullptr;
  lv_obj_t* diagnosticsPage_ = nullptr;
  lv_obj_t* logsPage_ = nullptr;
  lv_obj_t* logText_ = nullptr;
  lv_obj_t* homeStatus_ = nullptr;
  lv_obj_t* navigationHome_ = nullptr;
  lv_obj_t* navigationDiagnostics_ = nullptr;
  lv_obj_t* navigationLogs_ = nullptr;
  LogEntry logSnapshot_[kLogSnapshotCapacity] = {};
  char logTextBuffer_[kLogTextCapacity] = {};
  uint32_t lastTickAt_ = 0;
  uint32_t lastLogRefreshAt_ = 0;
  bool ready_ = false;
};

}  // namespace nova
