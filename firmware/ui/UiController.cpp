#include "UiController.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace nova {
namespace {

constexpr uint32_t kBackgroundColor = 0x0D141A;
constexpr uint32_t kPanelColor = 0x182632;
constexpr uint32_t kAccentColor = 0x39D98A;
constexpr uint32_t kTextColor = 0xE7F1F5;
constexpr uint32_t kMutedTextColor = 0x8BA3AD;
constexpr uint32_t kWarningColor = 0xF2C14E;
constexpr uint32_t kErrorColor = 0xFF6B6B;
constexpr char kSparklineChars[] = " .o*#";
constexpr int16_t kKeyboardHorizontalInset = 8;
constexpr int16_t kKeyboardTop = 216;
constexpr int16_t kKeyboardBottomInset = 10;
constexpr int16_t kKeyboardWidth =
    board::kDisplayWidth - (kKeyboardHorizontalInset * 2);
constexpr int16_t kKeyboardHeight =
    board::kDisplayHeight - kKeyboardTop - kKeyboardBottomInset;

void styleObject(lv_obj_t* object, uint32_t background, uint32_t text) {
  lv_obj_set_style_bg_color(object, lv_color_hex(background), LV_PART_MAIN);
  lv_obj_set_style_text_color(object, lv_color_hex(text), LV_PART_MAIN);
  lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(object, 8, LV_PART_MAIN);
}

void placeKeyboard(lv_obj_t* keyboard) {
  lv_obj_set_align(keyboard, LV_ALIGN_TOP_LEFT);
  lv_obj_set_size(keyboard, kKeyboardWidth, kKeyboardHeight);
  lv_obj_set_pos(keyboard, kKeyboardHorizontalInset, kKeyboardTop);
}

uint32_t stateColor(HubState state) {
  switch (state) {
    case HubState::Live:
      return kAccentColor;
    case HubState::Degraded:
    case HubState::Stale:
    case HubState::Connecting:
    case HubState::Discovering:
    case HubState::Registering:
      return kWarningColor;
    case HubState::UpdateRequired:
    case HubState::Error:
      return kErrorColor;
    default:
      return kMutedTextColor;
  }
}

uint32_t healthColor(HealthGrade grade) {
  switch (grade) {
    case HealthGrade::Normal:
      return kAccentColor;
    case HealthGrade::Warning:
      return kWarningColor;
    case HealthGrade::Critical:
      return kErrorColor;
  }
  return kMutedTextColor;
}

void formatSparkline(const float* values, uint8_t count, char* output,
                     size_t capacity) {
  if (output == nullptr || capacity == 0) {
    return;
  }
  if (values == nullptr || count == 0) {
    snprintf(output, capacity, "--");
    return;
  }
  float minimum = values[0];
  float maximum = values[0];
  for (uint8_t index = 1; index < count; ++index) {
    minimum = std::min(minimum, values[index]);
    maximum = std::max(maximum, values[index]);
  }
  const float range = maximum - minimum;
  const size_t length = std::min(static_cast<size_t>(count), capacity - 1);
  for (size_t index = 0; index < length; ++index) {
    const float normalized = range <= 0.01F
                                 ? 0.5F
                                 : (values[index] - minimum) / range;
    const size_t bucket = static_cast<size_t>(normalized * 4.0F + 0.5F);
    output[index] = kSparklineChars[bucket > 4 ? 4 : bucket];
  }
  output[length] = '\0';
}

void setMetricText(lv_obj_t* label, const char* name, const char* value) {
  if (label != nullptr) {
    lv_label_set_text_fmt(label, "%s\n%s", name, value);
  }
}

void formatMegabytes(uint64_t used, uint64_t total, char* output,
                     size_t capacity) {
  if (total == 0) {
    snprintf(output, capacity, "--");
    return;
  }
  snprintf(output, capacity, "%llu/%llu MB",
           static_cast<unsigned long long>(used / 1048576ULL),
           static_cast<unsigned long long>(total / 1048576ULL));
}

}  // namespace

UiController::UiController(BoardDisplay& display, BoardTouch& touch,
                           Logger& logger, WifiService& wifi, SshService& ssh,
                           HubConnectionService& hub)
    : display_(display),
      touch_(touch),
      logger_(logger),
      wifi_(wifi),
      ssh_(ssh),
      hub_(hub) {}

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
  logger_.write(LogLevel::Info, "Dashboard UI initialized");
  return true;
}

void UiController::update() {
  if (!ready_) {
    return;
  }

  const uint32_t now = millis();
  lv_tick_inc(now - lastTickAt_);
  lastTickAt_ = now;
  if (now - lastRefreshAt_ >= kRefreshPeriodMs) {
    lastRefreshAt_ = now;
    updateHomeView();
    updateServerView();
    updateSetupView();
    updateDiagnosticsView();
    updateWifiView();
    updateSshView();
    updateLogView();
  }
  if (navigationRestorePending_ && !touchPressed_) {
    navigationRestorePending_ = false;
    setNavigationVisible(true);
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
  if (controller == nullptr) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  if (!controller->touch_.read(point) || !point.pressed) {
    controller->touchPressed_ = false;
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  controller->touchPressed_ = true;
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
  } else if (target == controller->navigationServer_) {
    controller->showPage(Page::Server);
  } else if (target == controller->navigationSetup_) {
    controller->showPage(Page::Setup);
  } else if (target == controller->navigationDiagnostics_) {
    controller->showPage(Page::Diagnostics);
  } else if (target == controller->navigationLogs_) {
    controller->showPage(Page::Logs);
  }
}

void UiController::handleHubSetup(lv_event_t* event) {
  auto* controller = static_cast<UiController*>(lv_event_get_user_data(event));
  if (controller == nullptr) {
    return;
  }
  lv_obj_t* target = lv_event_get_target(event);
  const lv_event_code_t code = lv_event_get_code(event);
  if (target == controller->setupWifiButton_ && code == LV_EVENT_CLICKED) {
    controller->showPage(Page::Wifi);
  } else if (target == controller->setupSshButton_ && code == LV_EVENT_CLICKED) {
    controller->showPage(Page::Ssh);
  }
}

void UiController::handleWifiControls(lv_event_t* event) {
  auto* controller = static_cast<UiController*>(lv_event_get_user_data(event));
  if (controller == nullptr) {
    return;
  }
  lv_obj_t* target = lv_event_get_target(event);
  if (target == controller->wifiBackButton_) {
    controller->showPage(Page::Setup);
    return;
  }
  if (target == controller->wifiScanButton_) {
    controller->wifi_.startScan();
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
  } else if (code == LV_EVENT_READY) {
    const WifiNetwork* network =
        controller->wifi_.network(controller->selectedNetworkIndex_);
    if (network != nullptr) {
      controller->wifi_.connect(network->ssid,
                                lv_textarea_get_text(controller->wifiPassword_),
                                true);
    }
    controller->hideWifiPassword();
  }
}

void UiController::handleSshControls(lv_event_t* event) {
  auto* controller = static_cast<UiController*>(lv_event_get_user_data(event));
  if (controller == nullptr) {
    return;
  }
  lv_obj_t* target = lv_event_get_target(event);
  if (target == controller->sshBackButton_) {
    controller->showPage(Page::Setup);
    return;
  }
  if (target != controller->sshActionButton_) {
    return;
  }
  if (controller->ssh_.isEnabled()) {
    controller->ssh_.setEnabled(false);
  } else if (controller->ssh_.hasCredentials()) {
    controller->ssh_.setEnabled(true);
  } else {
    controller->showSshPassword();
  }
  controller->updateSshView();
}

void UiController::handleSshKeyboard(lv_event_t* event) {
  auto* controller = static_cast<UiController*>(lv_event_get_user_data(event));
  if (controller == nullptr) {
    return;
  }
  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_CANCEL) {
    controller->hideSshPassword();
  } else if (code == LV_EVENT_READY) {
    if (controller->ssh_.configure(
            "nova", lv_textarea_get_text(controller->sshPassword_), true)) {
      controller->ssh_.setEnabled(true);
    }
    controller->hideSshPassword();
  }
}

void UiController::buildUi() {
  lv_obj_t* screen = lv_scr_act();
  styleObject(screen, kBackgroundColor, kTextColor);

  lv_obj_t* title = lv_label_create(screen);
  lv_label_set_text(title, "NOVA  /  HOME AI HUB");
  lv_obj_set_pos(title, 12, 8);
  lv_obj_set_style_text_color(title, lv_color_hex(kAccentColor), LV_PART_MAIN);
  lv_obj_t* subtitle = lv_label_create(screen);
  lv_label_set_text(subtitle, "LOCAL TERMINAL  /  ESP32-S3");
  lv_obj_set_pos(subtitle, 12, 27);
  lv_obj_set_style_text_color(subtitle, lv_color_hex(kMutedTextColor), LV_PART_MAIN);

  lv_obj_t* content = lv_obj_create(screen);
  lv_obj_set_size(content, 304, 364);
  lv_obj_set_pos(content, 8, 50);
  styleObject(content, kBackgroundColor, kTextColor);
  lv_obj_set_style_pad_all(content, 0, LV_PART_MAIN);
  lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

  homePage_ = lv_obj_create(content);
  serverPage_ = lv_obj_create(content);
  setupPage_ = lv_obj_create(content);
  diagnosticsPage_ = lv_obj_create(content);
  wifiPage_ = lv_obj_create(content);
  sshPage_ = lv_obj_create(content);
  logsPage_ = lv_obj_create(content);
  preparePage(homePage_);
  preparePage(serverPage_);
  preparePage(setupPage_);
  preparePage(diagnosticsPage_);
  preparePage(wifiPage_);
  preparePage(sshPage_);
  preparePage(logsPage_);

  lv_obj_t* homeTitle = lv_label_create(homePage_);
  lv_label_set_text(homeTitle, "HOME STATUS");
  lv_obj_set_pos(homeTitle, 12, 10);
  lv_obj_set_style_text_color(homeTitle, lv_color_hex(kAccentColor), LV_PART_MAIN);
  homeClock_ = lv_label_create(homePage_);
  lv_label_set_text(homeClock_, "SERVER TIME  --");
  lv_obj_set_pos(homeClock_, 160, 10);
  lv_obj_set_style_text_color(homeClock_, lv_color_hex(kMutedTextColor), LV_PART_MAIN);
  homeState_ = lv_label_create(homePage_);
  lv_label_set_text(homeState_, "HUB  SETUP REQUIRED");
  lv_obj_set_pos(homeState_, 12, 42);
  lv_obj_set_style_text_color(homeState_, lv_color_hex(kMutedTextColor), LV_PART_MAIN);
  homeLatency_ = lv_label_create(homePage_);
  lv_label_set_text(homeLatency_, "Waiting for live server metrics");
  lv_obj_set_pos(homeLatency_, 12, 68);
  lv_obj_set_style_text_color(homeLatency_, lv_color_hex(kMutedTextColor), LV_PART_MAIN);
  homeCpu_ = lv_label_create(homePage_);
  homeMemory_ = lv_label_create(homePage_);
  homeDisk_ = lv_label_create(homePage_);
  homeNetwork_ = lv_label_create(homePage_);
  lv_obj_t* tiles[] = {homeCpu_, homeMemory_, homeDisk_, homeNetwork_};
  const int16_t positions[][2] = {{12, 112}, {158, 112}, {12, 180}, {158, 180}};
  for (size_t index = 0; index < 4; ++index) {
    lv_obj_set_size(tiles[index], 134, 56);
    lv_obj_set_pos(tiles[index], positions[index][0], positions[index][1]);
    styleObject(tiles[index], kPanelColor, kTextColor);
    lv_obj_set_style_pad_left(tiles[index], 10, LV_PART_MAIN);
    lv_obj_set_style_pad_top(tiles[index], 8, LV_PART_MAIN);
  }
  lv_obj_t* homeHint = lv_label_create(homePage_);
  lv_label_set_text(homeHint, "Tap SERVER for detail  /  SETUP for network");
  lv_obj_set_pos(homeHint, 12, 270);
  lv_obj_set_style_text_color(homeHint, lv_color_hex(kMutedTextColor), LV_PART_MAIN);

  lv_obj_t* serverTitle = lv_label_create(serverPage_);
  lv_label_set_text(serverTitle, "SERVER DETAIL");
  lv_obj_set_pos(serverTitle, 12, 10);
  lv_obj_set_style_text_color(serverTitle, lv_color_hex(kAccentColor), LV_PART_MAIN);
  serverStatus_ = lv_label_create(serverPage_);
  lv_obj_set_pos(serverStatus_, 12, 42);
  serverMetrics_ = lv_label_create(serverPage_);
  lv_obj_set_pos(serverMetrics_, 12, 132);
  lv_obj_set_style_text_color(serverMetrics_, lv_color_hex(kTextColor), LV_PART_MAIN);

  lv_obj_t* setupTitle = lv_label_create(setupPage_);
  lv_label_set_text(setupTitle, "TERMINAL SETUP");
  lv_obj_set_pos(setupTitle, 12, 10);
  lv_obj_set_style_text_color(setupTitle, lv_color_hex(kAccentColor), LV_PART_MAIN);
  setupStatus_ = lv_label_create(setupPage_);
  lv_obj_set_pos(setupStatus_, 12, 40);
  lv_obj_t* setupHint = lv_label_create(setupPage_);
  lv_label_set_text(setupHint,
                    "Hub registration and metrics connection\nstart automatically when Wi-Fi is ready.");
  lv_obj_set_pos(setupHint, 12, 92);
  lv_obj_set_style_text_color(setupHint, lv_color_hex(kMutedTextColor), LV_PART_MAIN);
  setupWifiButton_ = lv_btn_create(setupPage_);
  setupSshButton_ = lv_btn_create(setupPage_);
  lv_obj_set_size(setupWifiButton_, 136, 38);
  lv_obj_set_size(setupSshButton_, 136, 38);
  lv_obj_set_pos(setupWifiButton_, 12, 154);
  lv_obj_set_pos(setupSshButton_, 156, 154);
  styleObject(setupWifiButton_, kPanelColor, kTextColor);
  styleObject(setupSshButton_, kPanelColor, kTextColor);
  lv_obj_add_event_cb(setupWifiButton_, handleHubSetup, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(setupSshButton_, handleHubSetup, LV_EVENT_CLICKED, this);
  lv_obj_t* wifiLabel = lv_label_create(setupWifiButton_);
  lv_label_set_text(wifiLabel, "WI-FI");
  lv_obj_center(wifiLabel);
  lv_obj_t* sshLabel = lv_label_create(setupSshButton_);
  lv_label_set_text(sshLabel, "SSH");
  lv_obj_center(sshLabel);
  lv_obj_t* diagnosticsTitle = lv_label_create(diagnosticsPage_);
  lv_label_set_text(diagnosticsTitle, "DIAGNOSTICS");
  lv_obj_set_pos(diagnosticsTitle, 12, 10);
  lv_obj_set_style_text_color(diagnosticsTitle, lv_color_hex(kAccentColor), LV_PART_MAIN);
  diagnosticsStatus_ = lv_label_create(diagnosticsPage_);
  lv_obj_set_pos(diagnosticsStatus_, 12, 42);

  buildWifiPage();
  buildSshPage();
  buildLogPage();

  navigationHome_ = createNavigationButton("HOME", 8);
  navigationServer_ = createNavigationButton("SERVER", 68);
  navigationSetup_ = createNavigationButton("SETUP", 128);
  navigationDiagnostics_ = createNavigationButton("DIAG", 188);
  navigationLogs_ = createNavigationButton("LOG", 248);
  showPage(Page::Home);
}

void UiController::setNavigationVisible(bool visible) {
  lv_obj_t* navigation[] = {navigationHome_, navigationServer_, navigationSetup_,
                            navigationDiagnostics_, navigationLogs_};
  for (lv_obj_t* button : navigation) {
    if (button == nullptr) {
      continue;
    }
    if (visible) {
      lv_obj_clear_flag(button, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

void UiController::restoreNavigationAfterTouchRelease() {
  // Do not expose navigation until this keyboard tap has fully ended.
  navigationRestorePending_ = true;
}

void UiController::showPage(Page page) {
  lv_obj_t* pages[] = {homePage_, serverPage_, setupPage_, diagnosticsPage_,
                       wifiPage_, sshPage_, logsPage_};
  for (lv_obj_t* current : pages) {
    lv_obj_add_flag(current, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_add_flag(wifiKeyboard_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(sshKeyboard_, LV_OBJ_FLAG_HIDDEN);
  navigationRestorePending_ = false;
  setNavigationVisible(true);
  lv_obj_t* selected = homePage_;
  switch (page) {
    case Page::Home:
      selected = homePage_;
      break;
    case Page::Server:
      selected = serverPage_;
      updateServerView();
      break;
    case Page::Setup:
      selected = setupPage_;
      updateSetupView();
      break;
    case Page::Diagnostics:
      selected = diagnosticsPage_;
      break;
    case Page::Wifi:
      selected = wifiPage_;
      updateWifiView();
      break;
    case Page::Ssh:
      selected = sshPage_;
      updateSshView();
      break;
    case Page::Logs:
      selected = logsPage_;
      updateLogView();
      break;
  }
  lv_obj_clear_flag(selected, LV_OBJ_FLAG_HIDDEN);
}

void UiController::buildWifiPage() {
  lv_obj_t* title = lv_label_create(wifiPage_);
  lv_label_set_text(title, "WI-FI SETUP");
  lv_obj_set_pos(title, 12, 10);
  lv_obj_set_style_text_color(title, lv_color_hex(kAccentColor), LV_PART_MAIN);
  wifiBackButton_ = lv_btn_create(wifiPage_);
  lv_obj_set_size(wifiBackButton_, 72, 32);
  lv_obj_set_pos(wifiBackButton_, 220, 8);
  styleObject(wifiBackButton_, kPanelColor, kTextColor);
  lv_obj_add_event_cb(wifiBackButton_, handleWifiControls, LV_EVENT_CLICKED, this);
  lv_obj_t* backLabel = lv_label_create(wifiBackButton_);
  lv_label_set_text(backLabel, "BACK");
  lv_obj_center(backLabel);
  wifiStatus_ = lv_label_create(wifiPage_);
  lv_obj_set_pos(wifiStatus_, 12, 48);
  wifiScanButton_ = lv_btn_create(wifiPage_);
  lv_obj_set_size(wifiScanButton_, 82, 34);
  lv_obj_set_pos(wifiScanButton_, 210, 50);
  styleObject(wifiScanButton_, kPanelColor, kTextColor);
  lv_obj_add_event_cb(wifiScanButton_, handleWifiControls, LV_EVENT_CLICKED, this);
  lv_obj_t* scanLabel = lv_label_create(wifiScanButton_);
  lv_label_set_text(scanLabel, "SCAN");
  lv_obj_center(scanLabel);
  wifiList_ = lv_list_create(wifiPage_);
  lv_obj_set_size(wifiList_, 280, 238);
  lv_obj_set_pos(wifiList_, 12, 92);
  styleObject(wifiList_, kPanelColor, kTextColor);
  wifiPassword_ = lv_textarea_create(wifiPage_);
  lv_obj_set_size(wifiPassword_, 280, 42);
  lv_obj_set_pos(wifiPassword_, 12, 44);
  lv_textarea_set_one_line(wifiPassword_, true);
  lv_textarea_set_password_mode(wifiPassword_, true);
  lv_textarea_set_placeholder_text(wifiPassword_, "Wi-Fi password");
  styleObject(wifiPassword_, kPanelColor, kTextColor);
  lv_obj_add_event_cb(wifiPassword_, handleWifiKeyboard, LV_EVENT_ALL, this);
  wifiKeyboard_ = lv_keyboard_create(lv_scr_act());
  placeKeyboard(wifiKeyboard_);
  lv_keyboard_set_mode(wifiKeyboard_, LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_keyboard_set_textarea(wifiKeyboard_, wifiPassword_);
  lv_obj_add_flag(wifiPassword_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(wifiKeyboard_, LV_OBJ_FLAG_HIDDEN);
}

void UiController::buildSshPage() {
  lv_obj_t* title = lv_label_create(sshPage_);
  lv_label_set_text(title, "SSH DIAGNOSTICS");
  lv_obj_set_pos(title, 12, 10);
  lv_obj_set_style_text_color(title, lv_color_hex(kAccentColor), LV_PART_MAIN);
  sshBackButton_ = lv_btn_create(sshPage_);
  lv_obj_set_size(sshBackButton_, 72, 32);
  lv_obj_set_pos(sshBackButton_, 220, 8);
  styleObject(sshBackButton_, kPanelColor, kTextColor);
  lv_obj_add_event_cb(sshBackButton_, handleSshControls, LV_EVENT_CLICKED, this);
  lv_obj_t* backLabel = lv_label_create(sshBackButton_);
  lv_label_set_text(backLabel, "BACK");
  lv_obj_center(backLabel);
  sshStatus_ = lv_label_create(sshPage_);
  lv_obj_set_pos(sshStatus_, 12, 50);
  sshActionButton_ = lv_btn_create(sshPage_);
  lv_obj_set_size(sshActionButton_, 82, 34);
  lv_obj_set_pos(sshActionButton_, 210, 50);
  styleObject(sshActionButton_, kPanelColor, kTextColor);
  lv_obj_add_event_cb(sshActionButton_, handleSshControls, LV_EVENT_CLICKED, this);
  lv_obj_t* actionLabel = lv_label_create(sshActionButton_);
  lv_label_set_text(actionLabel, "SET UP");
  lv_obj_center(actionLabel);
  sshInstructions_ = lv_label_create(sshPage_);
  lv_label_set_text(sshInstructions_,
                    "LAN-only diagnostics.\nThe server exposes status commands,\nnot an operating-system shell.");
  lv_obj_set_pos(sshInstructions_, 12, 112);
  sshPassword_ = lv_textarea_create(sshPage_);
  lv_obj_set_size(sshPassword_, 280, 42);
  lv_obj_set_pos(sshPassword_, 12, 44);
  lv_textarea_set_one_line(sshPassword_, true);
  lv_textarea_set_password_mode(sshPassword_, true);
  lv_textarea_set_placeholder_text(sshPassword_, "SSH password");
  styleObject(sshPassword_, kPanelColor, kTextColor);
  lv_obj_add_event_cb(sshPassword_, handleSshKeyboard, LV_EVENT_ALL, this);
  sshKeyboard_ = lv_keyboard_create(lv_scr_act());
  placeKeyboard(sshKeyboard_);
  lv_keyboard_set_mode(sshKeyboard_, LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_keyboard_set_textarea(sshKeyboard_, sshPassword_);
  lv_obj_add_flag(sshPassword_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(sshKeyboard_, LV_OBJ_FLAG_HIDDEN);
}

void UiController::buildLogPage() {
  lv_obj_t* title = lv_label_create(logsPage_);
  lv_label_set_text(title, "LIVE DEBUG LOG");
  lv_obj_set_pos(title, 12, 10);
  lv_obj_set_style_text_color(title, lv_color_hex(kAccentColor), LV_PART_MAIN);
  logText_ = lv_textarea_create(logsPage_);
  lv_obj_set_size(logText_, 280, 300);
  lv_obj_set_pos(logText_, 12, 48);
  lv_textarea_set_text(logText_, "Waiting for diagnostic messages...");
  lv_textarea_set_cursor_click_pos(logText_, false);
  styleObject(logText_, kPanelColor, kTextColor);
}

void UiController::showWifiPassword(size_t networkIndex) {
  if (wifi_.network(networkIndex) == nullptr) {
    return;
  }
  selectedNetworkIndex_ = networkIndex;
  lv_textarea_set_text(wifiPassword_, "");
  lv_obj_add_flag(wifiList_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(wifiScanButton_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(wifiPassword_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(wifiKeyboard_, LV_OBJ_FLAG_HIDDEN);
  lv_keyboard_set_textarea(wifiKeyboard_, wifiPassword_);
  lv_label_set_text_fmt(wifiStatus_, "Password for: %s",
                        wifi_.network(networkIndex)->ssid);
  setNavigationVisible(false);
  navigationRestorePending_ = false;
}

void UiController::hideWifiPassword() {
  selectedNetworkIndex_ = WifiService::kMaxNetworks;
  lv_obj_add_flag(wifiPassword_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(wifiKeyboard_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(wifiList_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(wifiScanButton_, LV_OBJ_FLAG_HIDDEN);
  restoreNavigationAfterTouchRelease();
  updateWifiView();
}

void UiController::showSshPassword() {
  lv_textarea_set_text(sshPassword_, "");
  lv_obj_add_flag(sshInstructions_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(sshActionButton_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(sshPassword_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(sshKeyboard_, LV_OBJ_FLAG_HIDDEN);
  lv_keyboard_set_textarea(sshKeyboard_, sshPassword_);
  lv_label_set_text(sshStatus_, "Set password for SSH user: nova");
  setNavigationVisible(false);
  navigationRestorePending_ = false;
}

void UiController::hideSshPassword() {
  lv_obj_add_flag(sshPassword_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(sshKeyboard_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(sshInstructions_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(sshActionButton_, LV_OBJ_FLAG_HIDDEN);
  restoreNavigationAfterTouchRelease();
  updateSshView();
}

void UiController::updateHomeView() {
  const HubHealthSnapshot& health = hub_.health();
  lv_label_set_text_fmt(homeState_, "HUB  %s", hub_.stateName());
  const uint32_t statusColor = health.valid && hub_.state() == HubState::Live
                                   ? healthColor(health.healthGrade)
                                   : stateColor(hub_.state());
  lv_obj_set_style_text_color(homeState_, lv_color_hex(statusColor), LV_PART_MAIN);
  if (health.valid) {
    lv_label_set_text_fmt(homeState_, "HUB %s / %s", hub_.stateName(),
                          healthGradeName(health.healthGrade));
    lv_label_set_text_fmt(homeClock_, "SERVER %s", health.serverTime);
    lv_label_set_text_fmt(homeLatency_, "Latency %lu ms  /  %s",
                          static_cast<unsigned long>(health.latencyMs),
                          healthGradeName(health.healthGrade));
    char memory[32] = {};
    char disk[32] = {};
    char network[32] = {};
    formatMegabytes(health.memoryUsedBytes, health.memoryTotalBytes, memory,
                    sizeof(memory));
    formatMegabytes(health.diskUsedBytes, health.diskTotalBytes, disk,
                    sizeof(disk));
    snprintf(network, sizeof(network), "%.0f/%.0f KB/s",
             health.networkRxBytesPerSecond / 1024.0F,
             health.networkTxBytesPerSecond / 1024.0F);
    char cpu[16] = {};
    snprintf(cpu, sizeof(cpu), "%.1f%%", health.cpuPercent);
    setMetricText(homeCpu_, "CPU", cpu);
    setMetricText(homeMemory_, "RAM", memory);
    setMetricText(homeDisk_, "DISK", disk);
    setMetricText(homeNetwork_, "NET RX/TX", network);
  } else {
    lv_label_set_text(homeClock_, "SERVER TIME  --");
    lv_label_set_text(homeLatency_, "Waiting for live server metrics");
    setMetricText(homeCpu_, "CPU", "--");
    setMetricText(homeMemory_, "RAM", "--");
    setMetricText(homeDisk_, "DISK", "--");
    setMetricText(homeNetwork_, "NET RX/TX", "--");
  }
}

void UiController::updateServerView() {
  const HubHealthSnapshot& health = hub_.health();
  lv_label_set_text_fmt(serverStatus_, "State: %s\nHost: %s\nDevice: %s\nCA: %s",
                        hub_.stateName(), hub_.host(),
                        hub_.isRegistered() ? hub_.deviceId() : "NOT REGISTERED",
                        hub_.hasTrustAnchor() ? "LOADED" : "MISSING");
  if (!health.valid) {
    lv_label_set_text(serverMetrics_, "Waiting for a live health snapshot.");
    return;
  }
  char cpuTrend[HubHealthTrends::kMaxPoints + 1] = {};
  char memoryTrend[HubHealthTrends::kMaxPoints + 1] = {};
  char diskTrend[HubHealthTrends::kMaxPoints + 1] = {};
  formatSparkline(health.trends.cpuPercent, health.trends.cpuPointCount, cpuTrend,
                  sizeof(cpuTrend));
  formatSparkline(health.trends.memoryUsedPercent,
                  health.trends.memoryPointCount, memoryTrend,
                  sizeof(memoryTrend));
  formatSparkline(health.trends.diskUsedPercent, health.trends.diskPointCount,
                  diskTrend, sizeof(diskTrend));
  char alertText[96] = {};
  if (health.activeAlertCount > 0) {
    const HubActiveAlert& alert = health.activeAlerts[0];
    snprintf(alertText, sizeof(alertText), "Alert: %s %s %.1f",
             alert.metric, healthGradeName(alert.state), alert.value);
  } else {
    snprintf(alertText, sizeof(alertText), "Alerts: none");
  }
  char metrics[768] = {};
  snprintf(
      metrics, sizeof(metrics),
      "Version: %s\nHealth: %s\nUptime: %.0f s\nCPU: %.1f%%\nRAM: %llu / %llu MB\n"
      "Disk: %llu / %llu MB\nNetwork: %s\nRX/TX: %.0f / %.0f KB/s\n%s\n%s\n"
      "Trends CPU:%s RAM:%s DISK:%s\nServices: hub=%s metrics=%s",
      health.serverVersion, healthGradeName(health.healthGrade),
      health.uptimeSeconds, health.cpuPercent,
      static_cast<unsigned long long>(health.memoryUsedBytes / 1048576ULL),
      static_cast<unsigned long long>(health.memoryTotalBytes / 1048576ULL),
      static_cast<unsigned long long>(health.diskUsedBytes / 1048576ULL),
      static_cast<unsigned long long>(health.diskTotalBytes / 1048576ULL),
      health.networkInterface, health.networkRxBytesPerSecond / 1024.0F,
      health.networkTxBytesPerSecond / 1024.0F,
      health.error[0] == '\0' ? "Metrics current" : health.error,
      alertText,
      cpuTrend, memoryTrend, diskTrend,
      health.hubApiStatus, health.metricsStatus);
  lv_label_set_text(serverMetrics_, metrics);
}

void UiController::updateSetupView() {
  const char* error = hub_.health().error;
  if (error[0] == '\0') {
    lv_label_set_text_fmt(setupStatus_, "Hub: %s\nWi-Fi: %s", hub_.stateName(),
                          wifi_.stateName());
    return;
  }
  lv_label_set_text_fmt(setupStatus_, "Hub: %s\nWi-Fi: %s\n%s", hub_.stateName(),
                        wifi_.stateName(), error);
}

void UiController::updateDiagnosticsView() {
  lv_label_set_text_fmt(
      diagnosticsStatus_,
      "DISPLAY  %s\nTOUCH    %s\nWI-FI    %s\nHUB      %s\n"
      "HEALTH   %s\nCA       %s\nSSH      %s\nPSRAM    %s\n\n"
      "Production SSH/OTA is disabled by the secure rollout policy.",
      display_.isReady() ? "READY" : "FAILED", touch_.isReady() ? "READY" : "FAILED",
      wifi_.stateName(), hub_.stateName(),
      healthGradeName(hub_.health().healthGrade),
      hub_.hasTrustAnchor() ? "LOADED" : "MISSING",
      ssh_.isEnabled() ? "ENABLED" : "DISABLED", psramFound() ? "READY" : "MISSING");
}

void UiController::updateWifiView() {
  const String ip = wifi_.ipAddress().toString();
  lv_label_set_text_fmt(wifiStatus_, "State: %s\nSSID: %s\nIP: %s  RSSI: %ld",
                        wifi_.stateName(), wifi_.configuredSsid(), ip.c_str(),
                        static_cast<long>(wifi_.rssi()));
  lv_obj_t* scanLabel = lv_obj_get_child(wifiScanButton_, 0);
  if (scanLabel != nullptr) {
    lv_label_set_text(scanLabel, wifi_.scanInProgress() ? "WAIT" : "SCAN");
  }
  if (wifi_.networkCount() == renderedNetworkCount_) {
    return;
  }
  while (lv_obj_get_child(wifiList_, 0) != nullptr) {
    lv_obj_del(lv_obj_get_child(wifiList_, 0));
  }
  for (lv_obj_t*& button : wifiNetworkButtons_) {
    button = nullptr;
  }
  renderedNetworkCount_ = wifi_.networkCount();
  for (size_t index = 0; index < renderedNetworkCount_; ++index) {
    const WifiNetwork* network = wifi_.network(index);
    if (network == nullptr) {
      continue;
    }
    char label[64] = {};
    snprintf(label, sizeof(label), "%s  %ld dBm %s", network->ssid,
             static_cast<long>(network->rssi), network->encrypted ? "LOCK" : "OPEN");
    wifiNetworkButtons_[index] = lv_list_add_btn(wifiList_, nullptr, label);
    lv_obj_add_event_cb(wifiNetworkButtons_[index], handleWifiControls,
                        LV_EVENT_CLICKED, this);
  }
}

void UiController::updateSshView() {
  const String ip = wifi_.ipAddress().toString();
  if (!ssh_.hasCredentials()) {
    lv_label_set_text(sshStatus_, "Status: NOT CONFIGURED\nUser: nova");
  } else if (ssh_.isEnabled()) {
    lv_label_set_text_fmt(sshStatus_, "Status: %s\nssh %s@%s",
                          ssh_.isReady() ? "READY" : "STARTING", ssh_.username(),
                          ip.c_str());
  } else {
    lv_label_set_text_fmt(sshStatus_, "Status: DISABLED\nUser: %s", ssh_.username());
  }
  lv_obj_t* actionLabel = lv_obj_get_child(sshActionButton_, 0);
  if (actionLabel != nullptr) {
    lv_label_set_text(actionLabel, ssh_.isEnabled() ? "DISABLE"
                                                     : ssh_.hasCredentials() ? "ENABLE" : "SET UP");
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
        static_cast<unsigned long>(entry.timestampMs / 1000), levelName(entry.level),
        entry.message);
    if (written <= 0) {
      break;
    }
    offset += static_cast<size_t>(written) < kLogTextCapacity - offset
                  ? static_cast<size_t>(written)
                  : kLogTextCapacity - offset - 1;
  }
  if (count == 0) {
    snprintf(logTextBuffer_, sizeof(logTextBuffer_), "No diagnostic messages recorded.");
  }
  lv_textarea_set_text(logText_, logTextBuffer_);
  lv_textarea_set_cursor_pos(logText_, LV_TEXTAREA_CURSOR_LAST);
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
  lv_obj_set_size(button, 56, 44);
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
