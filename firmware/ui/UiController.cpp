#include "UiController.h"

#include <Arduino.h>
#include <freertos/task.h>

#include <cmath>
#include <cstdio>

namespace nova {
namespace {

constexpr uint32_t kBackgroundColor = 0x111A18;
constexpr uint32_t kPanelColor = 0x192622;
constexpr uint32_t kLineColor = 0x2C3C36;
constexpr uint32_t kTextColor = 0xEEF5EF;
constexpr uint32_t kMutedTextColor = 0x91A49A;
constexpr uint32_t kFaintTextColor = 0x667B70;
constexpr uint32_t kMintColor = 0xACE8C8;
constexpr uint32_t kMintPressedColor = 0xC2F1D7;
constexpr uint32_t kGoldColor = 0xE4B56D;
constexpr uint32_t kActionTextColor = 0x102219;
constexpr char kFirmwareVersion[] = "v0.1.0";

constexpr int16_t kMargin = 14;
constexpr int16_t kContentWidth = 292;
constexpr int16_t kTelemetryY = 46;
constexpr int16_t kTelemetryHeight = 106;
constexpr int16_t kStatsY = 160;
constexpr int16_t kStatsHeight = 222;
constexpr int16_t kActionY = 410;
constexpr int16_t kActionHeight = 52;
constexpr int16_t kStatRowHeight = 20;

constexpr const char* kStatNames[] = {
    "Wi-Fi",          "IP address", "CPU utilisation", "GPU utilisation",
    "Uptime",         "Memory free", "Temperature",     "Firmware",
};

void styleSurface(lv_obj_t* object, uint32_t background, uint32_t text,
                  uint32_t border, uint16_t radius) {
  lv_obj_set_style_bg_color(object, lv_color_hex(background), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_text_color(object, lv_color_hex(text), LV_PART_MAIN);
  lv_obj_set_style_border_color(object, lv_color_hex(border), LV_PART_MAIN);
  lv_obj_set_style_border_width(object, border == kBackgroundColor ? 0 : 1,
                                LV_PART_MAIN);
  lv_obj_set_style_radius(object, radius, LV_PART_MAIN);
  lv_obj_set_style_pad_all(object, 0, LV_PART_MAIN);
}

void styleScreen(lv_obj_t* object) {
  styleSurface(object, kBackgroundColor, kTextColor, kBackgroundColor, 0);
  lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* createLabel(lv_obj_t* parent, const char* text, int16_t x, int16_t y,
                      int16_t width, int16_t height, uint32_t color) {
  lv_obj_t* label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_size(label, width, height);
  lv_obj_set_pos(label, x, y);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
  return label;
}

lv_obj_t* createPanel(lv_obj_t* parent, int16_t x, int16_t y, int16_t width,
                      int16_t height) {
  lv_obj_t* panel = lv_obj_create(parent);
  lv_obj_set_size(panel, width, height);
  lv_obj_set_pos(panel, x, y);
  styleSurface(panel, kPanelColor, kTextColor, kLineColor, 14);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  return panel;
}

lv_obj_t* createButton(lv_obj_t* parent, const char* text, int16_t x,
                       int16_t y, int16_t width, int16_t height,
                       uint32_t background, uint32_t foreground) {
  lv_obj_t* button = lv_btn_create(parent);
  lv_obj_set_size(button, width, height);
  lv_obj_set_pos(button, x, y);
  styleSurface(button, background, foreground, background, 11);
  if (background == kMintColor) {
    lv_obj_set_style_bg_color(button, lv_color_hex(kMintPressedColor),
                              LV_PART_MAIN | LV_STATE_PRESSED);
  }
  lv_obj_t* label = createLabel(button, text, 8, 0, width - 16, height,
                                foreground);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  return button;
}

void setMetricLabel(lv_obj_t* label, const char* name, int16_t value) {
  if (label == nullptr) {
    return;
  }
  char text[24] = {};
  if (value < 0) {
    snprintf(text, sizeof(text), "%s --", name);
  } else {
    snprintf(text, sizeof(text), "%s %d%%", name, value);
  }
  lv_label_set_text(label, text);
}

void formatUptime(uint32_t uptimeSeconds, char* output, size_t capacity) {
  const uint32_t hours = uptimeSeconds / 3600;
  const uint32_t minutes = (uptimeSeconds % 3600) / 60;
  const uint32_t seconds = uptimeSeconds % 60;
  snprintf(output, capacity, "%02lu:%02lu:%02lu",
           static_cast<unsigned long>(hours),
           static_cast<unsigned long>(minutes),
           static_cast<unsigned long>(seconds));
}

}  // namespace

UiController::UiController(BoardDisplay& display, BoardTouch& touch,
                           Logger& logger, WifiService& wifi, SshService& ssh)
    : display_(display),
      touch_(touch),
      logger_(logger),
      wifi_(wifi),
      ssh_(ssh) {}

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
  lastRefreshAt_ = millis();
  lastChartAt_ = millis();
  ready_ = true;
  logger_.write(LogLevel::Info, "Device status UI initialized");
  return true;
}

void UiController::update() {
  if (!ready_) {
    return;
  }

  const uint32_t now = millis();
  lv_tick_inc(now - lastRefreshAt_);
  lastRefreshAt_ = now;
  if (now - lastCpuSampleAt_ >= kRefreshPeriodMs) {
    refreshDashboard();
    refreshWifiSheet();
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

void UiController::handleDashboardControls(lv_event_t* event) {
  auto* controller = static_cast<UiController*>(lv_event_get_user_data(event));
  if (controller != nullptr &&
      lv_event_get_target(event) == controller->wifiActionButton_) {
    controller->showWifiSheet();
  }
}

void UiController::handleWifiControls(lv_event_t* event) {
  auto* controller = static_cast<UiController*>(lv_event_get_user_data(event));
  if (controller == nullptr) {
    return;
  }

  lv_obj_t* target = lv_event_get_target(event);
  if (target == controller->wifiCloseButton_) {
    controller->hideWifiSheet();
    return;
  }
  if (target == controller->wifiScanButton_) {
    controller->wifi_.startScan();
    controller->wifiListDirty_ = true;
    return;
  }
  if (target == controller->wifiBackButton_) {
    controller->hideWifiPassword();
    return;
  }
  for (size_t index = 0; index < WifiService::kMaxNetworks; ++index) {
    if (target == controller->wifiNetworkButtons_[index]) {
      controller->showWifiPassword(index);
      return;
    }
  }
}

void UiController::handleWifiKeyboard(lv_event_t* event) {
  auto* controller = static_cast<UiController*>(lv_event_get_user_data(event));
  if (controller == nullptr) {
    return;
  }

  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_CANCEL) {
    controller->hideWifiPassword();
    return;
  }
  if (code != LV_EVENT_READY) {
    return;
  }

  const WifiNetwork* network =
      controller->wifi_.network(controller->selectedNetworkIndex_);
  if (network != nullptr) {
    controller->wifi_.connect(network->ssid,
                               lv_textarea_get_text(controller->wifiPassword_),
                               true);
  }
  controller->hideWifiSheet();
}

void UiController::buildUi() {
  styleScreen(lv_scr_act());
  buildDashboard();
  buildWifiSheet();
}

void UiController::buildDashboard() {
  lv_obj_t* screen = lv_scr_act();
  lv_obj_t* mark = lv_obj_create(screen);
  lv_obj_set_size(mark, 12, 12);
  lv_obj_set_pos(mark, 16, 15);
  styleSurface(mark, kBackgroundColor, kMintColor, kMintColor, 4);
  lv_obj_set_style_border_width(mark, 2, LV_PART_MAIN);

  lv_obj_t* wordmark = createLabel(screen, "NOVA", 36, 8, 100, 26, kTextColor);
  lv_obj_set_style_text_letter_space(wordmark, 2, LV_PART_MAIN);
  createLabel(screen, "DEVICE STATUS", 194, 13, 110, 20, kFaintTextColor);

  lv_obj_t* telemetry =
      createPanel(screen, kMargin, kTelemetryY, kContentWidth, kTelemetryHeight);
  createLabel(telemetry, "Utilisation", 12, 8, 100, 20, kTextColor);
  createLabel(telemetry, "10 MIN", 246, 10, 34, 16, kFaintTextColor);
  telemetryChart_ = lv_chart_create(telemetry);
  lv_obj_set_size(telemetryChart_, 268, 54);
  lv_obj_set_pos(telemetryChart_, 12, 30);
  styleSurface(telemetryChart_, kPanelColor, kTextColor, kPanelColor, 0);
  lv_chart_set_type(telemetryChart_, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(telemetryChart_, kChartPointCount);
  lv_chart_set_range(telemetryChart_, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
  lv_chart_set_div_line_count(telemetryChart_, 2, 3);
  lv_chart_set_update_mode(telemetryChart_, LV_CHART_UPDATE_MODE_SHIFT);
  lv_obj_set_style_line_color(telemetryChart_, lv_color_hex(kLineColor),
                              LV_PART_MAIN);
  lv_obj_set_style_line_width(telemetryChart_, 1, LV_PART_MAIN);
  cpuSeries_ = lv_chart_add_series(telemetryChart_, lv_color_hex(kMintColor),
                                   LV_CHART_AXIS_PRIMARY_Y);
  gpuSeries_ = lv_chart_add_series(telemetryChart_, lv_color_hex(kGoldColor),
                                   LV_CHART_AXIS_PRIMARY_Y);
  lv_chart_set_series_color(telemetryChart_, cpuSeries_,
                            lv_color_hex(kMintColor));
  lv_chart_set_series_color(telemetryChart_, gpuSeries_,
                            lv_color_hex(kGoldColor));
  for (size_t index = 0; index < kChartPointCount; ++index) {
    cpuHistory_[index] = LV_CHART_POINT_NONE;
    gpuHistory_[index] = LV_CHART_POINT_NONE;
    lv_chart_set_value_by_id(telemetryChart_, cpuSeries_, index,
                             LV_CHART_POINT_NONE);
    lv_chart_set_value_by_id(telemetryChart_, gpuSeries_, index,
                             LV_CHART_POINT_NONE);
  }
  cpuLegend_ = createLabel(telemetry, "CPU --", 14, 86, 68, 16, kMintColor);
  gpuLegend_ = createLabel(telemetry, "GPU --", 84, 86, 68, 16, kGoldColor);

  lv_obj_t* stats = createPanel(screen, kMargin, kStatsY, kContentWidth,
                                kStatsHeight);
  createLabel(stats, "Device stats", 12, 8, 150, 20, kTextColor);
  for (size_t index = 0; index < kStatCount; ++index) {
    const int16_t y = 34 + static_cast<int16_t>(index) * kStatRowHeight;
    if (index > 0) {
      lv_obj_t* divider = lv_obj_create(stats);
      lv_obj_set_size(divider, 268, 1);
      lv_obj_set_pos(divider, 12, y - 4);
      styleSurface(divider, kLineColor, kLineColor, kLineColor, 0);
    }
    createLabel(stats, kStatNames[index], 12, y, 150, 18, kMutedTextColor);
    statValues_[index] = createLabel(stats, "--", 164, y, 116, 18, kTextColor);
    lv_obj_set_style_text_align(statValues_[index], LV_TEXT_ALIGN_RIGHT,
                                LV_PART_MAIN);
  }

  wifiActionButton_ = createButton(screen, "Connect to Wi-Fi", kMargin,
                                   kActionY, kContentWidth, kActionHeight,
                                   kMintColor, kActionTextColor);
  lv_obj_add_event_cb(wifiActionButton_, handleDashboardControls,
                      LV_EVENT_CLICKED, this);
}

void UiController::buildWifiSheet() {
  lv_obj_t* screen = lv_scr_act();
  wifiSheet_ = lv_obj_create(screen);
  lv_obj_set_size(wifiSheet_, board::kDisplayWidth, board::kDisplayHeight);
  lv_obj_set_pos(wifiSheet_, 0, 0);
  styleScreen(wifiSheet_);

  createLabel(wifiSheet_, "NETWORK SETUP", 16, 14, 170, 18,
              kFaintTextColor);
  createLabel(wifiSheet_, "Connect to Wi-Fi", 16, 34, 210, 26, kTextColor);
  wifiCloseButton_ = createButton(wifiSheet_, "X", 264, 12, 40, 36,
                                  kPanelColor, kTextColor);
  lv_obj_add_event_cb(wifiCloseButton_, handleWifiControls, LV_EVENT_CLICKED,
                      this);

  wifiStatus_ = createLabel(wifiSheet_, "Tap scan to find nearby networks.",
                            16, 76, 288, 30, kMutedTextColor);
  wifiScanButton_ = createButton(wifiSheet_, "SCAN", 16, 112, 96, 38,
                                 kPanelColor, kTextColor);
  lv_obj_add_event_cb(wifiScanButton_, handleWifiControls, LV_EVENT_CLICKED,
                      this);
  wifiList_ = createPanel(wifiSheet_, 16, 158, 288, 244);

  wifiBackButton_ = createButton(wifiSheet_, "BACK", 16, 158, 92, 38,
                                kPanelColor, kTextColor);
  lv_obj_add_event_cb(wifiBackButton_, handleWifiControls, LV_EVENT_CLICKED,
                      this);
  wifiPassword_ = lv_textarea_create(wifiSheet_);
  lv_obj_set_size(wifiPassword_, 188, 38);
  lv_obj_set_pos(wifiPassword_, 116, 158);
  lv_textarea_set_one_line(wifiPassword_, true);
  lv_textarea_set_password_mode(wifiPassword_, true);
  lv_textarea_set_placeholder_text(wifiPassword_, "Password");
  styleSurface(wifiPassword_, kPanelColor, kTextColor, kLineColor, 10);
  lv_obj_add_event_cb(wifiPassword_, handleWifiKeyboard, LV_EVENT_ALL, this);

  wifiKeyboard_ = lv_keyboard_create(wifiSheet_);
  lv_obj_set_size(wifiKeyboard_, board::kDisplayWidth, 270);
  lv_obj_set_pos(wifiKeyboard_, 0, 204);
  lv_keyboard_set_mode(wifiKeyboard_, LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_keyboard_set_textarea(wifiKeyboard_, wifiPassword_);
  styleSurface(wifiKeyboard_, kPanelColor, kTextColor, kLineColor, 0);

  lv_obj_add_flag(wifiSheet_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(wifiBackButton_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(wifiPassword_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(wifiKeyboard_, LV_OBJ_FLAG_HIDDEN);
}

void UiController::refreshDashboard() {
  sampleCpuUsage();
  refreshStats();
  refreshChart();
}

void UiController::refreshStats() {
  char value[48] = {};
  const bool connected = wifi_.state() == WifiState::Connected;
  setStatValue(0, connected ? wifi_.configuredSsid() : wifi_.stateName());
  setStatValue(1, connected ? wifi_.ipAddress().toString().c_str() : "--");
  if (cpuUsagePercent_ >= 0) {
    snprintf(value, sizeof(value), "%d%%", cpuUsagePercent_);
    setStatValue(2, value);
  } else {
    setStatValue(2, "--");
  }
  setStatValue(3, "--");
  formatUptime(millis() / 1000, value, sizeof(value));
  setStatValue(4, value);
  snprintf(value, sizeof(value), "%.1f MB",
           static_cast<double>(ESP.getFreeHeap()) / 1024.0);
  setStatValue(5, value);
  const float temperature = temperatureRead();
  if (std::isfinite(temperature)) {
    snprintf(value, sizeof(value), "%.1f C", static_cast<double>(temperature));
    setStatValue(6, value);
  } else {
    setStatValue(6, "--");
  }
  setStatValue(7, kFirmwareVersion);
}

void UiController::refreshChart() {
  if (cpuUsagePercent_ >= 0 && !chartSeeded_) {
    for (size_t index = 0; index < kChartPointCount; ++index) {
      cpuHistory_[index] = cpuUsagePercent_;
    }
    chartSeeded_ = true;
  }

  setMetricLabel(cpuLegend_, "CPU", cpuUsagePercent_);
  setMetricLabel(gpuLegend_, "GPU", kMetricUnavailable);
  if (!chartSeeded_ || millis() - lastChartAt_ < kChartPeriodMs) {
    return;
  }

  lastChartAt_ = millis();
  for (size_t index = 1; index < kChartPointCount; ++index) {
    cpuHistory_[index - 1] = cpuHistory_[index];
    gpuHistory_[index - 1] = gpuHistory_[index];
  }
  cpuHistory_[kChartPointCount - 1] = cpuUsagePercent_;
  gpuHistory_[kChartPointCount - 1] = LV_CHART_POINT_NONE;
  for (size_t index = 0; index < kChartPointCount; ++index) {
    lv_chart_set_value_by_id(telemetryChart_, cpuSeries_, index,
                             cpuHistory_[index]);
    lv_chart_set_value_by_id(telemetryChart_, gpuSeries_, index,
                             gpuHistory_[index]);
  }
  lv_chart_refresh(telemetryChart_);
}

void UiController::refreshWifiSheet() {
  if (view_ != View::WifiSetup) {
    return;
  }

  const bool scanning = wifi_.scanInProgress();
  if (wifiListDirty_ || scanning != renderedScanInProgress_ ||
      wifi_.networkCount() != renderedNetworkCount_) {
    renderWifiNetworks();
    wifiListDirty_ = false;
    renderedScanInProgress_ = scanning;
    renderedNetworkCount_ = wifi_.networkCount();
  }
}

void UiController::sampleCpuUsage() {
#if configGENERATE_RUN_TIME_STATS == 1
  uint32_t idleRuntime[kCpuCoreCount] = {};
  for (uint8_t core = 0; core < kCpuCoreCount; ++core) {
    TaskHandle_t idleTask = xTaskGetIdleTaskHandleForCPU(core);
    if (idleTask == nullptr) {
      cpuUsagePercent_ = kMetricUnavailable;
      return;
    }
    TaskStatus_t status = {};
    vTaskGetInfo(idleTask, &status, pdFALSE, eInvalid);
    idleRuntime[core] = status.ulRunTimeCounter;
  }

  const uint32_t now = millis();
  if (!cpuSampleReady_) {
    for (uint8_t core = 0; core < kCpuCoreCount; ++core) {
      lastIdleRuntime_[core] = idleRuntime[core];
    }
    lastCpuSampleAt_ = now;
    cpuSampleReady_ = true;
    return;
  }

  const uint32_t elapsedMs = now - lastCpuSampleAt_;
  uint32_t idleDelta = 0;
  for (uint8_t core = 0; core < kCpuCoreCount; ++core) {
    idleDelta += idleRuntime[core] - lastIdleRuntime_[core];
    lastIdleRuntime_[core] = idleRuntime[core];
  }
  lastCpuSampleAt_ = now;
  const uint64_t capacity = static_cast<uint64_t>(elapsedMs) * 1000ULL *
                            getCpuFrequencyMhz() * kCpuCoreCount;
  if (capacity == 0) {
    return;
  }
  const uint64_t busyRuntime = idleDelta >= capacity ? 0 : capacity - idleDelta;
  cpuUsagePercent_ = static_cast<int16_t>((busyRuntime * 100) / capacity);
  if (cpuUsagePercent_ > 100) {
    cpuUsagePercent_ = 100;
  }
#else
  cpuUsagePercent_ = kMetricUnavailable;
  lastCpuSampleAt_ = millis();
#endif
}

void UiController::setStatValue(size_t index, const char* value) {
  if (index >= kStatCount || statValues_[index] == nullptr) {
    return;
  }
  lv_label_set_text(statValues_[index], value == nullptr ? "--" : value);
}

void UiController::showWifiSheet() {
  view_ = View::WifiSetup;
  selectedNetworkIndex_ = WifiService::kMaxNetworks;
  lv_obj_clear_flag(wifiSheet_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(wifiList_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(wifiScanButton_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(wifiBackButton_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(wifiPassword_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(wifiKeyboard_, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(wifiStatus_, "Scanning for nearby networks...");
  wifiListDirty_ = true;
  wifi_.startScan();
  refreshWifiSheet();
}

void UiController::hideWifiSheet() {
  view_ = View::Dashboard;
  selectedNetworkIndex_ = WifiService::kMaxNetworks;
  lv_obj_add_flag(wifiSheet_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(wifiBackButton_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(wifiPassword_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(wifiKeyboard_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(wifiList_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(wifiScanButton_, LV_OBJ_FLAG_HIDDEN);
}

void UiController::showWifiPassword(size_t networkIndex) {
  const WifiNetwork* network = wifi_.network(networkIndex);
  if (network == nullptr) {
    return;
  }
  if (!network->encrypted) {
    wifi_.connect(network->ssid, "", true);
    hideWifiSheet();
    return;
  }

  selectedNetworkIndex_ = networkIndex;
  lv_textarea_set_text(wifiPassword_, "");
  lv_label_set_text_fmt(wifiStatus_, "Password for %s", network->ssid);
  lv_obj_add_flag(wifiList_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(wifiScanButton_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(wifiBackButton_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(wifiPassword_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(wifiKeyboard_, LV_OBJ_FLAG_HIDDEN);
  lv_keyboard_set_textarea(wifiKeyboard_, wifiPassword_);
}

void UiController::hideWifiPassword() {
  selectedNetworkIndex_ = WifiService::kMaxNetworks;
  lv_obj_add_flag(wifiBackButton_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(wifiPassword_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(wifiKeyboard_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(wifiList_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(wifiScanButton_, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(wifiStatus_, "Choose a nearby network.");
}

void UiController::renderWifiNetworks() {
  while (lv_obj_get_child(wifiList_, 0) != nullptr) {
    lv_obj_del(lv_obj_get_child(wifiList_, 0));
  }
  for (lv_obj_t*& button : wifiNetworkButtons_) {
    button = nullptr;
  }

  const size_t count = wifi_.networkCount();
  if (wifi_.scanInProgress()) {
    lv_label_set_text(wifiStatus_, "Scanning for nearby networks...");
  } else if (count == 0) {
    lv_label_set_text(wifiStatus_, "No networks found. Tap scan to retry.");
  } else {
    lv_label_set_text(wifiStatus_, "Choose a nearby network.");
  }

  for (size_t index = 0; index < count; ++index) {
    const WifiNetwork* network = wifi_.network(index);
    if (network == nullptr) {
      continue;
    }
    char label[80] = {};
    snprintf(label, sizeof(label), "%s  %s", network->ssid,
             network->encrypted ? "LOCK" : "OPEN");
    wifiNetworkButtons_[index] = lv_list_add_btn(wifiList_, nullptr, label);
    lv_obj_set_height(wifiNetworkButtons_[index], 44);
    lv_obj_set_style_bg_color(wifiNetworkButtons_[index],
                              lv_color_hex(kPanelColor), LV_PART_MAIN);
    lv_obj_set_style_text_color(wifiNetworkButtons_[index],
                                lv_color_hex(kTextColor), LV_PART_MAIN);
    lv_obj_add_event_cb(wifiNetworkButtons_[index], handleWifiControls,
                        LV_EVENT_CLICKED, this);
  }
}

}  // namespace nova
