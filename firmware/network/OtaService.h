#pragma once

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Preferences.h>

#include "../core/Logger.h"
#include "SshService.h"
#include "WifiService.h"

namespace nova {

class OtaService {
 public:
  /** Construct OTA deployment support for the authenticated SSH session. */
  OtaService(Logger& logger, WifiService& wifi, SshService& ssh);

  /** Load the deployment credential from persistent settings. */
  bool begin();

  /** Start or stop OTA availability and process incoming update packets. */
  void update();

  /** Return whether OTA credentials and service initialization are available. */
  bool isReady() const;

 private:
  static constexpr size_t kPasswordLength = 65;

  static void handleStart();
  static void handleEnd();
  static void handleError(ota_error_t error);

  bool loadPassword();
  void start();
  void stop();

  Logger& logger_;
  WifiService& wifi_;
  SshService& ssh_;
  Preferences preferences_;
  char password_[kPasswordLength] = {};
  bool preferencesReady_ = false;
  bool credentialReady_ = false;
  bool started_ = false;
  bool ready_ = false;
};

}  // namespace nova
