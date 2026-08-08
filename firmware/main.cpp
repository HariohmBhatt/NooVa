#include <Arduino.h>

#include "core/Logger.h"
#include "hardware/BoardDisplay.h"
#include "hardware/BoardTouch.h"
#include "network/WifiService.h"
#include "ui/UiController.h"

namespace {

constexpr uint32_t kSerialBaudRate = 115200;
constexpr uint32_t kLoopDelayMs = 5;

nova::Logger gLogger;
nova::BoardDisplay gDisplay;
nova::BoardTouch gTouch;
nova::WifiService gWifi(gLogger);
nova::UiController gUi(gDisplay, gTouch, gLogger, gWifi);

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

  if (!gWifi.begin()) {
    gLogger.write(nova::LogLevel::Error, "Wi-Fi service initialization failed");
  }

  if (!gUi.begin()) {
    gLogger.write(nova::LogLevel::Error, "UI initialization failed");
  }
}

void loop() {
  gWifi.update();
  gUi.update();
  delay(kLoopDelayMs);
}
