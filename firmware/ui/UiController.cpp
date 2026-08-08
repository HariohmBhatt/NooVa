#include "UiController.h"

#include <cstdio>

namespace nova {
namespace {

constexpr uint32_t kBackgroundColor = 0x101820;
constexpr uint32_t kPanelColor = 0x182632;
constexpr uint32_t kAccentColor = 0x39D98A;
constexpr uint32_t kTextColor = 0xE7F1F5;
constexpr uint32_t kMutedTextColor = 0x8BA3AD;

void styleObject(lv_obj_t* object, uint32_t background, uint32_t text) {
  lv_obj_set_style_bg_color(object, lv_color_hex(background), LV_PART_MAIN);
  lv_obj_set_style_text_color(object, lv_color_hex(text), LV_PART_MAIN);
  lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
}

}  // namespace

UiController::UiController(BoardDisplay& display, BoardTouch& touch,
                           Logger& logger)
    : display_(display), touch_(touch), logger_(logger) {}

bool UiController::begin() {
  if (!display_.isReady()) {
    return false;
  }

  lv_init();
  lv_disp_draw_buf_init(&drawBuffer_, drawPixels_, nullptr,
                        board::kDisplayWidth * kBufferLines);
  lv_disp_drv_init(&displayDriver_);
  displayDriver_.hor_res = board::kDisplayWidth;
  displayDriver_.ver_res = board::kDisplayHeight;
  displayDriver_.flush_cb = flushDisplay;
  displayDriver_.draw_buf = &drawBuffer_;
  displayDriver_.user_data = this;
  lv_disp_drv_register(&displayDriver_);

  lv_indev_drv_init(&inputDriver_);
  inputDriver_.type = LV_INDEV_TYPE_POINTER;
  inputDriver_.read_cb = readTouch;
  inputDriver_.user_data = this;
  lv_indev_drv_register(&inputDriver_);

  buildUi();
  lastTickAt_ = millis();
  ready_ = true;
  logger_.write(LogLevel::Info, "Offline UI initialized");
  return true;
}

void UiController::update() {
  if (!ready_) {
    return;
  }

  const uint32_t now = millis();
  lv_tick_inc(now - lastTickAt_);
  lastTickAt_ = now;
  if (now - lastLogRefreshAt_ >= kLogRefreshPeriodMs) {
    lastLogRefreshAt_ = now;
    updateLogView();
  }
  lv_timer_handler();
}

bool UiController::isReady() const { return ready_; }

void UiController::flushDisplay(lv_disp_drv_t* driver, const lv_area_t* area,
                                lv_color_t* color) {
  if (area != nullptr && color != nullptr) {
    const int16_t width = area->x2 - area->x1 + 1;
    const int16_t height = area->y2 - area->y1 + 1;
    auto* controller = static_cast<UiController*>(driver->user_data);
    if (controller != nullptr) {
      controller->display_.drawPixels(area->x1, area->y1,
                                      reinterpret_cast<uint16_t*>(color), width,
                                      height);
    }
  }
  lv_disp_flush_ready(driver);
}

void UiController::readTouch(lv_indev_drv_t* driver, lv_indev_data_t* data) {
  auto* controller = static_cast<UiController*>(driver->user_data);
  TouchPoint point;
  if (controller == nullptr || !controller->touch_.read(point) ||
      !point.pressed) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }

  data->state = LV_INDEV_STATE_PRESSED;
  data->point.x = point.x;
  data->point.y = point.y;
}

void UiController::handleNavigation(lv_event_t* event) {
  auto* controller = static_cast<UiController*>(lv_event_get_user_data(event));
  if (controller == nullptr) {
    return;
  }

  lv_obj_t* target = lv_event_get_target(event);
  if (target == controller->navigationHome_) {
    controller->showPage(Page::Home);
  } else if (target == controller->navigationDiagnostics_) {
    controller->showPage(Page::Diagnostics);
  } else if (target == controller->navigationLogs_) {
    controller->showPage(Page::Logs);
  }
}

void UiController::buildUi() {
  lv_obj_t* screen = lv_scr_act();
  styleObject(screen, kBackgroundColor, kTextColor);

  lv_obj_t* title = lv_label_create(screen);
  lv_label_set_text(title, "NOVA  /  SYSTEM");
  lv_obj_set_pos(title, 12, 10);
  lv_obj_set_style_text_color(title, lv_color_hex(kAccentColor), LV_PART_MAIN);

  lv_obj_t* subtitle = lv_label_create(screen);
  lv_label_set_text(subtitle, "ESP32-S3 appliance");
  lv_obj_set_pos(subtitle, 12, 28);
  lv_obj_set_style_text_color(subtitle, lv_color_hex(kMutedTextColor),
                              LV_PART_MAIN);

  lv_obj_t* content = lv_obj_create(screen);
  lv_obj_set_size(content, 304, 364);
  lv_obj_set_pos(content, 8, 50);
  styleObject(content, kBackgroundColor, kTextColor);
  lv_obj_set_style_pad_all(content, 0, LV_PART_MAIN);
  lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

  homePage_ = lv_obj_create(content);
  diagnosticsPage_ = lv_obj_create(content);
  logsPage_ = lv_obj_create(content);
  preparePage(homePage_);
  preparePage(diagnosticsPage_);
  preparePage(logsPage_);

  lv_obj_t* homeTitle = lv_label_create(homePage_);
  lv_label_set_text(homeTitle, "READY FOR LOCAL OPERATION");
  lv_obj_set_pos(homeTitle, 12, 14);
  lv_obj_set_style_text_color(homeTitle, lv_color_hex(kAccentColor),
                              LV_PART_MAIN);
  homeStatus_ = lv_label_create(homePage_);
  lv_label_set_text_fmt(
      homeStatus_,
      "Display: %s\nTouch: %s\nWi-Fi: NOT CONFIGURED\nSSH: DISABLED\n\n"
      "USB serial is optional.\nThe device is ready to run from a power bank.",
      display_.isReady() ? "READY" : "FAILED",
      touch_.isReady() ? "READY" : "FAILED");
  lv_obj_set_pos(homeStatus_, 12, 54);
  lv_obj_set_style_text_color(homeStatus_, lv_color_hex(kTextColor),
                              LV_PART_MAIN);

  lv_obj_t* diagnosticsTitle = lv_label_create(diagnosticsPage_);
  lv_label_set_text(diagnosticsTitle, "COMPONENT STATUS");
  lv_obj_set_pos(diagnosticsTitle, 12, 14);
  lv_obj_set_style_text_color(diagnosticsTitle, lv_color_hex(kAccentColor),
                              LV_PART_MAIN);
  lv_obj_t* diagnostics = lv_label_create(diagnosticsPage_);
  lv_label_set_text(
      diagnostics,
      "HW-001  Display       READY\nHW-002  Touch         READY\n"
      "HW-003  Wi-Fi        NOT RUN\nHW-004  IMU           NOT RUN\n"
      "HW-005  Audio        NOT RUN\nHW-006  SD card       NOT RUN\n\n"
      "Production service integration is next.");
  lv_obj_set_pos(diagnostics, 12, 54);
  lv_obj_set_style_text_color(diagnostics, lv_color_hex(kTextColor),
                              LV_PART_MAIN);

  lv_obj_t* logsTitle = lv_label_create(logsPage_);
  lv_label_set_text(logsTitle, "LIVE DEBUG LOG");
  lv_obj_set_pos(logsTitle, 12, 14);
  lv_obj_set_style_text_color(logsTitle, lv_color_hex(kAccentColor),
                              LV_PART_MAIN);
  logText_ = lv_textarea_create(logsPage_);
  lv_obj_set_size(logText_, 280, 290);
  lv_obj_set_pos(logText_, 12, 54);
  lv_textarea_set_text(logText_, "Waiting for diagnostic messages...");
  lv_textarea_set_cursor_click_pos(logText_, false);
  styleObject(logText_, kPanelColor, kTextColor);

  navigationHome_ = createNavigationButton("HOME", 8);
  navigationDiagnostics_ = createNavigationButton("DIAG", 112);
  navigationLogs_ = createNavigationButton("LOGS", 216);
  lv_obj_add_flag(diagnosticsPage_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(logsPage_, LV_OBJ_FLAG_HIDDEN);
}

void UiController::showPage(Page page) {
  lv_obj_add_flag(homePage_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(diagnosticsPage_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(logsPage_, LV_OBJ_FLAG_HIDDEN);
  switch (page) {
    case Page::Home:
      lv_obj_clear_flag(homePage_, LV_OBJ_FLAG_HIDDEN);
      break;
    case Page::Diagnostics:
      lv_obj_clear_flag(diagnosticsPage_, LV_OBJ_FLAG_HIDDEN);
      break;
    case Page::Logs:
      lv_obj_clear_flag(logsPage_, LV_OBJ_FLAG_HIDDEN);
      updateLogView();
      break;
  }
}

void UiController::updateLogView() {
  const size_t count = logger_.copy(logSnapshot_, kLogSnapshotCapacity);
  size_t offset = 0;
  logTextBuffer_[0] = '\0';
  for (size_t index = 0; index < count && offset < kLogTextCapacity; ++index) {
    const LogEntry& entry = logSnapshot_[index];
    const int written = snprintf(
        logTextBuffer_ + offset, kLogTextCapacity - offset, "%lus %-5s %s\n",
        static_cast<unsigned long>(entry.timestampMs / 1000),
        levelName(entry.level), entry.message);
    if (written <= 0) {
      break;
    }
    offset += static_cast<size_t>(written) < kLogTextCapacity - offset
                  ? static_cast<size_t>(written)
                  : kLogTextCapacity - offset - 1;
  }
  if (count == 0) {
    snprintf(logTextBuffer_, sizeof(logTextBuffer_),
             "No diagnostic messages recorded.");
  }
  if (logText_ != nullptr) {
    lv_textarea_set_text(logText_, logTextBuffer_);
    lv_textarea_set_cursor_pos(logText_, LV_TEXTAREA_CURSOR_LAST);
  }
}

void UiController::preparePage(lv_obj_t* page) {
  lv_obj_set_size(page, 304, 364);
  lv_obj_set_pos(page, 0, 0);
  styleObject(page, kBackgroundColor, kTextColor);
  lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN);
  lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* UiController::createNavigationButton(const char* text, int16_t x) {
  lv_obj_t* button = lv_btn_create(lv_scr_act());
  lv_obj_set_size(button, 96, 44);
  lv_obj_set_pos(button, x, 426);
  styleObject(button, kPanelColor, kTextColor);
  lv_obj_add_event_cb(button, handleNavigation, LV_EVENT_CLICKED, this);

  lv_obj_t* label = lv_label_create(button);
  lv_label_set_text(label, text);
  lv_obj_center(label);
  return button;
}

const char* UiController::levelName(LogLevel level) const {
  switch (level) {
    case LogLevel::Debug:
      return "DEBUG";
    case LogLevel::Info:
      return "INFO";
    case LogLevel::Warning:
      return "WARN";
    case LogLevel::Error:
      return "ERROR";
  }
  return "?";
}

}  // namespace nova
