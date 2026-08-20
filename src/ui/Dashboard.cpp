#include "Dashboard.h"

#include <cstdio>
#include <cstring>

#include "config/BoardConfig.h"

namespace nova {
namespace {

lv_color_t color(uint32_t hex) { return lv_color_hex(hex); }
constexpr uint32_t kBackground = 0x0B1115;
constexpr uint32_t kPanel = 0x121B21;
constexpr uint32_t kRaised = 0x172229;
constexpr uint32_t kDivider = 0x27343D;
constexpr uint32_t kText = 0xF3F6F7;
constexpr uint32_t kMuted = 0x91A0AA;
constexpr uint32_t kHealthy = 0x66DFAE;
constexpr uint32_t kWarning = 0xFFC857;
constexpr uint32_t kCritical = 0xFF6B63;
constexpr uint32_t kStale = 0x7FC8FF;
constexpr uint32_t kError = 0xD7A6FF;
constexpr uint32_t kMetric = 0x79A9B3;

void baseObject(lv_obj_t* object, uint32_t background = kBackground) {
  lv_obj_set_style_bg_color(object, color(background), 0);
  lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(object, 0, 0);
  lv_obj_set_style_radius(object, 0, 0);
  lv_obj_set_style_pad_all(object, 0, 0);
}

lv_obj_t* label(lv_obj_t* parent, const char* text, int16_t x, int16_t y,
                int16_t width, const lv_font_t* font = &lv_font_montserrat_12,
                uint32_t textColor = kText) {
  lv_obj_t* result = lv_label_create(parent);
  lv_label_set_text(result, text);
  lv_label_set_long_mode(result, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(result, x, y);
  lv_obj_set_width(result, width);
  lv_obj_set_style_text_font(result, font, 0);
  lv_obj_set_style_text_color(result, color(textColor), 0);
  return result;
}

lv_obj_t* button(lv_obj_t* parent, const char* text, int16_t y,
                 lv_event_cb_t callback, void* userData) {
  lv_obj_t* result = lv_btn_create(parent);
  lv_obj_set_pos(result, 12, y);
  lv_obj_set_size(result, 296, 44);
  lv_obj_set_style_bg_color(result, color(kRaised), 0);
  lv_obj_set_style_border_color(result, color(kDivider), 0);
  lv_obj_set_style_border_width(result, 1, 0);
  lv_obj_set_style_radius(result, 10, 0);
  lv_obj_add_event_cb(result, callback, LV_EVENT_CLICKED, userData);
  lv_obj_t* caption = lv_label_create(result);
  lv_label_set_text(caption, text);
  lv_obj_set_style_text_font(caption, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(caption, color(kText), 0);
  lv_obj_center(caption);
  return result;
}

uint32_t toneColor(UiTone tone) {
  switch (tone) {
    case UiTone::Healthy: return kHealthy;
    case UiTone::Warning: return kWarning;
    case UiTone::Critical: return kCritical;
    case UiTone::Stale: return kStale;
    case UiTone::Error: return kError;
    case UiTone::Neutral: return kMuted;
  }
  return kMuted;
}

}  // namespace

bool Dashboard::begin(BoardDisplay& display, BoardTouch& touch) {
  display_ = &display;
  touch_ = &touch;
  if (!display.isReady()) return false;
  lv_init();
  lv_disp_draw_buf_init(&drawBuffer_, pixels_, nullptr,
                        board::kDisplayWidth * kBufferRows);
  lv_disp_drv_init(&displayDriver_);
  displayDriver_.hor_res = board::kDisplayWidth;
  displayDriver_.ver_res = board::kDisplayHeight;
  displayDriver_.flush_cb = flush;
  displayDriver_.draw_buf = &drawBuffer_;
  displayDriver_.user_data = this;
  lv_disp_drv_register(&displayDriver_);
  lv_indev_drv_init(&inputDriver_);
  inputDriver_.type = LV_INDEV_TYPE_POINTER;
  inputDriver_.read_cb = readTouch;
  inputDriver_.user_data = this;
  lv_indev_drv_register(&inputDriver_);
  baseObject(lv_scr_act());
  createHome();
  createDetails();
  showPage(Page::Home);
  ready_ = true;
  return true;
}

void Dashboard::flush(lv_disp_drv_t* driver, const lv_area_t* area,
                      lv_color_t* pixels) {
  auto* self = static_cast<Dashboard*>(driver->user_data);
  self->display_->drawPixels(area->x1, area->y1,
      reinterpret_cast<const uint16_t*>(pixels), area->x2 - area->x1 + 1,
      area->y2 - area->y1 + 1);
  lv_disp_flush_ready(driver);
}

void Dashboard::readTouch(lv_indev_drv_t* driver, lv_indev_data_t* data) {
  auto* self = static_cast<Dashboard*>(driver->user_data);
  TouchPoint point{};
  const bool read = self->touch_ != nullptr && self->touch_->read(point);
  data->state = read && point.pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
  if (read && point.pressed) data->point = {point.x, point.y};
}

void Dashboard::onNavigation(lv_event_t* event) {
  auto* self = static_cast<Dashboard*>(lv_event_get_user_data(event));
  self->showPage(self->page_ == Page::Home ? Page::Details : Page::Home);
}

void Dashboard::createHome() {
  home_ = lv_obj_create(lv_scr_act()); baseObject(home_);
  lv_obj_set_size(home_, 320, 480); lv_obj_clear_flag(home_, LV_OBJ_FLAG_SCROLLABLE);
  label(home_, "NOVA", 15, 14, 70, &lv_font_montserrat_14);
  homeFreshness_ = label(home_, "LIVE", 145, 15, 160, &lv_font_montserrat_12, kMuted);
  lv_obj_set_style_text_align(homeFreshness_, LV_TEXT_ALIGN_RIGHT, 0);
  homeBand_ = lv_obj_create(home_); baseObject(homeBand_, kWarning);
  lv_obj_set_pos(homeBand_, 0, 42); lv_obj_set_size(homeBand_, 320, 30);
  label(homeBand_, "LAST KNOWN DATA", 15, 8, 150, &lv_font_montserrat_12, kBackground);

  homeGlyph_ = label(home_, "+", 16, 80, 54, &lv_font_montserrat_28, kMuted);
  lv_obj_set_style_text_align(homeGlyph_, LV_TEXT_ALIGN_CENTER, 0);
  homeStaleArc_ = lv_arc_create(home_); lv_obj_set_pos(homeStaleArc_, 20, 80);
  lv_obj_set_size(homeStaleArc_, 44, 44); lv_arc_set_angles(homeStaleArc_, 35, 305);
  lv_obj_remove_style(homeStaleArc_, nullptr, LV_PART_KNOB);
  lv_obj_set_style_arc_width(homeStaleArc_, 3, LV_PART_INDICATOR);
  lv_obj_set_style_arc_opa(homeStaleArc_, LV_OPA_TRANSP, LV_PART_MAIN);
  homeTitle_ = label(home_, "Starting", 84, 78, 220, &lv_font_montserrat_28);
  homeSummary_ = label(home_, "Preparing the server sentinel", 84, 112, 220,
                       &lv_font_montserrat_12, kText);

  metrics_ = lv_obj_create(home_); baseObject(metrics_, kPanel);
  lv_obj_set_pos(metrics_, 14, 194); lv_obj_set_size(metrics_, 292, 108);
  const char* metricNames[] = {"CPU", "MEMORY", "DISK"};
  for (uint8_t i = 0; i < 3; ++i) {
    metricNames_[i] = label(metrics_, metricNames[i], 12, 8 + i * 32, 74,
                            &lv_font_montserrat_12, kMuted);
    metricValues_[i] = label(metrics_, "Unavailable", 150, 8 + i * 32, 126,
                             &lv_font_montserrat_12);
    lv_obj_set_style_text_align(metricValues_[i], LV_TEXT_ALIGN_RIGHT, 0);
    metricBars_[i] = lv_bar_create(metrics_); lv_obj_set_pos(metricBars_[i], 12, 25 + i * 32);
    lv_obj_set_size(metricBars_[i], 264, 3); lv_bar_set_range(metricBars_[i], 0, 1000);
    lv_obj_set_style_bg_color(metricBars_[i], color(kDivider), LV_PART_MAIN);
    lv_obj_set_style_bg_color(metricBars_[i], color(kMetric), LV_PART_INDICATOR);
  }

  path_ = lv_obj_create(home_); baseObject(path_);
  lv_obj_set_pos(path_, 16, 176); lv_obj_set_size(path_, 288, 100);
  for (uint8_t i = 0; i < 3; ++i) {
    pathNames_[i] = label(path_, "Layer", 14, i * 32, 90, &lv_font_montserrat_12);
    pathStates_[i] = label(path_, "Not evaluated", 95, i * 32, 145,
                           &lv_font_montserrat_12, kMuted);
    pathGlyphs_[i] = label(path_, "-", 252, i * 32, 30, &lv_font_montserrat_12, kMuted);
    lv_obj_set_style_text_align(pathGlyphs_[i], LV_TEXT_ALIGN_RIGHT, 0);
  }

  services_ = lv_obj_create(home_); baseObject(services_);
  lv_obj_set_pos(services_, 16, 310); lv_obj_set_size(services_, 288, 106);
  label(services_, "SERVICES", 0, 0, 100, &lv_font_montserrat_12, kMuted);
  serviceCount_ = label(services_, "NO CHECKS", 150, 0, 138, &lv_font_montserrat_12, kMuted);
  lv_obj_set_style_text_align(serviceCount_, LV_TEXT_ALIGN_RIGHT, 0);
  for (uint8_t i = 0; i < kMaxServices; ++i) {
    const int16_t x = (i % 2) * 146, y = 23 + (i / 2) * 34;
    lv_obj_t* tile = lv_obj_create(services_); baseObject(tile, kPanel);
    lv_obj_set_pos(tile, x, y); lv_obj_set_size(tile, 142, 30);
    lv_obj_set_style_border_width(tile, 1, 0); lv_obj_set_style_border_color(tile, color(kDivider), 0);
    serviceGlyphs_[i] = label(tile, "-", 5, 7, 24, &lv_font_montserrat_12, kMuted);
    serviceNames_[i] = label(tile, "", 32, 7, 103, &lv_font_montserrat_12);
    lv_label_set_long_mode(serviceNames_[i], LV_LABEL_LONG_DOT);
  }
  button(home_, "Details >", 426, onNavigation, this);
}

void Dashboard::createDetails() {
  details_ = lv_obj_create(lv_scr_act()); baseObject(details_);
  lv_obj_set_size(details_, 320, 480); lv_obj_clear_flag(details_, LV_OBJ_FLAG_SCROLLABLE);
  label(details_, "NOVA", 15, 14, 70, &lv_font_montserrat_14);
  detailsFreshness_ = label(details_, "LIVE", 145, 15, 160, &lv_font_montserrat_12, kMuted);
  lv_obj_set_style_text_align(detailsFreshness_, LV_TEXT_ALIGN_RIGHT, 0);
  detailsBand_ = lv_obj_create(details_); baseObject(detailsBand_, kWarning);
  lv_obj_set_pos(detailsBand_, 0, 42); lv_obj_set_size(detailsBand_, 320, 30);
  label(detailsBand_, "LAST KNOWN DATA", 15, 8, 200, &lv_font_montserrat_12, kBackground);
  detailsScroll_ = lv_obj_create(details_); baseObject(detailsScroll_);
  lv_obj_set_pos(detailsScroll_, 0, 42); lv_obj_set_size(detailsScroll_, 320, 378);
  lv_obj_set_scroll_dir(detailsScroll_, LV_DIR_VER);
  lv_obj_set_style_pad_bottom(detailsScroll_, 64, 0);
  detailsTitle_ = label(detailsScroll_, "Starting", 16, 12, 288, &lv_font_montserrat_22);
  detailsSummary_ = label(detailsScroll_, "Preparing", 16, 46, 288, &lv_font_montserrat_14);
  label(detailsScroll_, "METRICS", 16, 105, 120, &lv_font_montserrat_12, kMuted);
  for (uint8_t i = 0; i < 3; ++i)
    detailsMetrics_[i] = label(detailsScroll_, "Unavailable", 16, 128 + i * 28, 288, &lv_font_montserrat_12);
  label(detailsScroll_, "REASONS", 16, 220, 120, &lv_font_montserrat_12, kMuted);
  for (uint8_t i = 0; i < kMaxReasons; ++i)
    detailReasons_[i] = label(detailsScroll_, "", 16, 246 + i * 68, 288, &lv_font_montserrat_12);
  label(detailsScroll_, "SERVICES", 16, 455, 120, &lv_font_montserrat_12, kMuted);
  for (uint8_t i = 0; i < kMaxServices; ++i)
    detailServices_[i] = label(detailsScroll_, "", 16, 482 + i * 38, 288, &lv_font_montserrat_12);
  button(details_, "< Back", 426, onNavigation, this);
}

void Dashboard::showPage(Page page) {
  page_ = page;
  if (page == Page::Home) { lv_obj_clear_flag(home_, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(details_, LV_OBJ_FLAG_HIDDEN); }
  else { lv_obj_add_flag(home_, LV_OBJ_FLAG_HIDDEN); lv_obj_clear_flag(details_, LV_OBJ_FLAG_HIDDEN); }
}

void Dashboard::renderHeader(lv_obj_t* freshness, lv_obj_t* band,
                             const DeviceView& view, uint32_t ageSeconds) {
  char text[32]{};
  if (view.state == DeviceState::SetupRequired) std::snprintf(text, sizeof(text), "NOT CONFIGURED");
  else std::snprintf(text, sizeof(text), "%s | %lus", view.lastKnown ? "LAST KNOWN" : "LIVE", static_cast<unsigned long>(ageSeconds));
  lv_label_set_text(freshness, text);
  if (view.lastKnown) lv_obj_clear_flag(band, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(band, LV_OBJ_FLAG_HIDDEN);
}

void Dashboard::applyTone(UiTone tone) {
  const lv_color_t accent = color(toneColor(tone));
  lv_obj_set_style_text_color(homeGlyph_, accent, 0);
  lv_obj_set_style_arc_color(homeStaleArc_, accent, LV_PART_INDICATOR);
}

void Dashboard::renderMetrics(const DeviceView& view) {
  const Nullable<uint16_t> values[] = {view.snapshot.cpuPercentTenths, view.snapshot.memoryPercentTenths, view.snapshot.diskPercentTenths};
  const char* names[] = {"CPU", "MEMORY", "DISK"};
  char text[32]{};
  for (uint8_t i = 0; i < 3; ++i) {
    DashboardPresenter::formatMetric(values[i], text, sizeof(text));
    lv_label_set_text(metricValues_[i], text);
    lv_bar_set_value(metricBars_[i], values[i].available ? values[i].value : 0, LV_ANIM_OFF);
    std::snprintf(text, sizeof(text), "%s    %s", names[i], values[i].available ? "" : "Unavailable");
    if (values[i].available) DashboardPresenter::formatMetric(values[i], text + std::strlen(text), sizeof(text) - std::strlen(text));
    lv_label_set_text(detailsMetrics_[i], text);
  }
}

void Dashboard::render(const DeviceView& view, uint32_t ageSeconds) {
  const DashboardContent content = DashboardPresenter::present(view);
  renderHeader(homeFreshness_, homeBand_, view, ageSeconds);
  renderHeader(detailsFreshness_, detailsBand_, view, ageSeconds);
  lv_obj_set_y(detailsScroll_, view.lastKnown ? 72 : 42);
  lv_obj_set_height(detailsScroll_, view.lastKnown ? 348 : 378);
  lv_label_set_text(homeGlyph_, content.glyph);
  if (view.state == DeviceState::Stale) lv_obj_clear_flag(homeStaleArc_, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(homeStaleArc_, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(homeTitle_, content.title); lv_label_set_text(detailsTitle_, content.title);
  const char* summary = view.hasSnapshot && view.snapshot.summary[0] != '\0' && !content.diagnostic ? view.snapshot.summary : content.fallbackSummary;
  lv_label_set_text(homeSummary_, summary); lv_label_set_text(detailsSummary_, summary);
  applyTone(content.tone); renderMetrics(view);
  if (content.diagnostic) {
    lv_obj_clear_flag(path_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(metrics_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(metrics_, 16, 276); lv_obj_set_size(metrics_, 288, 42);
    lv_obj_set_pos(services_, 16, 326);
    for (uint8_t i = 0; i < 3; ++i) {
      lv_obj_set_pos(metricNames_[i], i * 96 + 6, 5); lv_obj_set_width(metricNames_[i], 38);
      lv_obj_set_pos(metricValues_[i], i * 96 + 40, 5); lv_obj_set_width(metricValues_[i], 52);
      lv_obj_add_flag(metricBars_[i], LV_OBJ_FLAG_HIDDEN);
    }
  } else {
    lv_obj_add_flag(path_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(metrics_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(metrics_, 14, 194); lv_obj_set_size(metrics_, 292, 108);
    lv_obj_set_pos(services_, 16, 310);
    for (uint8_t i = 0; i < 3; ++i) {
      lv_obj_set_pos(metricNames_[i], 12, 8 + i * 32); lv_obj_set_width(metricNames_[i], 74);
      lv_obj_set_pos(metricValues_[i], 150, 8 + i * 32); lv_obj_set_width(metricValues_[i], 126);
      lv_obj_clear_flag(metricBars_[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
  for (uint8_t i = 0; i < 3; ++i) { lv_label_set_text(pathNames_[i], content.layers[i].name); lv_label_set_text(pathStates_[i], content.layers[i].status); lv_label_set_text(pathGlyphs_[i], content.layers[i].glyph); }
  char count[24]{};
  if (view.lastKnown) std::snprintf(count, sizeof(count), "LAST KNOWN");
  else if (!view.hasSnapshot || view.snapshot.serviceCount == 0) std::snprintf(count, sizeof(count), "NO CHECKS");
  else { uint8_t healthy=0; for(uint8_t i=0;i<view.snapshot.serviceCount;++i) healthy += view.snapshot.services[i].state==ServiceState::Healthy; std::snprintf(count,sizeof(count),"%u/%u OK",healthy,view.snapshot.serviceCount); }
  lv_label_set_text(serviceCount_, count);
  for (uint8_t i=0;i<kMaxServices;++i) {
    if (view.hasSnapshot && i<view.snapshot.serviceCount) { lv_label_set_text(serviceGlyphs_[i],DashboardPresenter::serviceGlyph(view.snapshot.services[i].state)); lv_label_set_text(serviceNames_[i],view.snapshot.services[i].name); char row[64]{}; std::snprintf(row,sizeof(row),"%s    %s",view.snapshot.services[i].name,DashboardPresenter::serviceName(view.snapshot.services[i].state)); lv_label_set_text(detailServices_[i],row); }
    else { lv_label_set_text(serviceGlyphs_[i],"-"); lv_label_set_text(serviceNames_[i],"Not configured"); lv_label_set_text(detailServices_[i],""); }
  }
  for (uint8_t i=0;i<kMaxReasons;++i) lv_label_set_text(detailReasons_[i], view.hasSnapshot && i<view.snapshot.reasonCount ? view.snapshot.reasons[i].message : "");
}

void Dashboard::update(const DeviceView& view, uint32_t) {
  if (!ready_) return;
  const uint32_t ageSeconds = view.snapshotAgeMs / 1000U;
  const bool identityChanged = view.state != renderedState_ ||
      view.hasSnapshot != renderedSnapshot_ ||
      (view.hasSnapshot && view.snapshot.sequence != renderedSequence_);
  if (!identityChanged) {
    if (ageSeconds == renderedAgeSeconds_) return;
    renderedAgeSeconds_ = ageSeconds;
    renderHeader(homeFreshness_, homeBand_, view, ageSeconds);
    renderHeader(detailsFreshness_, detailsBand_, view, ageSeconds);
    return;
  }
  renderedState_=view.state; renderedSnapshot_=view.hasSnapshot;
  renderedSequence_=view.hasSnapshot?view.snapshot.sequence:0; renderedAgeSeconds_=ageSeconds;
  render(view, ageSeconds);
}

void Dashboard::process() { if (ready_) lv_timer_handler(); }

}  // namespace nova
