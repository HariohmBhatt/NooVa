#include <Arduino.h>

#include "core/Logger.h"
#include "hardware/ButtonService.h"
#include "hardware/BoardDisplay.h"
#include "hardware/BoardTouch.h"
#include "network/SshService.h"
#include "network/WifiService.h"
#include "network/OtaService.h"
#include "ui/UiController.h"

namespace {

constexpr uint32_t kSerialBaudRate = 115200;
constexpr uint32_t kLoopDelayMs = 5;

nova::Logger gLogger;
nova::ButtonService gButtons(gLogger);
nova::BoardDisplay gDisplay;
nova::BoardTouch gTouch;
nova::WifiService gWifi(gLogger);
nova::SshService gSsh(gLogger, gWifi);
nova::OtaService gOta(gLogger, gWifi, gSsh);
nova::UiController gUi(gDisplay, gTouch, gLogger, gWifi, gSsh);
bool gRemoteServicesPaused = false;

void pauseRemoteServices() {
  if (gRemoteServicesPaused) {
    return;
  }
  gRemoteServicesPaused = true;
  gSsh.setEnabled(false, false);
  gWifi.disconnect();
  gLogger.write(nova::LogLevel::Info,
                "Remote services paused until the next boot");
}

}  // namespace

void setup() {
  Serial.begin(kSerialBaudRate);
  gLogger.write(nova::LogLevel::Info, "Nova appliance booting");
  gLogger.writef(nova::LogLevel::Info, "Chip=%s revision=%d flash=%lu PSRAM=%s",
                 ESP.getChipModel(), ESP.getChipRevision(),
                 static_cast<unsigned long>(ESP.getFlashChipSize()),
                 psramFound() ? "READY" : "MISSING");

  if (!gDisplay.begin()) {
    gLogger.write(nova::LogLevel::Error, "Display initialization failed");
    return;
  }
  gLogger.write(nova::LogLevel::Info, "Display initialized");

  if (gTouch.begin()) {
    gLogger.write(nova::LogLevel::Info, "Touch initialized");
  } else {
    gLogger.write(nova::LogLevel::Error, "Touch initialization failed");
  }
  if (!gButtons.begin()) {
    gLogger.write(nova::LogLevel::Error, "Button service initialization failed");
  }

  if (!gWifi.begin()) {
    gLogger.write(nova::LogLevel::Error, "Wi-Fi service initialization failed");
  }
  if (!gSsh.begin()) {
    gLogger.write(nova::LogLevel::Error, "SSH service initialization failed");
  }
  if (!gOta.begin()) {
    gLogger.write(nova::LogLevel::Error, "OTA service initialization failed");
  }

  if (!gUi.begin()) {
    gLogger.write(nova::LogLevel::Error, "UI initialization failed");
  }
}

void loop() {
  if (gButtons.update() == nova::ButtonEvent::BootReleased) {
    pauseRemoteServices();
  }
  gWifi.update();
  gSsh.update();
  gOta.update();
  gUi.update();
  delay(kLoopDelayMs);
}
