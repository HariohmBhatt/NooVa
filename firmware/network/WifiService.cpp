#include "WifiService.h"

#include <algorithm>
#include <cstdio>

namespace nova {

WifiService::WifiService(Logger& logger) : logger_(logger) {}

bool WifiService::begin() {
  servicePaused_ = false;
  preferencesReady_ = preferences_.begin("nova", false);
  if (!preferencesReady_) {
    state_ = WifiState::Error;
    logger_.write(LogLevel::Error, "Wi-Fi settings storage unavailable");
    return false;
  }

  if (preferences_.isKey("wifi_ssid")) {
    preferences_.getString("wifi_ssid", ssid_, sizeof(ssid_));
  }
  if (preferences_.isKey("wifi_password")) {
    preferences_.getString("wifi_password", password_, sizeof(password_));
  }
  ssid_[sizeof(ssid_) - 1] = '\0';
  password_[sizeof(password_) - 1] = '\0';
  credentialsReady_ = ssid_[0] != '\0';

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  if (!credentialsReady_) {
    state_ = WifiState::Unconfigured;
    logger_.write(LogLevel::Info, "Wi-Fi awaiting touchscreen provisioning");
    return true;
  }

  autoReconnect_ = true;
  startConnection();
  return true;
}

void WifiService::update() {
  const uint32_t now = millis();
  if (scanActive_) {
    const int result = WiFi.scanComplete();
    if (result != WIFI_SCAN_RUNNING) {
      finishScan(result);
    }
  }

  updateConnection(now);
  if (autoReconnect_ && !connectionActive_ && state_ != WifiState::Connected &&
      now >= reconnectAt_) {
    startConnection();
  }
}

void WifiService::startScan() {
  if (servicePaused_ || scanActive_) {
    return;
  }

  networkCount_ = 0;
  WiFi.scanDelete();
  const int result = WiFi.scanNetworks(true, true);
  if (result == WIFI_SCAN_FAILED) {
    logger_.write(LogLevel::Error, "Wi-Fi scan could not start");
    return;
  }
  scanActive_ = true;
  logger_.write(LogLevel::Info, "Wi-Fi scan started");
}

bool WifiService::connect(const char* ssid, const char* password, bool persist) {
  if (ssid == nullptr || ssid[0] == '\0') {
    return false;
  }

  snprintf(ssid_, sizeof(ssid_), "%s", ssid);
  snprintf(password_, sizeof(password_), "%s", password == nullptr ? "" : password);
  credentialsReady_ = true;
  servicePaused_ = false;
  autoReconnect_ = true;
  if (persist && preferencesReady_) {
    preferences_.putString("wifi_ssid", ssid_);
    preferences_.putString("wifi_password", password_);
  }
  startConnection();
  return true;
}

void WifiService::disconnect() {
  cancelScan();
  autoReconnect_ = false;
  connectionActive_ = false;
  WiFi.disconnect(false, false);
  state_ = credentialsReady_ ? WifiState::Disconnected : WifiState::Unconfigured;
  logger_.write(LogLevel::Info, "Wi-Fi disconnected by user");
}

void WifiService::pause() {
  cancelScan();
  servicePaused_ = true;
  autoReconnect_ = false;
  connectionActive_ = false;
  WiFi.disconnect(false, false);
  state_ = credentialsReady_ ? WifiState::Disconnected : WifiState::Unconfigured;
  logger_.write(LogLevel::Info, "Wi-Fi service paused until the next boot");
}

WifiState WifiService::state() const { return state_; }

const char* WifiService::stateName() const {
  if (scanActive_) {
    return "SCANNING";
  }
  switch (state_) {
    case WifiState::Unconfigured:
      return "NOT CONFIGURED";
    case WifiState::Connecting:
      return "CONNECTING";
    case WifiState::Connected:
      return "CONNECTED";
    case WifiState::Disconnected:
      return "DISCONNECTED";
    case WifiState::Error:
      return "ERROR";
  }
  return "UNKNOWN";
}

bool WifiService::scanInProgress() const { return scanActive_; }

size_t WifiService::networkCount() const { return networkCount_; }

const WifiNetwork* WifiService::network(size_t index) const {
  return index < networkCount_ ? &networks_[index] : nullptr;
}

const char* WifiService::configuredSsid() const { return ssid_; }

IPAddress WifiService::ipAddress() const { return WiFi.localIP(); }

int32_t WifiService::rssi() const {
  return state_ == WifiState::Connected ? WiFi.RSSI() : 0;
}

void WifiService::startConnection() {
  if (!credentialsReady_) {
    state_ = WifiState::Unconfigured;
    return;
  }

  WiFi.disconnect(false, false);
  WiFi.begin(ssid_, password_);
  connectionStartedAt_ = millis();
  connectionActive_ = true;
  state_ = WifiState::Connecting;
  logger_.writef(LogLevel::Info, "Wi-Fi connecting to %s", ssid_);
}

void WifiService::cancelScan() {
  if (!scanActive_) {
    return;
  }
  WiFi.scanDelete();
  scanActive_ = false;
  networkCount_ = 0;
  logger_.write(LogLevel::Info, "Wi-Fi scan stopped");
}

void WifiService::finishScan(int result) {
  scanActive_ = false;
  if (result < 0) {
    logger_.write(LogLevel::Error, "Wi-Fi scan failed");
    return;
  }

  networkCount_ = std::min(static_cast<size_t>(result), kMaxNetworks);
  for (size_t index = 0; index < networkCount_; ++index) {
    networks_[index].ssid[0] = '\0';
    WiFi.SSID(index).toCharArray(networks_[index].ssid,
                                sizeof(networks_[index].ssid));
    networks_[index].rssi = WiFi.RSSI(index);
    networks_[index].channel = WiFi.channel(index);
    networks_[index].encrypted = WiFi.encryptionType(index) != WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  logger_.writef(LogLevel::Info, "Wi-Fi scan complete: %u networks",
                 static_cast<unsigned>(networkCount_));
}

void WifiService::updateConnection(uint32_t now) {
  if (!connectionActive_) {
    if (state_ == WifiState::Connected && WiFi.status() != WL_CONNECTED) {
      connectionActive_ = false;
      state_ = WifiState::Disconnected;
      reconnectAt_ = now + kReconnectDelayMs;
      logger_.write(LogLevel::Warning, "Wi-Fi connection lost");
    }
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    connectionActive_ = false;
    state_ = WifiState::Connected;
    logger_.writef(LogLevel::Info, "Wi-Fi connected, IP=%s",
                   WiFi.localIP().toString().c_str());
    return;
  }

  if (now - connectionStartedAt_ >= kConnectionTimeoutMs) {
    connectionActive_ = false;
    state_ = WifiState::Error;
    reconnectAt_ = now + kReconnectDelayMs;
    logger_.write(LogLevel::Warning, "Wi-Fi connection timeout");
  }
}

}  // namespace nova
