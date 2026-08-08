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
    updateHomeView();
    updateDiagnosticsView();
    updateWifiView();
    updateSshView();
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
  } else if (target == controller->navigationWifi_) {
    controller->showPage(Page::Wifi);
  } else if (target == controller->navigationSsh_) {
    controller->showPage(Page::Ssh);
  } else if (target == controller->navigationLogs_) {
    controller->showPage(Page::Logs);
  }
}

void UiController::handleSshControls(lv_event_t* event) {
  auto* controller = static_cast<UiController*>(lv_event_get_user_data(event));
  if (controller == nullptr || lv_event_get_target(event) !=
                                  controller->sshActionButton_) {
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
    if (controller->ssh_.configure("nova",
                                   lv_textarea_get_text(controller->sshPassword_),
                                   true)) {
      controller->ssh_.setEnabled(true);
    }
    controller->hideSshPassword();
  }
}

void UiController::handleWifiControls(lv_event_t* event) {
  auto* controller = static_cast<UiController*>(lv_event_get_user_data(event));
  if (controller == nullptr) {
    return;
  }

  lv_obj_t* target = lv_event_get_target(event);
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
  controller->hideWifiPassword();
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
  wifiPage_ = lv_obj_create(content);
  sshPage_ = lv_obj_create(content);
  logsPage_ = lv_obj_create(content);
  preparePage(homePage_);
  preparePage(diagnosticsPage_);
  preparePage(wifiPage_);
  preparePage(sshPage_);
  preparePage(logsPage_);

  lv_obj_t* homeTitle = lv_label_create(homePage_);
  lv_label_set_text(homeTitle, "READY FOR LOCAL OPERATION");
  lv_obj_set_pos(homeTitle, 12, 14);
  lv_obj_set_style_text_color(homeTitle, lv_color_hex(kAccentColor),
                              LV_PART_MAIN);
  homeStatus_ = lv_label_create(homePage_);
  lv_label_set_text_fmt(
      homeStatus_,
      "Display: %s\nTouch: %s\nWi-Fi: %s\nSSH: %s\n\n"
      "USB serial is optional.\nThe device is ready to run from a power bank.",
      display_.isReady() ? "READY" : "FAILED",
      touch_.isReady() ? "READY" : "FAILED", wifi_.stateName(),
      ssh_.isEnabled() ? "ENABLED" : "DISABLED");
  lv_obj_set_pos(homeStatus_, 12, 54);
  lv_obj_set_style_text_color(homeStatus_, lv_color_hex(kTextColor),
                              LV_PART_MAIN);

  lv_obj_t* diagnosticsTitle = lv_label_create(diagnosticsPage_);
  lv_label_set_text(diagnosticsTitle, "COMPONENT STATUS");
  lv_obj_set_pos(diagnosticsTitle, 12, 14);
  lv_obj_set_style_text_color(diagnosticsTitle, lv_color_hex(kAccentColor),
                              LV_PART_MAIN);
  diagnosticsStatus_ = lv_label_create(diagnosticsPage_);
  lv_label_set_text(
      diagnosticsStatus_,
      "HW-001  Display       READY\nHW-002  Touch         READY\n"
      "HW-003  Wi-Fi        NOT CONFIGURED\nHW-004  IMU           NOT RUN\n"
      "HW-005  Audio        NOT RUN\nHW-006  SD card       NOT RUN\n\n"
      "Production service integration is next.");
  lv_obj_set_pos(diagnosticsStatus_, 12, 54);
  lv_obj_set_style_text_color(diagnosticsStatus_, lv_color_hex(kTextColor),
                              LV_PART_MAIN);

  lv_obj_t* wifiTitle = lv_label_create(wifiPage_);
  lv_label_set_text(wifiTitle, "WI-FI SETUP");
  lv_obj_set_pos(wifiTitle, 12, 14);
  lv_obj_set_style_text_color(wifiTitle, lv_color_hex(kAccentColor),
                              LV_PART_MAIN);
  wifiStatus_ = lv_label_create(wifiPage_);
  lv_obj_set_pos(wifiStatus_, 12, 48);
  lv_obj_set_style_text_color(wifiStatus_, lv_color_hex(kTextColor),
                              LV_PART_MAIN);
  wifiScanButton_ = lv_btn_create(wifiPage_);
  lv_obj_set_size(wifiScanButton_, 82, 34);
  lv_obj_set_pos(wifiScanButton_, 210, 10);
  styleObject(wifiScanButton_, kPanelColor, kTextColor);
  lv_obj_add_event_cb(wifiScanButton_, handleWifiControls, LV_EVENT_CLICKED,
                      this);
  lv_obj_t* scanLabel = lv_label_create(wifiScanButton_);
  lv_label_set_text(scanLabel, "SCAN");
  lv_obj_center(scanLabel);
  wifiList_ = lv_list_create(wifiPage_);
  lv_obj_set_size(wifiList_, 280, 250);
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
  wifiKeyboard_ = lv_keyboard_create(wifiPage_);
  lv_obj_set_size(wifiKeyboard_, 304, 270);
  lv_obj_set_pos(wifiKeyboard_, 0, 92);
  lv_keyboard_set_mode(wifiKeyboard_, LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_keyboard_set_textarea(wifiKeyboard_, wifiPassword_);
  lv_obj_add_flag(wifiPassword_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(wifiKeyboard_, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* sshTitle = lv_label_create(sshPage_);
  lv_label_set_text(sshTitle, "SSH ACCESS");
  lv_obj_set_pos(sshTitle, 12, 14);
  lv_obj_set_style_text_color(sshTitle, lv_color_hex(kAccentColor),
                              LV_PART_MAIN);
  sshStatus_ = lv_label_create(sshPage_);
  lv_obj_set_pos(sshStatus_, 12, 50);
  lv_obj_set_style_text_color(sshStatus_, lv_color_hex(kTextColor),
                              LV_PART_MAIN);
  sshActionButton_ = lv_btn_create(sshPage_);
  lv_obj_set_size(sshActionButton_, 82, 34);
  lv_obj_set_pos(sshActionButton_, 210, 10);
  styleObject(sshActionButton_, kPanelColor, kTextColor);
  lv_obj_add_event_cb(sshActionButton_, handleSshControls, LV_EVENT_CLICKED,
                      this);
  lv_obj_t* sshActionLabel = lv_label_create(sshActionButton_);
  lv_label_set_text(sshActionLabel, "SET UP");
  lv_obj_center(sshActionLabel);
  sshInstructions_ = lv_label_create(sshPage_);
  lv_label_set_text(sshInstructions_,
                    "SSH is LAN-only and disabled until\na password is configured.\n\n"
                    "The server exposes diagnostic commands,\nnot an operating-system shell.");
  lv_obj_set_pos(sshInstructions_, 12, 108);
  lv_obj_set_style_text_color(sshInstructions_, lv_color_hex(kTextColor),
                              LV_PART_MAIN);
  sshPassword_ = lv_textarea_create(sshPage_);
  lv_obj_set_size(sshPassword_, 280, 42);
  lv_obj_set_pos(sshPassword_, 12, 44);
  lv_textarea_set_one_line(sshPassword_, true);
  lv_textarea_set_password_mode(sshPassword_, true);
  lv_textarea_set_placeholder_text(sshPassword_, "SSH password");
  styleObject(sshPassword_, kPanelColor, kTextColor);
  lv_obj_add_event_cb(sshPassword_, handleSshKeyboard, LV_EVENT_ALL, this);
  sshKeyboard_ = lv_keyboard_create(sshPage_);
  lv_obj_set_size(sshKeyboard_, 304, 270);
  lv_obj_set_pos(sshKeyboard_, 0, 92);
  lv_keyboard_set_mode(sshKeyboard_, LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_keyboard_set_textarea(sshKeyboard_, sshPassword_);
  lv_obj_add_flag(sshPassword_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(sshKeyboard_, LV_OBJ_FLAG_HIDDEN);

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
  navigationDiagnostics_ = createNavigationButton("DIAG", 67);
  navigationWifi_ = createNavigationButton("WIFI", 126);
  navigationSsh_ = createNavigationButton("SSH", 185);
  navigationLogs_ = createNavigationButton("LOG", 244);
  lv_obj_add_flag(diagnosticsPage_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(wifiPage_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(sshPage_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(logsPage_, LV_OBJ_FLAG_HIDDEN);
}

void UiController::showPage(Page page) {
  lv_obj_add_flag(homePage_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(diagnosticsPage_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(wifiPage_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(sshPage_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(logsPage_, LV_OBJ_FLAG_HIDDEN);
  switch (page) {
    case Page::Home:
      lv_obj_clear_flag(homePage_, LV_OBJ_FLAG_HIDDEN);
      break;
    case Page::Diagnostics:
      lv_obj_clear_flag(diagnosticsPage_, LV_OBJ_FLAG_HIDDEN);
      break;
    case Page::Wifi:
      lv_obj_clear_flag(wifiPage_, LV_OBJ_FLAG_HIDDEN);
      updateWifiView();
      break;
    case Page::Ssh:
      lv_obj_clear_flag(sshPage_, LV_OBJ_FLAG_HIDDEN);
      updateSshView();
      break;
    case Page::Logs:
      lv_obj_clear_flag(logsPage_, LV_OBJ_FLAG_HIDDEN);
      updateLogView();
      break;
  }
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
}

void UiController::hideWifiPassword() {
  selectedNetworkIndex_ = WifiService::kMaxNetworks;
  lv_obj_add_flag(wifiPassword_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(wifiKeyboard_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(wifiList_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(wifiScanButton_, LV_OBJ_FLAG_HIDDEN);
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
}

void UiController::hideSshPassword() {
  lv_obj_add_flag(sshPassword_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(sshKeyboard_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(sshInstructions_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(sshActionButton_, LV_OBJ_FLAG_HIDDEN);
  updateSshView();
}

void UiController::updateHomeView() {
  if (homeStatus_ == nullptr) {
    return;
  }
  const String ip = wifi_.ipAddress().toString();
  lv_label_set_text_fmt(
      homeStatus_,
      "Display: %s\nTouch: %s\nWi-Fi: %s\nIP: %s\nSSH: %s\n\n"
      "USB serial is optional.\nThe device is ready to run from a power bank.",
      display_.isReady() ? "READY" : "FAILED",
      touch_.isReady() ? "READY" : "FAILED", wifi_.stateName(), ip.c_str(),
      ssh_.isEnabled() ? "ENABLED" : "DISABLED");
}

void UiController::updateDiagnosticsView() {
  if (diagnosticsStatus_ == nullptr) {
    return;
  }
  lv_label_set_text_fmt(
      diagnosticsStatus_,
      "HW-001  Display       %s\nHW-002  Touch         %s\n"
      "HW-003  Wi-Fi        %s\nHW-004  IMU           NOT RUN\n"
      "HW-005  Audio        NOT RUN\nHW-006  SD card       NOT RUN\n\n"
      "Production service integration is next.",
      display_.isReady() ? "READY" : "FAILED",
      touch_.isReady() ? "READY" : "FAILED", wifi_.stateName());
}

void UiController::updateWifiView() {
  if (wifiStatus_ == nullptr || wifiList_ == nullptr) {
    return;
  }

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
  if (sshStatus_ == nullptr || sshActionButton_ == nullptr) {
    return;
  }

  const String ip = wifi_.ipAddress().toString();
  if (!ssh_.hasCredentials()) {
    lv_label_set_text(sshStatus_, "Status: NOT CONFIGURED\nUser: nova");
  } else if (ssh_.isEnabled()) {
    lv_label_set_text_fmt(sshStatus_, "Status: %s\nssh %s@%s",
                          ssh_.isReady() ? "READY" : "STARTING", ssh_.username(),
                          ip.c_str());
  } else {
    lv_label_set_text_fmt(sshStatus_, "Status: DISABLED\nUser: %s",
                          ssh_.username());
  }
  lv_obj_t* actionLabel = lv_obj_get_child(sshActionButton_, 0);
  if (actionLabel != nullptr) {
    lv_label_set_text(actionLabel,
                      ssh_.isEnabled() ? "DISABLE"
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
  lv_obj_set_size(button, 72, 44);
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
