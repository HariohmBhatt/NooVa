#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>

#include <cstddef>
#include <cstdint>

#include "../core/Logger.h"

namespace nova {

enum class WifiState : uint8_t {
  Unconfigured,
  Connecting,
  Connected,
  Disconnected,
  Error,
};

struct WifiNetwork {
  char ssid[33] = {};
  int32_t rssi = 0;
  uint8_t channel = 0;
  bool encrypted = false;
};

class WifiService {
 public:
  static constexpr size_t kMaxNetworks = 12;

  /** Construct a Wi-Fi service backed by the shared diagnostic logger. */
  explicit WifiService(Logger& logger);

  /** Initialize station mode and restore credentials from encrypted storage later. */
  bool begin();

  /** Advance scan, connection, timeout, and automatic-reconnect state. */
  void update();

  /** Start an asynchronous nearby-network scan. */
  void startScan();

  /** Connect to an SSID and optionally persist its credentials in NVS. */
  bool connect(const char* ssid, const char* password, bool persist);

  /** Disconnect and disable automatic reconnect until the next connect call. */
  void disconnect();

  /** Return the current station state. */
  WifiState state() const;

  /** Return a short UI-safe state name without credentials. */
  const char* stateName() const;

  /** Return whether a scan is currently running. */
  bool scanInProgress() const;

  /** Return the number of scan results currently available. */
  size_t networkCount() const;

  /** Return one scan result, or nullptr when the index is invalid. */
  const WifiNetwork* network(size_t index) const;

  /** Return the configured SSID without exposing the password. */
  const char* configuredSsid() const;

  /** Return the current station IP address. */
  IPAddress ipAddress() const;

  /** Return the current RSSI, or zero when disconnected. */
  int32_t rssi() const;

 private:
  static constexpr uint32_t kConnectionTimeoutMs = 15000;
  static constexpr uint32_t kReconnectDelayMs = 5000;

  void startConnection();
  void finishScan(int result);
  void updateConnection(uint32_t now);

  Logger& logger_;
  Preferences preferences_;
  WifiNetwork networks_[kMaxNetworks] = {};
  char ssid_[33] = {};
  char password_[65] = {};
  size_t networkCount_ = 0;
  uint32_t connectionStartedAt_ = 0;
  uint32_t reconnectAt_ = 0;
  WifiState state_ = WifiState::Unconfigured;
  bool preferencesReady_ = false;
  bool credentialsReady_ = false;
  bool connectionActive_ = false;
  bool autoReconnect_ = false;
  bool scanActive_ = false;
};

}  // namespace nova
