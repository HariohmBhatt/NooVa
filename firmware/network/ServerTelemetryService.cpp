#include "ServerTelemetryService.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <cstdio>
#include <cstring>

#include "HubTrustAnchor.h"

namespace nova {
namespace {

constexpr char kHubHost[] = "nova-hub.local";
constexpr uint16_t kHubPort = 443;
constexpr char kTelemetryPath[] = "/v1/telemetry";
constexpr int16_t kMetricUnavailable = -1;

}  // namespace

ServerTelemetryService::ServerTelemetryService(Logger& logger, WifiService& wifi)
    : logger_(logger), wifi_(wifi) {}

bool ServerTelemetryService::begin() {
  if (kHubRootCa[0] == '\0') {
    setError("hub CA trust anchor missing");
    return true;
  }
  ready_ = true;
  state_ = ServerTelemetryState::Offline;
  return true;
}

void ServerTelemetryService::update() {
  if (!ready_) {
    return;
  }
  if (wifi_.state() != WifiState::Connected) {
    state_ = ServerTelemetryState::Offline;
    return;
  }

  const uint32_t now = millis();
  if (lastPollAt_ != 0 && now - lastPollAt_ < kPollPeriodMs) {
    if (snapshot_.valid && now - snapshot_.receivedAtMs >= kStaleAfterMs) {
      state_ = ServerTelemetryState::Stale;
    }
    return;
  }
  lastPollAt_ = now;
  fetch(now);
}

const ServerTelemetrySnapshot& ServerTelemetryService::snapshot() const {
  return snapshot_;
}

ServerTelemetryState ServerTelemetryService::state() const { return state_; }

const char* ServerTelemetryService::stateName() const {
  switch (state_) {
    case ServerTelemetryState::Unconfigured:
      return "UNCONFIGURED";
    case ServerTelemetryState::Offline:
      return "OFFLINE";
    case ServerTelemetryState::Live:
      return "LIVE";
    case ServerTelemetryState::Degraded:
      return "DEGRADED";
    case ServerTelemetryState::Stale:
      return "STALE";
    case ServerTelemetryState::Error:
      return "ERROR";
  }
  return "UNKNOWN";
}

bool ServerTelemetryService::fetch(uint32_t now) {
  WiFiClientSecure tls;
  tls.setCACert(kHubRootCa);
  HTTPClient http;
  char url[128] = {};
  snprintf(url, sizeof(url), "https://%s:%u%s", kHubHost, kHubPort,
           kTelemetryPath);
  if (!http.begin(tls, url)) {
    setError("telemetry HTTPS setup failed");
    return false;
  }
  http.setTimeout(kRequestTimeoutMs);
  const int responseCode = http.GET();
  if (responseCode != HTTP_CODE_OK) {
    http.end();
    setError("telemetry request failed");
    return false;
  }
  const bool parsed = parseResponse(http.getStream(), now);
  http.end();
  return parsed;
}

bool ServerTelemetryService::parseResponse(Stream& body, uint32_t now) {
  StaticJsonDocument<4096> response;
  const DeserializationError error = deserializeJson(response, body);
  if (error) {
    setError("telemetry response invalid");
    return false;
  }

  const JsonObjectConst payload = response["payload"].as<JsonObjectConst>();
  if (payload.isNull()) {
    setError("telemetry payload missing");
    return false;
  }
  snapshot_.cpuPercent = payload["cpu_percent"] | -1.0F;
  snapshot_.gpuAvailable =
      strcmp(payload["gpu_state"] | "unavailable", "available") == 0;
  snapshot_.gpuUtilizationPercent =
      payload["gpu_utilization_percent"] | -1.0F;
  snapshot_.gpuTemperatureC = payload["gpu_temperature_c"] | -1.0F;
  snapshot_.gpuVramUsedBytes = payload["gpu_vram_used_bytes"] | 0ULL;
  snapshot_.gpuVramTotalBytes = payload["gpu_vram_total_bytes"] | 0ULL;
  copyText(snapshot_.gpuName, sizeof(snapshot_.gpuName),
           payload["gpu_name"] | "");
  copyText(snapshot_.dependencyStatus, sizeof(snapshot_.dependencyStatus),
           payload["dependency_status"] | "degraded");
  snapshot_.valid = true;
  snapshot_.receivedAtMs = now;
  snapshot_.error[0] = '\0';
  state_ = strcmp(snapshot_.dependencyStatus, "healthy") == 0
               ? ServerTelemetryState::Live
               : ServerTelemetryState::Degraded;
  logger_.writef(LogLevel::Debug,
                 "Server telemetry CPU=%.1f GPU=%.1f%% GPU temp=%.1f C",
                 snapshot_.cpuPercent, snapshot_.gpuUtilizationPercent,
                 snapshot_.gpuTemperatureC);
  return true;
}

void ServerTelemetryService::setError(const char* message) {
  copyText(snapshot_.error, sizeof(snapshot_.error), message);
  state_ = snapshot_.valid ? ServerTelemetryState::Stale
                           : ServerTelemetryState::Error;
  logger_.writef(LogLevel::Warning, "Telemetry: %s",
                 message == nullptr ? "request error" : message);
}

void ServerTelemetryService::copyText(char* destination, size_t capacity,
                                      const char* source) {
  if (destination == nullptr || capacity == 0) {
    return;
  }
  snprintf(destination, capacity, "%s", source == nullptr ? "" : source);
}

}  // namespace nova
