#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include <cstddef>
#include <cstdint>

#include "../core/Logger.h"
#include "WifiService.h"

namespace nova {

class SshService {
 public:
  static constexpr uint16_t kPort = 22;

  /** Construct the SSH service using Wi-Fi and diagnostics as dependencies. */
  SshService(Logger& logger, WifiService& wifi);

  /** Load SSH settings and prepare the server task. */
  bool begin();

  /** Keep the public service state synchronized with its worker task. */
  void update();

  /** Store the password and username required for SSH password authentication. */
  bool configure(const char* username, const char* password, bool persist);

  /** Enable or disable the LAN SSH server, optionally preserving its setting. */
  bool setEnabled(bool enabled, bool persist = true);

  /** Return whether the server is enabled in persistent settings. */
  bool isEnabled() const;

  /** Return whether authentication credentials have been configured. */
  bool hasCredentials() const;

  /** Return whether the server task is listening for connections. */
  bool isReady() const;

  /** Return the configured SSH username without exposing its password. */
  const char* username() const;

 private:
  static constexpr size_t kUsernameLength = 33;
  static constexpr size_t kPasswordLength = 65;
  static constexpr size_t kHostKeyLength = 2048;
  static constexpr uint32_t kTaskStackSize = 12288;

  static void taskEntry(void* argument);
  void runServer();
  bool ensureHostKey();
  bool authenticate(void* session);
  void serveSession(void* session);
  void executeCommand(void* channel, const char* command);
  void write(void* channel, const char* text);
  void writeLogs(void* channel);

  Logger& logger_;
  WifiService& wifi_;
  Preferences preferences_;
  char username_[kUsernameLength] = {};
  char password_[kPasswordLength] = {};
  char hostKeyBase64_[kHostKeyLength] = {};
  TaskHandle_t taskHandle_ = nullptr;
  volatile bool enabled_ = false;
  volatile bool ready_ = false;
  bool preferencesReady_ = false;
  bool credentialsReady_ = false;
};

}  // namespace nova
