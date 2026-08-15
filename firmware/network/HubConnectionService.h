#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WebSocketsClient.h>

#include <cstddef>
#include <cstdint>

#include "../core/Logger.h"
#include "WifiService.h"

namespace nova {

class DeviceTelemetryCollector;

/** Connection and freshness states shown by the terminal dashboard. */
enum class HubState : uint8_t {
  Unconfigured,
  Discovering,
  Registering,
  Connecting,
  Live,
  Stale,
  // Retained for source compatibility; health grade is no longer a HubState.
  Degraded,
  Offline,
  UpdateRequired,
  Error,
};

/** Graded health reported by the hub independently of transport freshness. */
enum class HealthGrade : uint8_t {
  Normal,
  Warning,
  Critical,
};

/** Return the protocol name for a health grade. */
const char* healthGradeName(HealthGrade grade);

/** Bounded server metric trends carried by a health snapshot. */
struct HubHealthTrends {
  static constexpr size_t kMaxPoints = 16;

  uint32_t periodSeconds = 0;
  uint8_t cpuPointCount = 0;
  uint8_t memoryPointCount = 0;
  uint8_t diskPointCount = 0;
  float cpuPercent[kMaxPoints] = {};
  float memoryUsedPercent[kMaxPoints] = {};
  float diskUsedPercent[kMaxPoints] = {};
};

/** One bounded active alert carried by a server health snapshot. */
struct HubActiveAlert {
  char metric[32] = {};
  HealthGrade state = HealthGrade::Warning;
  float value = 0.0F;
};

/** Latest server health values rendered by the terminal dashboard. */
struct HubHealthSnapshot {
  bool valid = false;
  HealthGrade healthGrade = HealthGrade::Warning;
  char dependencyStatus[12] = {};
  char serverVersion[24] = {};
  char serverTime[32] = {};
  char hubApiStatus[12] = {};
  char metricsStatus[12] = {};
  char networkInterface[16] = {};
  float cpuPercent = 0.0F;
  uint64_t memoryUsedBytes = 0;
  uint64_t memoryTotalBytes = 0;
  uint64_t diskUsedBytes = 0;
  uint64_t diskTotalBytes = 0;
  float networkRxBytesPerSecond = 0.0F;
  float networkTxBytesPerSecond = 0.0F;
  uint64_t networkRxBytesTotal = 0;
  uint64_t networkTxBytesTotal = 0;
  float uptimeSeconds = 0.0F;
  uint32_t latencyMs = 0;
  uint32_t receivedAtMs = 0;
  HubHealthTrends trends = {};
  static constexpr size_t kMaxActiveAlerts = 4;
  uint8_t activeAlertCount = 0;
  HubActiveAlert activeAlerts[kMaxActiveAlerts] = {};
  char error[64] = {};
};

class HubConnectionService {
 public:
  static constexpr uint16_t kDefaultPort = 443;

  /** Construct the local terminal connection without upstream telemetry. */
  HubConnectionService(Logger& logger, WifiService& wifi);

  /** Construct the connection with a collector that outlives this service. */
  HubConnectionService(Logger& logger, WifiService& wifi,
                        DeviceTelemetryCollector& telemetry);

  /** Load registration state and prepare the fail-closed TLS client. */
  bool begin();

  /** Advance discovery, registration, WebSocket, and freshness state. */
  void update();

  /** Set the manual fallback endpoint for automatic registration. */
  bool configureEndpoint(const char* host, uint16_t port = kDefaultPort,
                         bool persist = true);

  /** Register this installation with the hub without an operator credential. */
  bool registerDevice();

  /** Erase only hub identity state, preserving Wi-Fi credentials. */
  void clearRegistration();

  /** Return whether a hub device identity is stored. */
  bool isRegistered() const;

  /** Return the current connection state. */
  HubState state() const;

  /** Return a concise state name suitable for the display. */
  const char* stateName() const;

  /** Return the configured or discovered hub hostname. */
  const char* host() const;

  /** Return the immutable server-assigned device identifier. */
  const char* deviceId() const;

  /** Return the latest health snapshot owned by this service. */
  const HubHealthSnapshot& health() const;

  /** Return whether the CA trust anchor was supplied at build time. */
  bool hasTrustAnchor() const;

 private:
  static constexpr size_t kHostLength = 64;
  static constexpr size_t kDeviceIdLength = 48;
  static constexpr size_t kNonceLength = 48;
  static constexpr uint32_t kDiscoveryPeriodMs = 10000;
  static constexpr uint32_t kRegistrationPeriodMs = 10000;
  static constexpr uint32_t kStaleAfterMs = 15000;
  static constexpr uint32_t kRegistrationTimeoutMs = 10000;

  static void handleSocketEvent(HubConnectionService* service, WStype_t type,
                                uint8_t* payload, size_t length);

  bool loadSettings();
  bool ensureInstallationNonce();
  void startClockSync();
  bool clockSynchronized() const;
  bool discoverHub(uint32_t now);
  bool startSocket();
  bool sendHello();
  bool sendPing();
  bool sendDeviceMetrics();
  bool handleMessage(const uint8_t* payload, size_t length);
  bool handleHealth(JsonObjectConst payload, const char* timestamp);
  bool telemetryWasAcknowledged(JsonObjectConst payload) const;
  void eraseRegistrationIdentity();
  void updateFreshness(uint32_t now);
  void setError(const char* message);
  void copyText(char* destination, size_t capacity, const char* source);

  Logger& logger_;
  WifiService& wifi_;
  Preferences preferences_;
  WebSocketsClient websocket_;
  char host_[kHostLength] = "nova-hub.local";
  char connectionHost_[kHostLength] = "nova-hub.local";
  uint16_t port_ = kDefaultPort;
  char deviceId_[kDeviceIdLength] = {};
  char installationNonce_[kNonceLength] = {};
  HubHealthSnapshot health_ = {};
  HubState state_ = HubState::Unconfigured;
  uint32_t lastDiscoveryAt_ = 0;
  uint32_t lastRegistrationAt_ = 0;
  uint32_t socketStartedAt_ = 0;
  uint32_t lastPingAt_ = 0;
  uint32_t lastTelemetryAt_ = 0;
  bool preferencesReady_ = false;
  bool socketStarted_ = false;
  bool socketConnected_ = false;
  bool sessionReady_ = false;
  bool telemetryAcknowledged_ = false;
  bool discoveryReady_ = false;
  bool mdnsStarted_ = false;
  bool timeSyncStarted_ = false;
  bool registrationComplete_ = false;
  bool clockWaitingLogged_ = false;
  bool clockReadyLogged_ = false;
  bool servicePaused_ = false;
  DeviceTelemetryCollector* telemetryCollector_ = nullptr;
};

}  // namespace nova
