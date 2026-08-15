#include "HubConnectionService.h"

#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_system.h>

#include <cstdio>
#include <ctime>

#include "HubTrustAnchor.h"
#include "../telemetry/DeviceTelemetryCollector.h"

namespace nova {
namespace {

constexpr char kDefaultHost[] = "nova-hub.local";
constexpr char kRegistrationPath[] = "/v1/register";
constexpr char kWebSocketPath[] = "/v1/ws";
constexpr char kServiceName[] = "nova-hub";
constexpr char kPreferenceHost[] = "hub_host";
constexpr char kPreferencePort[] = "hub_port";
constexpr char kPreferenceDevice[] = "hub_device";
constexpr char kLegacyPreferenceToken[] = "hub_token";
constexpr char kPreferenceNonce[] = "hub_nonce";
constexpr char kFirmwareVersion[] = "0.1.0";
constexpr char kDeviceMetricsCapability[] = "device_metrics_v1";
constexpr size_t kRegistrationDocumentCapacity = 512;
constexpr size_t kRegistrationBodyCapacity = 512;
constexpr size_t kDeviceMetricsDocumentCapacity = 768;
constexpr size_t kDeviceMetricsBodyCapacity = 768;
constexpr uint32_t kPingPeriodMs = 10000;

bool hasText(const char* value) { return value != nullptr && value[0] != '\0'; }

void writeTimestamp(char* output, size_t capacity) {
  const time_t now = time(nullptr);
  if (now < 86400) {
    snprintf(output, capacity, "1970-01-01T00:00:00Z");
    return;
  }
  struct tm utc = {};
  gmtime_r(&now, &utc);
  snprintf(output, capacity, "%04d-%02d-%02dT%02d:%02d:%02dZ",
           utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour,
           utc.tm_min, utc.tm_sec);
}

bool hasCapability(JsonArrayConst capabilities, const char* capability) {
  for (JsonVariantConst item : capabilities) {
    if (strcmp(item | "", capability) == 0) {
      return true;
    }
  }
  return false;
}

HealthGrade parseHealthGrade(const char* value, const char* dependencyStatus) {
  if (strcmp(value, "normal") == 0) {
    return HealthGrade::Normal;
  }
  if (strcmp(value, "critical") == 0) {
    return HealthGrade::Critical;
  }
  if (strcmp(value, "warning") == 0) {
    return HealthGrade::Warning;
  }
  return strcmp(dependencyStatus, "healthy") == 0 ? HealthGrade::Normal
                                                   : HealthGrade::Warning;
}

uint8_t copyTrend(JsonArrayConst values, float* destination, size_t capacity) {
  const size_t count = values.size() < capacity ? values.size() : capacity;
  for (size_t index = 0; index < count; ++index) {
    destination[index] = values[index] | 0.0F;
  }
  return static_cast<uint8_t>(count);
}

}  // namespace

const char* healthGradeName(HealthGrade grade) {
  switch (grade) {
    case HealthGrade::Normal:
      return "normal";
    case HealthGrade::Warning:
      return "warning";
    case HealthGrade::Critical:
      return "critical";
  }
  return "warning";
}

HubConnectionService::HubConnectionService(Logger& logger, WifiService& wifi)
    : logger_(logger), wifi_(wifi) {}

HubConnectionService::HubConnectionService(Logger& logger, WifiService& wifi,
                                           DeviceTelemetryCollector& telemetry)
    : logger_(logger), wifi_(wifi), telemetryCollector_(&telemetry) {}

bool HubConnectionService::begin() {
  preferencesReady_ = preferences_.begin("nova", false);
  if (!preferencesReady_ || !loadSettings() || !ensureInstallationNonce()) {
    state_ = HubState::Error;
    setError("hub settings unavailable");
    return false;
  }

  if (!hasTrustAnchor()) {
    state_ = HubState::Error;
    setError("hub CA trust anchor missing");
    logger_.write(LogLevel::Warning,
                  "Hub connection disabled until a CA trust anchor is generated");
    return true;
  }

  logger_.writef(
      LogLevel::Info,
      "[HUB] settings host=%s port=%u prefs=%d registered=%d id-len=%u",
      host_, static_cast<unsigned>(port_), preferencesReady_, isRegistered(),
      static_cast<unsigned>(strlen(deviceId_)));
  state_ = isRegistered() ? HubState::Offline : HubState::Discovering;
  return true;
}

void HubConnectionService::update() {
  if (!hasTrustAnchor()) {
    return;
  }
  if (servicePaused_ || wifi_.state() != WifiState::Connected) {
    if (socketStarted_) {
      websocket_.disconnect();
      socketStarted_ = false;
      socketConnected_ = false;
      sessionReady_ = false;
      telemetryAcknowledged_ = false;
    }
    if (isRegistered()) {
      state_ = HubState::Offline;
    }
    return;
  }

  const uint32_t now = millis();
  if (!timeSyncStarted_) {
    startClockSync();
  }
  if (!clockSynchronized()) {
    if (!clockWaitingLogged_) {
      logger_.write(LogLevel::Info, "[HUB] waiting for NTP before TLS");
      clockWaitingLogged_ = true;
    }
    state_ = isRegistered() ? HubState::Connecting : HubState::Discovering;
    return;
  }
  if (!clockReadyLogged_) {
    logger_.writef(LogLevel::Info, "[HUB] NTP ready unix=%lu registered=%d",
                   static_cast<unsigned long>(time(nullptr)), isRegistered());
    clockReadyLogged_ = true;
  }
  if (!discoveryReady_ && now - lastDiscoveryAt_ >= kDiscoveryPeriodMs) {
    discoverHub(now);
  }
  if (!registrationComplete_ && discoveryReady_ &&
      now - lastRegistrationAt_ >= kRegistrationPeriodMs) {
    registerDevice();
  }
  if (isRegistered() && !socketStarted_) {
    startSocket();
  }
  if (socketStarted_) {
    websocket_.loop();
    if (sessionReady_ && now - lastPingAt_ >= kPingPeriodMs) {
      sendPing();
    }
    if (sessionReady_ && telemetryAcknowledged_ &&
        now - lastTelemetryAt_ >= DeviceTelemetryCollector::kCadenceMs) {
      sendDeviceMetrics();
    }
  }
  updateFreshness(now);
}

bool HubConnectionService::configureEndpoint(const char* host, uint16_t port,
                                              bool persist) {
  if (!hasText(host) || port == 0) {
    logger_.writef(LogLevel::Error, "[HUB] endpoint rejected host=%d port=%u",
                   hasText(host), static_cast<unsigned>(port));
    return false;
  }
  copyText(host_, sizeof(host_), host);
  copyText(connectionHost_, sizeof(connectionHost_), host);
  port_ = port;
  discoveryReady_ = true;
  if (persist && preferencesReady_) {
    preferences_.putString(kPreferenceHost, host_);
    preferences_.putUShort(kPreferencePort, port_);
  }
  logger_.writef(LogLevel::Debug, "[HUB] endpoint host=%s port=%u persisted=%d",
                 host_, static_cast<unsigned>(port_), persist);
  return true;
}

bool HubConnectionService::registerDevice() {
  logger_.writef(
      LogLevel::Info,
      "[REGISTER] start host=%s connect=%s:%u wifi=%s ca=%d clock=%d",
      host_, connectionHost_, static_cast<unsigned>(port_), wifi_.stateName(),
      hasTrustAnchor(), clockSynchronized());
  if (!hasTrustAnchor() || wifi_.state() != WifiState::Connected) {
    setError("registration preflight failed");
    return false;
  }
  if (!clockSynchronized()) {
    setError("clock not synchronized for TLS");
    return false;
  }

  state_ = HubState::Registering;
  lastRegistrationAt_ = millis();
  WiFiClientSecure tls;
  tls.setCACert(kHubRootCa);
  tls.setTimeout(kRegistrationTimeoutMs / 1000);
  HTTPClient http;
  char url[128] = {};
  snprintf(url, sizeof(url), "https://%s:%u%s", connectionHost_, port_,
           kRegistrationPath);
  if (!http.begin(tls, url)) {
    setError("registration HTTPS setup failed");
    return false;
  }

  int responseCode;
  {
    StaticJsonDocument<kRegistrationDocumentCapacity> request;
    request["protocol_version"] = 1;
    request["installation_nonce"] = installationNonce_;
    request["firmware"] = kFirmwareVersion;
    JsonArray capabilities = request["capabilities"].to<JsonArray>();
    capabilities.add("touch");
    capabilities.add("display");
    capabilities.add("microphone");
    capabilities.add("speaker");
    capabilities.add(kDeviceMetricsCapability);
    char body[kRegistrationBodyCapacity] = {};
    serializeJson(request, body, sizeof(body));
    http.addHeader("Content-Type", "application/json");
    responseCode = http.POST(reinterpret_cast<uint8_t*>(body), strlen(body));
  }
  logger_.writef(LogLevel::Info, "[REGISTER] HTTP status=%d", responseCode);
  if (responseCode != HTTP_CODE_OK) {
    http.end();
    char error[48] = {};
    snprintf(error, sizeof(error), "registration HTTP status %d", responseCode);
    setError(error);
    return false;
  }

  StaticJsonDocument<kRegistrationDocumentCapacity> response;
  const int responseSize = http.getSize();
  const DeserializationError error = deserializeJson(response, http.getStream());
  logger_.writef(LogLevel::Debug, "[REGISTER] HTTP response-bytes=%d json=%s",
                 responseSize, error.c_str());
  http.end();
  if (error) {
    setError("registration response invalid");
    return false;
  }

  const char* device = response["device_id"] | "";
  const char* hubHost = response["hub_host"] | host_;
  const uint16_t hubPort = response["hub_port"] | port_;
  if ((response["protocol_version"] | 0) != 1) {
    state_ = HubState::UpdateRequired;
    setError("registration protocol version unsupported");
    return false;
  }
  if (!hasText(device)) {
    setError("registration response incomplete");
    return false;
  }
  logger_.writef(LogLevel::Info,
                 "[REGISTER] response protocol=%u device-len=%u hub=%s:%u",
                 static_cast<unsigned>(response["protocol_version"] | 0),
                 static_cast<unsigned>(strlen(device)), hubHost,
                 static_cast<unsigned>(hubPort));
  copyText(deviceId_, sizeof(deviceId_), device);
  configureEndpoint(hubHost, hubPort, true);
  if (preferencesReady_) {
    const bool deviceStored = preferences_.putString(kPreferenceDevice, deviceId_) > 0;
    const bool nonceStored =
        preferences_.putString(kPreferenceNonce, installationNonce_) > 0;
    logger_.writef(LogLevel::Debug,
                   "[REGISTER] identity persistence device=%d nonce=%d",
                   deviceStored, nonceStored);
  }
  state_ = HubState::Offline;
  health_ = {};
  registrationComplete_ = true;
  logger_.write(LogLevel::Info, "Hub registration completed");
  return true;
}

void HubConnectionService::clearRegistration() {
  if (socketStarted_) {
    websocket_.disconnect();
  }
  socketStarted_ = false;
  socketConnected_ = false;
  sessionReady_ = false;
  telemetryAcknowledged_ = false;
  registrationComplete_ = false;
  eraseRegistrationIdentity();
  health_ = {};
  state_ = HubState::Unconfigured;
}

void HubConnectionService::eraseRegistrationIdentity() {
  deviceId_[0] = '\0';
  if (preferencesReady_) {
    preferences_.remove(kPreferenceDevice);
    preferences_.remove(kLegacyPreferenceToken);
  }
}

bool HubConnectionService::isRegistered() const {
  return hasText(deviceId_);
}

HubState HubConnectionService::state() const { return state_; }

const char* HubConnectionService::stateName() const {
  switch (state_) {
    case HubState::Unconfigured:
      return "SETUP REQUIRED";
    case HubState::Discovering:
      return "DISCOVERING";
    case HubState::Registering:
      return "REGISTERING";
    case HubState::Connecting:
      return "CONNECTING";
    case HubState::Live:
      return "LIVE";
    case HubState::Stale:
      return "STALE";
    case HubState::Degraded:
      return "DEGRADED";
    case HubState::Offline:
      return "OFFLINE";
    case HubState::UpdateRequired:
      return "UPDATE REQUIRED";
    case HubState::Error:
      return "ERROR";
  }
  return "UNKNOWN";
}

const char* HubConnectionService::host() const { return host_; }

const char* HubConnectionService::deviceId() const { return deviceId_; }

const HubHealthSnapshot& HubConnectionService::health() const { return health_; }

bool HubConnectionService::hasTrustAnchor() const {
  return kHubRootCa[0] != '\0';
}

void HubConnectionService::handleSocketEvent(HubConnectionService* service,
                                              WStype_t type, uint8_t* payload,
                                              size_t length) {
  if (service == nullptr) {
    return;
  }
  switch (type) {
    case WStype_CONNECTED:
      service->socketConnected_ = true;
      service->sessionReady_ = false;
      service->telemetryAcknowledged_ = false;
      service->state_ = HubState::Connecting;
      service->logger_.write(LogLevel::Info, "[HUB] WSS connected; sending hello");
      service->sendHello();
      break;
    case WStype_TEXT:
      service->handleMessage(payload, length);
      break;
    case WStype_DISCONNECTED:
    case WStype_ERROR:
      service->logger_.writef(LogLevel::Warning, "[HUB] WSS event=%u",
                              static_cast<unsigned>(type));
      service->socketConnected_ = false;
      service->sessionReady_ = false;
      service->telemetryAcknowledged_ = false;
      if (service->isRegistered() &&
          service->state_ != HubState::UpdateRequired) {
        service->state_ = HubState::Offline;
      }
      break;
    default:
      break;
  }
}

bool HubConnectionService::loadSettings() {
  if (!preferencesReady_) {
    return false;
  }
  if (preferences_.isKey(kPreferenceHost)) {
    preferences_.getString(kPreferenceHost, host_, sizeof(host_));
  }
  port_ = preferences_.getUShort(kPreferencePort, kDefaultPort);
  if (preferences_.isKey(kPreferenceDevice)) {
    preferences_.getString(kPreferenceDevice, deviceId_, sizeof(deviceId_));
  }
  if (preferences_.isKey(kPreferenceNonce)) {
    preferences_.getString(kPreferenceNonce, installationNonce_,
                           sizeof(installationNonce_));
  }
  host_[sizeof(host_) - 1] = '\0';
  deviceId_[sizeof(deviceId_) - 1] = '\0';
  installationNonce_[sizeof(installationNonce_) - 1] = '\0';
  if (!hasText(host_)) {
    copyText(host_, sizeof(host_), kDefaultHost);
  }
  copyText(connectionHost_, sizeof(connectionHost_), host_);
  return true;
}

bool HubConnectionService::ensureInstallationNonce() {
  if (hasText(installationNonce_)) {
    return true;
  }
  snprintf(installationNonce_, sizeof(installationNonce_), "%08lx%08lx%08lx",
           static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(esp_random()));
  return !preferencesReady_ ||
         preferences_.putString(kPreferenceNonce, installationNonce_) > 0;
}

void HubConnectionService::startClockSync() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  timeSyncStarted_ = true;
  logger_.write(LogLevel::Info, "[HUB] NTP sync requested");
}

bool HubConnectionService::clockSynchronized() const {
  return time(nullptr) >= 1704067200;
}

bool HubConnectionService::discoverHub(uint32_t now) {
  lastDiscoveryAt_ = now;
  if (!mdnsStarted_ && !MDNS.begin("nova-terminal")) {
    return false;
  }
  mdnsStarted_ = true;
  const int count = MDNS.queryService(kServiceName, "tcp");
  if (count <= 0) {
    state_ = isRegistered() ? HubState::Offline : HubState::Discovering;
    return false;
  }
  if (!isRegistered()) {
    configureEndpoint(kDefaultHost, MDNS.port(0), false);
  }
  copyText(connectionHost_, sizeof(connectionHost_), host_);
  discoveryReady_ = true;
  logger_.writef(LogLevel::Info, "Hub discovered as %s:%u", connectionHost_,
                 static_cast<unsigned>(MDNS.port(0)));
  return true;
}

bool HubConnectionService::startSocket() {
  if (!hasTrustAnchor() || !isRegistered() || connectionHost_[0] == '\0') {
    logger_.writef(LogLevel::Error,
                   "[HUB] WSS preflight ca=%d registered=%d host-present=%d",
                   hasTrustAnchor(), isRegistered(), connectionHost_[0] != '\0');
    return false;
  }
  logger_.writef(LogLevel::Info, "[HUB] WSS start host=%s port=%u", connectionHost_,
                 static_cast<unsigned>(port_));
  websocket_.beginSslWithCA(connectionHost_, port_, kWebSocketPath, kHubRootCa,
                             "nova.v1");
  websocket_.onEvent([this](WStype_t type, uint8_t* payload, size_t length) {
    handleSocketEvent(this, type, payload, length);
  });
  websocket_.setReconnectInterval(5000);
  websocket_.enableHeartbeat(15000, 5000, 2);
  socketStarted_ = true;
  socketStartedAt_ = millis();
  lastPingAt_ = socketStartedAt_;
  state_ = HubState::Connecting;
  return true;
}

bool HubConnectionService::sendHello() {
  StaticJsonDocument<1536> message;
  message["protocol_version"] = 1;
  message["type"] = "device.hello";
  char timestamp[32] = {};
  writeTimestamp(timestamp, sizeof(timestamp));
  message["timestamp"] = timestamp;
  JsonObject payload = message["payload"].to<JsonObject>();
  payload["device_id"] = deviceId_;
  payload["installation_nonce"] = installationNonce_;
  payload["firmware"] = kFirmwareVersion;
  JsonArray capabilities = payload["capabilities"].to<JsonArray>();
  capabilities.add("touch");
  capabilities.add("display");
  capabilities.add("microphone");
  capabilities.add("speaker");
  capabilities.add(kDeviceMetricsCapability);
  char body[1536] = {};
  serializeJson(message, body, sizeof(body));
  const bool sent = websocket_.sendTXT(body);
  logger_.writef(LogLevel::Info, "[HUB] hello sent=%d", sent);
  return sent;
}

bool HubConnectionService::sendDeviceMetrics() {
  if (telemetryCollector_ == nullptr || !telemetryAcknowledged_ ||
      !sessionReady_ || !socketConnected_) {
    return false;
  }
  StaticJsonDocument<kDeviceMetricsDocumentCapacity> message;
  message["protocol_version"] = 1;
  message["type"] = "device.metrics";
  char timestamp[32] = {};
  writeTimestamp(timestamp, sizeof(timestamp));
  message["timestamp"] = timestamp;
  DeviceTelemetrySnapshot sample;
  telemetryCollector_->collect(sample);
  JsonObject payload = message["payload"].to<JsonObject>();
  payload["sequence"] = sample.sequence;
  payload["uptime_seconds"] = sample.uptimeSeconds;
  payload["wifi_rssi_dbm"] = sample.wifiRssiDbm;
  payload["free_heap_bytes"] = sample.freeHeapBytes;
  payload["touch_ready"] = sample.touchReady;
  payload["touch_active"] = sample.touchActive;
  payload["audio_state"] = sample.audioState;
  char body[kDeviceMetricsBodyCapacity] = {};
  if (measureJson(message) >= sizeof(body)) {
    logger_.write(LogLevel::Error, "[HUB] device metrics frame too large");
    return false;
  }
  serializeJson(message, body, sizeof(body));
  lastTelemetryAt_ = millis();
  const bool sent = websocket_.sendTXT(body);
  logger_.writef(LogLevel::Debug, "[HUB] device metrics sequence=%lu sent=%d",
                 static_cast<unsigned long>(sample.sequence), sent);
  return sent;
}

bool HubConnectionService::sendPing() {
  StaticJsonDocument<256> message;
  message["protocol_version"] = 1;
  message["type"] = "device.ping";
  char timestamp[32] = {};
  writeTimestamp(timestamp, sizeof(timestamp));
  message["timestamp"] = timestamp;
  message.createNestedObject("payload");
  char body[256] = {};
  serializeJson(message, body, sizeof(body));
  lastPingAt_ = millis();
  return websocket_.sendTXT(body);
}

bool HubConnectionService::handleMessage(const uint8_t* payload, size_t length) {
  StaticJsonDocument<4096> message;
  if (deserializeJson(message, payload, length)) {
    setError("hub message invalid");
    return false;
  }
  const char* type = message["type"] | "";
  const int protocolVersion = message["protocol_version"] | 0;
  if (protocolVersion != 1) {
    state_ = HubState::UpdateRequired;
    return false;
  }
  if (strcmp(type, "session.ready") == 0) {
    sessionReady_ = true;
    const JsonObjectConst readyPayload =
        message["payload"].as<JsonObjectConst>();
    telemetryAcknowledged_ = telemetryCollector_ != nullptr &&
                             telemetryWasAcknowledged(readyPayload);
    lastTelemetryAt_ = millis() - DeviceTelemetryCollector::kCadenceMs;
    logger_.writef(LogLevel::Info, "[HUB] session ready device-metrics=%d",
                   telemetryAcknowledged_);
    return true;
  }
  if (strcmp(type, "health.snapshot") == 0) {
    const char* timestamp = message["timestamp"] | "";
    return handleHealth(message["payload"].as<JsonObjectConst>(), timestamp);
  }
  if (strcmp(type, "server.pong") == 0) {
    health_.latencyMs = millis() - lastPingAt_;
    return true;
  }
  if (strcmp(type, "protocol.error") == 0) {
    const char* code = message["payload"]["code"] | "";
    logger_.writef(LogLevel::Warning, "[HUB] protocol error=%s", code);
    if (strcmp(code, "incompatible_version") == 0) {
      state_ = HubState::UpdateRequired;
    }
  }
  return true;
}

bool HubConnectionService::telemetryWasAcknowledged(
    JsonObjectConst payload) const {
  if ((payload["device_metrics_v1"] | false) ||
      (payload["device_metrics"] | false)) {
    return true;
  }
  const JsonObjectConst telemetry = payload["telemetry"].as<JsonObjectConst>();
  if (telemetry["device_metrics_v1"] | false) {
    return true;
  }
  return hasCapability(payload["capabilities"].as<JsonArrayConst>(),
                       kDeviceMetricsCapability) ||
         hasCapability(payload["accepted_capabilities"].as<JsonArrayConst>(),
                       kDeviceMetricsCapability);
}

bool HubConnectionService::handleHealth(JsonObjectConst payload,
                                        const char* timestamp) {
  health_.valid = true;
  copyText(health_.dependencyStatus, sizeof(health_.dependencyStatus),
           payload["dependency_status"] | "degraded");
  health_.healthGrade = parseHealthGrade(payload["health_grade"] | "",
                                          health_.dependencyStatus);
  copyText(health_.serverVersion, sizeof(health_.serverVersion),
           payload["server_version"] | "unknown");
  copyText(health_.serverTime, sizeof(health_.serverTime),
           payload["home_time"] | timestamp);
  copyText(health_.hubApiStatus, sizeof(health_.hubApiStatus),
           payload["service_status"]["hub_api"] | "unknown");
  copyText(health_.metricsStatus, sizeof(health_.metricsStatus),
           payload["service_status"]["metrics"] | "unknown");
  copyText(health_.networkInterface, sizeof(health_.networkInterface),
           payload["network_interface"] | "unknown");
  health_.cpuPercent = payload["cpu_percent"] | 0.0F;
  health_.memoryUsedBytes = payload["memory_used_bytes"] | 0ULL;
  health_.memoryTotalBytes = payload["memory_total_bytes"] | 0ULL;
  health_.diskUsedBytes = payload["disk_used_bytes"] | 0ULL;
  health_.diskTotalBytes = payload["disk_total_bytes"] | 0ULL;
  health_.networkRxBytesPerSecond =
      payload["network_rx_bytes_per_second"] | 0.0F;
  health_.networkTxBytesPerSecond =
      payload["network_tx_bytes_per_second"] | 0.0F;
  health_.networkRxBytesTotal = payload["network_rx_bytes_total"] | 0ULL;
  health_.networkTxBytesTotal = payload["network_tx_bytes_total"] | 0ULL;
  health_.uptimeSeconds = payload["uptime_seconds"] | 0.0F;
  health_.receivedAtMs = millis();
  health_.error[0] = '\0';
  if (payload["collection_errors"].size() > 0) {
    copyText(health_.error, sizeof(health_.error), "host metric collection degraded");
  }
  const JsonObjectConst trends = payload["trends"].as<JsonObjectConst>();
  health_.trends.periodSeconds = trends["period_seconds"] | 0U;
  health_.trends.cpuPointCount = copyTrend(
      trends["cpu_percent"].as<JsonArrayConst>(), health_.trends.cpuPercent,
      HubHealthTrends::kMaxPoints);
  health_.trends.memoryPointCount = copyTrend(
      trends["memory_used_percent"].as<JsonArrayConst>(),
      health_.trends.memoryUsedPercent, HubHealthTrends::kMaxPoints);
  health_.trends.diskPointCount = copyTrend(
      trends["disk_used_percent"].as<JsonArrayConst>(), health_.trends.diskUsedPercent,
      HubHealthTrends::kMaxPoints);
  if (socketConnected_) {
    state_ = HubState::Live;
  }
  return true;
}

void HubConnectionService::updateFreshness(uint32_t now) {
  if (!isRegistered() || !health_.valid || !socketConnected_ ||
      state_ == HubState::UpdateRequired) {
    if (isRegistered() && !socketConnected_ &&
        state_ != HubState::UpdateRequired) {
      state_ = HubState::Offline;
    }
    return;
  }
  if (now - health_.receivedAtMs >= kStaleAfterMs) {
    state_ = HubState::Stale;
  }
}

void HubConnectionService::setError(const char* message) {
  copyText(health_.error, sizeof(health_.error), message);
  state_ = HubState::Error;
  logger_.writef(LogLevel::Error, "Hub: %s", message == nullptr ? "error" : message);
}

void HubConnectionService::copyText(char* destination, size_t capacity,
                                    const char* source) {
  if (destination == nullptr || capacity == 0) {
    return;
  }
  snprintf(destination, capacity, "%s", source == nullptr ? "" : source);
}

}  // namespace nova
