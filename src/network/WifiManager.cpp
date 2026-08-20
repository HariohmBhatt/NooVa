#include "WifiManager.h"

#include <cstdio>

namespace nova {

bool WifiManager::begin(const char* fallbackSsid, const char* fallbackPassword,
                        uint32_t nowMs) {
  if (!preferences_.begin("nova", true)) {
    state_ = WifiManagerState::StorageError;
    return false;
  }

  // Preserve the deployed archive's namespace and key names so an already
  // provisioned board upgrades without requiring credentials to be re-entered.
  if (preferences_.isKey("wifi_ssid")) {
    preferences_.getString("wifi_ssid", ssid_, sizeof(ssid_));
    preferences_.getString("wifi_password", password_, sizeof(password_));
  }
  preferences_.end();
  ssid_[sizeof(ssid_) - 1] = '\0';
  password_[sizeof(password_) - 1] = '\0';

  // An ignored header or build environment can provision a clean board. These
  // fallback credentials are used in RAM only and are not silently persisted.
  if (ssid_[0] == '\0' && fallbackSsid != nullptr && fallbackSsid[0] != '\0') {
    std::snprintf(ssid_, sizeof(ssid_), "%s", fallbackSsid);
    std::snprintf(password_, sizeof(password_), "%s",
                  fallbackPassword == nullptr ? "" : fallbackPassword);
  }

  credentialsReady_ = ssid_[0] != '\0';
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  if (!credentialsReady_) {
    state_ = WifiManagerState::Unconfigured;
    return true;
  }

  startConnection(nowMs);
  return true;
}

void WifiManager::update(uint32_t nowMs) {
  if (!credentialsReady_ || state_ == WifiManagerState::StorageError) {
    return;
  }

  if (state_ == WifiManagerState::Connecting) {
    if (WiFi.status() == WL_CONNECTED) {
      state_ = WifiManagerState::Connected;
      phaseStartedAtMs_ = nowMs;
    } else if (nowMs - phaseStartedAtMs_ >= kConnectionTimeoutMs) {
      WiFi.disconnect(false, false);
      state_ = WifiManagerState::Offline;
      phaseStartedAtMs_ = nowMs;
    }
    return;
  }

  if (state_ == WifiManagerState::Connected && WiFi.status() != WL_CONNECTED) {
    state_ = WifiManagerState::Offline;
    phaseStartedAtMs_ = nowMs;
    return;
  }

  if (state_ == WifiManagerState::Offline &&
      nowMs - phaseStartedAtMs_ >= kReconnectDelayMs) {
    startConnection(nowMs);
  }
}

WifiManagerState WifiManager::state() const { return state_; }

bool WifiManager::hasCredentials() const { return credentialsReady_; }

bool WifiManager::connected() const {
  return state_ == WifiManagerState::Connected;
}

int32_t WifiManager::rssi() const { return connected() ? WiFi.RSSI() : 0; }

void WifiManager::startConnection(uint32_t nowMs) {
  WiFi.disconnect(false, false);
  WiFi.begin(ssid_, password_);
  state_ = WifiManagerState::Connecting;
  phaseStartedAtMs_ = nowMs;
}

}  // namespace nova
