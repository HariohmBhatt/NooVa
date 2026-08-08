#include "OtaService.h"

#include <ArduinoOTA.h>

namespace nova {
namespace {

constexpr char kPasswordPreference[] = "ssh_password";
OtaService* gActiveService = nullptr;

}  // namespace

OtaService::OtaService(Logger& logger, WifiService& wifi, SshService& ssh)
    : logger_(logger), wifi_(wifi), ssh_(ssh) {}

bool OtaService::begin() {
  preferencesReady_ = preferences_.begin("nova", true);
  credentialReady_ = preferencesReady_ && loadPassword();
  ready_ = credentialReady_;
  if (!credentialReady_) {
    logger_.write(LogLevel::Debug, "OTA awaiting SSH credential setup");
  }
  return true;
}

void OtaService::update() {
  if (!ready_ || !ssh_.isEnabled() || wifi_.state() != WifiState::Connected) {
    if (started_) {
      stop();
    }
    return;
  }

  if (!started_) {
    start();
  }
  ArduinoOTA.handle();
}

bool OtaService::isReady() const { return ready_; }

void OtaService::handleStart() {
  if (gActiveService != nullptr) {
    gActiveService->logger_.write(LogLevel::Info, "OTA update started");
  }
}

void OtaService::handleEnd() {
  if (gActiveService != nullptr) {
    gActiveService->logger_.write(LogLevel::Info, "OTA update complete");
  }
}

void OtaService::handleError(ota_error_t error) {
  if (gActiveService != nullptr) {
    gActiveService->logger_.writef(LogLevel::Error, "OTA error=%u",
                                   static_cast<unsigned>(error));
  }
}

bool OtaService::loadPassword() {
  if (!preferences_.isKey(kPasswordPreference)) {
    return false;
  }
  preferences_.getString(kPasswordPreference, password_, sizeof(password_));
  password_[sizeof(password_) - 1] = '\0';
  return password_[0] != '\0';
}

void OtaService::start() {
  gActiveService = this;
  ArduinoOTA.setHostname("nova");
  ArduinoOTA.setPassword(password_);
  ArduinoOTA.onStart(handleStart);
  ArduinoOTA.onEnd(handleEnd);
  ArduinoOTA.onError(handleError);
  ArduinoOTA.begin();
  started_ = true;
  logger_.write(LogLevel::Info, "OTA deployment ready on port 3232");
}

void OtaService::stop() {
  ArduinoOTA.end();
  started_ = false;
  gActiveService = nullptr;
  logger_.write(LogLevel::Info, "OTA deployment stopped");
}

}  // namespace nova
