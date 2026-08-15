#include <Arduino.h>

#include "core/Logger.h"
#include "hardware/ButtonService.h"
#include "hardware/BoardDisplay.h"
#include "hardware/BoardTouch.h"
#include "network/HubConnectionService.h"
#include "network/SshService.h"
#include "network/WifiService.h"
#include "network/OtaService.h"
#include "telemetry/DeviceTelemetryCollector.h"
#include "ui/UiController.h"

namespace {

constexpr uint32_t kSerialBaudRate = 115200;
constexpr uint32_t kLoopDelayMs = 5;

nova::Logger gLogger;
nova::ButtonService gButtons(gLogger);
nova::BoardDisplay gDisplay;
nova::BoardTouch gTouch;
nova::WifiService gWifi(gLogger);
nova::DeviceTelemetryCollector gTelemetry(gWifi, gTouch);
nova::HubConnectionService gHub(gLogger, gWifi, gTelemetry);
nova::SshService gSsh(gLogger, gWifi);
nova::OtaService gOta(gLogger, gWifi, gSsh);
nova::UiController gUi(gDisplay, gTouch, gLogger, gWifi, gSsh, gHub);
bool gRemoteServicesPaused = false;

void pauseAppliance() {
  if (gRemoteServicesPaused) {
    return;
  }
  gRemoteServicesPaused = true;
  gDisplay.setBacklight(0);
  gSsh.setEnabled(false, false);
  gWifi.pause();
  gLogger.write(nova::LogLevel::Info,
                "Display and remote services paused until the next boot");
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
  if (!gHub.begin()) {
    gLogger.write(nova::LogLevel::Error, "Hub connection service initialization failed");
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
  const nova::ButtonEvent buttonEvent = gButtons.update();
  if (buttonEvent == nova::ButtonEvent::BootPressed ||
      buttonEvent == nova::ButtonEvent::PwrPressed) {
    pauseAppliance();
  }
  gWifi.update();
  gHub.update();
  gSsh.update();
  gOta.update();
  gUi.update();
  delay(kLoopDelayMs);
}
