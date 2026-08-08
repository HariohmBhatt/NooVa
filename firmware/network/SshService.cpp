#include "SshService.h"

#include <libssh/libssh.h>
#include <libssh/server.h>
#include <libssh_esp32.h>

#include <cstdio>
#include <cstring>

namespace nova {
namespace {

constexpr char kHostKeyPreference[] = "ssh_hostkey";
constexpr char kUsernamePreference[] = "ssh_username";
constexpr char kPasswordPreference[] = "ssh_password";
constexpr char kEnabledPreference[] = "ssh_enabled";

}  // namespace

SshService::SshService(Logger& logger, WifiService& wifi)
    : logger_(logger), wifi_(wifi) {}

bool SshService::begin() {
  preferencesReady_ = preferences_.begin("nova", false);
  if (!preferencesReady_) {
    logger_.write(LogLevel::Error, "SSH settings storage unavailable");
    return false;
  }

  if (preferences_.isKey(kUsernamePreference)) {
    preferences_.getString(kUsernamePreference, username_, sizeof(username_));
  }
  if (preferences_.isKey(kPasswordPreference)) {
    preferences_.getString(kPasswordPreference, password_, sizeof(password_));
  }
  username_[sizeof(username_) - 1] = '\0';
  password_[sizeof(password_) - 1] = '\0';
  credentialsReady_ = username_[0] != '\0' && password_[0] != '\0';
  enabled_ = preferences_.getBool(kEnabledPreference, false);
  if (enabled_ && credentialsReady_) {
    if (xTaskCreatePinnedToCore(taskEntry, "nova-ssh", kTaskStackSize, this,
                                1, &taskHandle_, 0) != pdPASS) {
      enabled_ = false;
      logger_.write(LogLevel::Error, "SSH worker task could not start");
      return false;
    }
  }
  logger_.write(enabled_ ? LogLevel::Info : LogLevel::Debug,
                enabled_ ? "SSH enabled from persistent settings"
                         : "SSH disabled until touchscreen setup");
  return true;
}

void SshService::update() {}

bool SshService::configure(const char* username, const char* password,
                           bool persist) {
  if (username == nullptr || username[0] == '\0' || password == nullptr ||
      password[0] == '\0') {
    return false;
  }
  snprintf(username_, sizeof(username_), "%s", username);
  snprintf(password_, sizeof(password_), "%s", password);
  credentialsReady_ = true;
  if (persist && preferencesReady_) {
    preferences_.putString(kUsernamePreference, username_);
    preferences_.putString(kPasswordPreference, password_);
  }
  logger_.write(LogLevel::Info, "SSH credentials configured");
  return true;
}

bool SshService::setEnabled(bool enabled, bool persist) {
  if (enabled && !credentialsReady_) {
    return false;
  }
  enabled_ = enabled;
  if (persist && preferencesReady_) {
    preferences_.putBool(kEnabledPreference, enabled);
  }
  if (enabled && taskHandle_ == nullptr) {
    if (xTaskCreatePinnedToCore(taskEntry, "nova-ssh", kTaskStackSize, this, 1,
                                &taskHandle_, 0) != pdPASS) {
      enabled_ = false;
      return false;
    }
  }
  logger_.write(enabled ? LogLevel::Info : LogLevel::Info,
                enabled ? "SSH server enabling" : "SSH server disabling");
  return true;
}

bool SshService::isEnabled() const { return enabled_; }

bool SshService::hasCredentials() const { return credentialsReady_; }

bool SshService::isReady() const { return ready_; }

const char* SshService::username() const { return username_; }

void SshService::taskEntry(void* argument) {
  auto* service = static_cast<SshService*>(argument);
  service->runServer();
  service->taskHandle_ = nullptr;
  vTaskDelete(nullptr);
}

void SshService::runServer() {
  libssh_begin();
  if (!ensureHostKey()) {
    enabled_ = false;
    logger_.write(LogLevel::Error, "SSH host key unavailable");
    return;
  }

  const int port = kPort;
  ssh_bind bind = ssh_bind_new();
  if (bind == nullptr ||
      ssh_bind_options_set(bind, SSH_BIND_OPTIONS_BINDPORT, &port) !=
          SSH_OK ||
      ssh_bind_options_set(bind, SSH_BIND_OPTIONS_IMPORT_KEY_STR,
                           hostKeyBase64_) != SSH_OK ||
      ssh_bind_listen(bind) != SSH_OK) {
    if (bind != nullptr) {
      ssh_bind_free(bind);
    }
    enabled_ = false;
    logger_.write(LogLevel::Error, "SSH server could not listen on port 22");
    return;
  }

  ssh_bind_set_blocking(bind, 0);
  ready_ = true;
  logger_.write(LogLevel::Info, "SSH server listening on port 22");
  while (enabled_) {
    if (wifi_.state() != WifiState::Connected) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    ssh_session session = ssh_new();
    if (session == nullptr) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    const int result = ssh_bind_accept(bind, session);
    if (result == SSH_AGAIN) {
      ssh_free(session);
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    if (result != SSH_OK) {
      ssh_free(session);
      vTaskDelay(pdMS_TO_TICKS(250));
      continue;
    }
    serveSession(session);
    ssh_free(session);
  }

  ready_ = false;
  ssh_bind_free(bind);
  ssh_finalize();
}

bool SshService::ensureHostKey() {
  if (preferences_.isKey(kHostKeyPreference)) {
    preferences_.getString(kHostKeyPreference, hostKeyBase64_,
                           sizeof(hostKeyBase64_));
    hostKeyBase64_[sizeof(hostKeyBase64_) - 1] = '\0';
    return hostKeyBase64_[0] != '\0';
  }

  ssh_key key = nullptr;
  char* encoded = nullptr;
  if (ssh_pki_generate(SSH_KEYTYPE_ED25519, 0, &key) != SSH_OK || key == nullptr ||
      ssh_pki_export_privkey_base64(key, nullptr, nullptr, nullptr, &encoded) !=
          SSH_OK || encoded == nullptr) {
    SSH_KEY_FREE(key);
    if (encoded != nullptr) {
      ssh_string_free_char(encoded);
    }
    return false;
  }

  snprintf(hostKeyBase64_, sizeof(hostKeyBase64_), "%s", encoded);
  const bool stored = preferences_.putString(kHostKeyPreference, encoded) > 0;
  ssh_string_free_char(encoded);
  SSH_KEY_FREE(key);
  return stored;
}

bool SshService::authenticate(void* rawSession) {
  auto session = static_cast<ssh_session>(rawSession);
  ssh_set_auth_methods(session, SSH_AUTH_METHOD_PASSWORD);
  while (true) {
    ssh_message message = ssh_message_get(session);
    if (message == nullptr) {
      return false;
    }
    if (ssh_message_type(message) == SSH_REQUEST_AUTH &&
        ssh_message_subtype(message) == SSH_AUTH_METHOD_PASSWORD) {
      const char* user = ssh_message_auth_user(message);
      const char* password = ssh_message_auth_password(message);
      const bool valid = user != nullptr && password != nullptr &&
                         strcmp(user, username_) == 0 &&
                         strcmp(password, password_) == 0;
      if (valid) {
        ssh_message_auth_reply_success(message, 0);
        ssh_message_free(message);
        return true;
      }
      ssh_message_auth_set_methods(message, SSH_AUTH_METHOD_PASSWORD);
    }
    ssh_message_reply_default(message);
    ssh_message_free(message);
  }
}

void SshService::serveSession(void* rawSession) {
  auto session = static_cast<ssh_session>(rawSession);
  if (ssh_handle_key_exchange(session) != SSH_OK || !authenticate(session)) {
    ssh_disconnect(session);
    return;
  }

  ssh_channel channel = nullptr;
  char command[128] = {};
  while (channel == nullptr) {
    ssh_message message = ssh_message_get(session);
    if (message == nullptr) {
      return;
    }
    if (ssh_message_type(message) == SSH_REQUEST_CHANNEL_OPEN &&
        ssh_message_subtype(message) == SSH_CHANNEL_SESSION) {
      channel = ssh_message_channel_request_open_reply_accept(message);
      ssh_message_free(message);
      break;
    }
    ssh_message_reply_default(message);
    ssh_message_free(message);
  }

  bool shell = false;
  while (!shell && command[0] == '\0') {
    ssh_message message = ssh_message_get(session);
    if (message == nullptr) {
      break;
    }
    if (ssh_message_type(message) == SSH_REQUEST_CHANNEL &&
        ssh_message_subtype(message) == SSH_CHANNEL_REQUEST_SHELL) {
      shell = true;
      ssh_message_channel_request_reply_success(message);
    } else if (ssh_message_type(message) == SSH_REQUEST_CHANNEL &&
               ssh_message_subtype(message) == SSH_CHANNEL_REQUEST_EXEC) {
      const char* requestedCommand = ssh_message_channel_request_command(message);
      if (requestedCommand != nullptr) {
        snprintf(command, sizeof(command), "%s", requestedCommand);
      }
      ssh_message_channel_request_reply_success(message);
    } else {
      ssh_message_reply_default(message);
    }
    ssh_message_free(message);
  }

  if (channel == nullptr) {
    return;
  }
  if (command[0] != '\0') {
    executeCommand(channel, command);
  } else if (shell) {
    write(channel, "Nova SSH diagnostics\r\nType 'help' for commands.\r\nnova> ");
    char line[128] = {};
    size_t length = 0;
    uint8_t buffer[64] = {};
    int received = 0;
    while ((received = ssh_channel_read(channel, buffer, sizeof(buffer), 0)) > 0) {
      for (int index = 0; index < received; ++index) {
        const char character = static_cast<char>(buffer[index]);
        if (character == '\r' || character == '\n') {
          line[length] = '\0';
          write(channel, "\r\n");
          executeCommand(channel, line);
          write(channel, "nova> ");
          length = 0;
        } else if ((character == '\b' || character == 127) && length > 0) {
          --length;
          write(channel, "\b \b");
        } else if (length + 1 < sizeof(line) && character >= 32) {
          line[length++] = character;
          ssh_channel_write(channel, &character, 1);
        }
      }
    }
  }
  ssh_channel_send_eof(channel);
  ssh_channel_close(channel);
  ssh_channel_free(channel);
}

void SshService::executeCommand(void* rawChannel, const char* command) {
  auto channel = static_cast<ssh_channel>(rawChannel);
  if (strcmp(command, "help") == 0) {
    write(channel, "help\r\nstatus\r\nlogs\r\nwifi\r\nreboot\r\n");
  } else if (strcmp(command, "status") == 0) {
    const String ip = wifi_.ipAddress().toString();
    char status[160] = {};
    snprintf(status, sizeof(status), "wifi=%s ip=%s rssi=%ld ssh=%s\r\n",
             wifi_.stateName(), ip.c_str(), static_cast<long>(wifi_.rssi()),
             ready_ ? "ready" : "starting");
    write(channel, status);
  } else if (strcmp(command, "wifi") == 0) {
    write(channel, wifi_.stateName());
    write(channel, "\r\n");
  } else if (strcmp(command, "logs") == 0) {
    writeLogs(channel);
  } else if (strcmp(command, "reboot") == 0) {
    write(channel, "restarting\r\n");
    delay(100);
    ESP.restart();
  } else if (command[0] != '\0') {
    write(channel, "unknown command\r\n");
  }
}

void SshService::write(void* rawChannel, const char* text) {
  if (text == nullptr) {
    return;
  }
  auto channel = static_cast<ssh_channel>(rawChannel);
  ssh_channel_write(channel, text, strlen(text));
}

void SshService::writeLogs(void* rawChannel) {
  auto channel = static_cast<ssh_channel>(rawChannel);
  LogEntry entries[64] = {};
  const size_t count = logger_.copy(entries, 64);
  char line[144] = {};
  for (size_t index = 0; index < count; ++index) {
    snprintf(line, sizeof(line), "%lu %s %s\r\n",
             static_cast<unsigned long>(entries[index].timestampMs / 1000),
             entries[index].level == LogLevel::Error
                 ? "ERROR"
                 : entries[index].level == LogLevel::Warning ? "WARN" : "INFO",
             entries[index].message);
    write(channel, line);
  }
}

}  // namespace nova
