#pragma once

#include <Preferences.h>
#include <WiFi.h>

#include <cstddef>
#include <cstdint>

namespace nova {

enum class WifiManagerState : uint8_t {
  Unconfigured,
  Connecting,
  Connected,
  Offline,
  StorageError,
};

/**
 * Small station-mode manager compatible with the archived firmware's NVS keys.
 * Credentials never leave this module and are never written to Serial.
 */
class WifiManager {
 public:
  bool begin(const char* fallbackSsid, const char* fallbackPassword,
             uint32_t nowMs);
  void update(uint32_t nowMs);

  WifiManagerState state() const;
  bool hasCredentials() const;
  bool connected() const;
  int32_t rssi() const;

 private:
  static constexpr uint32_t kConnectionTimeoutMs = 15000;
  static constexpr uint32_t kReconnectDelayMs = 5000;

  void startConnection(uint32_t nowMs);

  Preferences preferences_;
  char ssid_[33]{};
  char password_[65]{};
  uint32_t phaseStartedAtMs_ = 0;
  WifiManagerState state_ = WifiManagerState::Unconfigured;
  bool credentialsReady_ = false;
};

}  // namespace nova
