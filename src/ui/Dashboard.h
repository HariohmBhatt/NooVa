#pragma once

#include <lvgl.h>

#include <cstdint>

#include "config/BoardConfig.h"
#include "hardware/BoardDisplay.h"
#include "hardware/BoardTouch.h"
#include "status/SentinelModel.h"
#include "ui/DashboardPresenter.h"

namespace nova {

/** Fixed-allocation LVGL dashboard for the 320x480 Sentinel display. */
class Dashboard {
 public:
  bool begin(BoardDisplay& display, BoardTouch& touch);
  void update(const DeviceView& view, uint32_t nowMs);
  void process();

 private:
  enum class Page : uint8_t { Home, Details };
  static constexpr uint16_t kBufferRows = 20;

  static void flush(lv_disp_drv_t* driver, const lv_area_t* area,
                    lv_color_t* pixels);
  static void readTouch(lv_indev_drv_t* driver, lv_indev_data_t* data);
  static void onNavigation(lv_event_t* event);

  void createHome();
  void createDetails();
  void showPage(Page page);
  void render(const DeviceView& view, uint32_t ageSeconds);
  void renderHeader(lv_obj_t* freshness, lv_obj_t* band,
                    const DeviceView& view, uint32_t ageSeconds);
  void renderMetrics(const DeviceView& view);
  void applyTone(UiTone tone);

  BoardDisplay* display_ = nullptr;
  BoardTouch* touch_ = nullptr;
  lv_disp_draw_buf_t drawBuffer_{};
  lv_disp_drv_t displayDriver_{};
  lv_indev_drv_t inputDriver_{};
  lv_color_t pixels_[board::kDisplayWidth * kBufferRows]{};

  lv_obj_t* home_ = nullptr;
  lv_obj_t* homeFreshness_ = nullptr;
  lv_obj_t* homeBand_ = nullptr;
  lv_obj_t* homeGlyph_ = nullptr;
  lv_obj_t* homeStaleArc_ = nullptr;
  lv_obj_t* homeTitle_ = nullptr;
  lv_obj_t* homeSummary_ = nullptr;
  lv_obj_t* metrics_ = nullptr;
  lv_obj_t* metricValues_[3]{};
  lv_obj_t* metricBars_[3]{};
  lv_obj_t* path_ = nullptr;
  lv_obj_t* pathNames_[3]{};
  lv_obj_t* pathStates_[3]{};
  lv_obj_t* pathGlyphs_[3]{};
  lv_obj_t* services_ = nullptr;
  lv_obj_t* serviceGlyphs_[kMaxServices]{};
  lv_obj_t* serviceNames_[kMaxServices]{};
  lv_obj_t* serviceCount_ = nullptr;

  lv_obj_t* details_ = nullptr;
  lv_obj_t* detailsFreshness_ = nullptr;
  lv_obj_t* detailsBand_ = nullptr;
  lv_obj_t* detailsScroll_ = nullptr;
  lv_obj_t* detailsTitle_ = nullptr;
  lv_obj_t* detailsSummary_ = nullptr;
  lv_obj_t* detailsMetrics_[3]{};
  lv_obj_t* detailReasons_[kMaxReasons]{};
  lv_obj_t* detailServices_[kMaxServices]{};

  DeviceState renderedState_ = DeviceState::Starting;
  uint32_t renderedSequence_ = UINT32_MAX;
  uint32_t renderedAgeSeconds_ = UINT32_MAX;
  bool renderedSnapshot_ = false;
  bool ready_ = false;
  Page page_ = Page::Home;
};

}  // namespace nova
