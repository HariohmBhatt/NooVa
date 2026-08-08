#include <Arduino.h>
#include <WiFi.h>

#include <cstring>

namespace {

#ifndef WIFI_TEST_SSID_HEX
#define WIFI_TEST_SSID_HEX ""
#endif

#ifndef WIFI_TEST_PASSWORD_HEX
#define WIFI_TEST_PASSWORD_HEX ""
#endif

#define WIFI_TEST_STRINGIFY_VALUE(value) #value
#define WIFI_TEST_STRINGIFY(value) WIFI_TEST_STRINGIFY_VALUE(value)

constexpr char kUnavailableSsid[] = "NOVA-HW-003-NOT-AN-AP";
constexpr char kUnavailablePassword[] = "not-a-real-network-password";
constexpr char kDnsHost[] = "example.com";
constexpr char kHttpHost[] = "example.com";
constexpr char kSoftApSsid[] = "NOVA-HW-003-AP";
constexpr char kSoftApPassword[] = "NovaHw003!";

constexpr uint32_t kSerialBaudRate = 115200;
constexpr uint32_t kSerialConnectTimeoutMs = 3000;
constexpr uint32_t kConnectTimeoutMs = 20000;
constexpr uint32_t kDisconnectTimeoutMs = 3000;
constexpr uint32_t kUnavailableTimeoutMs = 8000;
constexpr uint32_t kHttpTimeoutMs = 8000;
constexpr uint32_t kStatusReportIntervalMs = 10000;
constexpr uint8_t kReconnectCycleCount = 5;

bool gAutomatedChecksPassed = true;
bool gSoftApActive = false;
bool gSoftApPayloadPassed = false;
uint32_t gLastStatusReportAt = 0;
char gTargetSsid[64] = {};
char gTargetPassword[128] = {};
WiFiServer gSoftApServer(80);

int hexValue(char value) {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'A' && value <= 'F') {
    return value - 'A' + 10;
  }
  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10;
  }
  return -1;
}

size_t decodeHex(const char* encoded, char* decoded, size_t capacity) {
  const size_t encodedLength = std::strlen(encoded);
  if (encodedLength == 0 || encodedLength % 2 != 0 || encodedLength / 2 >= capacity) {
    return 0;
  }

  for (size_t index = 0; index < encodedLength; index += 2) {
    const int high = hexValue(encoded[index]);
    const int low = hexValue(encoded[index + 1]);
    if (high < 0 || low < 0) {
      decoded[0] = '\0';
      return 0;
    }
    decoded[index / 2] = static_cast<char>((high << 4) | low);
  }
  decoded[encodedLength / 2] = '\0';
  return encodedLength / 2;
}

void printResult(const char* check, bool passed) {
  Serial.printf("[HW-003][%s] %s\n", passed ? "PASS" : "FAIL", check);
  gAutomatedChecksPassed = gAutomatedChecksPassed && passed;
}

void printHeader() {
  Serial.println();
  Serial.println("[HW-003] WIFI_TEST_START");
  Serial.println("[HW-003] RADIO=ESP32-S3_2G4_STATION");
  Serial.printf("[HW-003] TARGET_SSID=%s\n", gTargetSsid[0] ? gTargetSsid : "NOT_INJECTED");
  Serial.printf("[HW-003] CREDENTIALS_PRESENT=%s\n",
                gTargetSsid[0] && gTargetPassword[0] ? "YES" : "NO");
  Serial.printf("[HW-003] RECONNECT_CYCLES=%u\n", kReconnectCycleCount);
  Serial.printf("[HW-003] DNS_HOST=%s HTTP_HOST=%s\n", kDnsHost, kHttpHost);
}

void printNetworkDetails() {
  Serial.printf("[HW-003] IP=%s SUBNET=%s GATEWAY=%s DNS=%s CHANNEL=%d RSSI=%d\n",
                WiFi.localIP().toString().c_str(),
                WiFi.subnetMask().toString().c_str(),
                WiFi.gatewayIP().toString().c_str(),
                WiFi.dnsIP().toString().c_str(), WiFi.channel(), WiFi.RSSI());
}

bool waitForDisconnected(uint32_t timeoutMs) {
  const uint32_t startedAt = millis();
  while (millis() - startedAt < timeoutMs) {
    if (WiFi.status() == WL_DISCONNECTED || WiFi.status() == WL_NO_SHIELD) {
      return true;
    }
    delay(50);
  }
  return WiFi.status() != WL_CONNECTED;
}

bool connectToTarget() {
  WiFi.begin(gTargetSsid, gTargetPassword);
  const uint32_t startedAt = millis();
  uint32_t lastProgressAt = startedAt;

  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < kConnectTimeoutMs) {
    if (millis() - lastProgressAt >= 1000) {
      lastProgressAt = millis();
      Serial.printf("[HW-003] CONNECT_WAIT STATUS=%d ELAPSED_MS=%lu\n",
                    WiFi.status(), lastProgressAt - startedAt);
    }
    delay(50);
  }

  const bool connected = WiFi.status() == WL_CONNECTED;
  printResult("STATION_CONNECTED", connected);
  if (connected) {
    printNetworkDetails();
  }
  return connected;
}

bool runNetworkScan() {
  Serial.println("[HW-003] SCAN_START");
  const int16_t networkCount = WiFi.scanNetworks(false, true);
  bool targetFound = false;
  bool targetIs2G = false;

  Serial.printf("[HW-003] SCAN_COUNT=%d\n", networkCount);
  for (int16_t index = 0; index < networkCount; ++index) {
    const String ssid = WiFi.SSID(index);
    const int channel = WiFi.channel(index);
    const bool isTarget = ssid == gTargetSsid;
    targetFound = targetFound || isTarget;
    targetIs2G = targetIs2G || (isTarget && channel >= 1 && channel <= 14);
    Serial.printf("[HW-003] NETWORK INDEX=%d SSID=%s CHANNEL=%d RSSI=%d ENCRYPTION=%d\n",
                  index, ssid.c_str(), channel, WiFi.RSSI(index),
                  static_cast<int>(WiFi.encryptionType(index)));
  }
  WiFi.scanDelete();

  printResult("TARGET_NETWORK_FOUND", targetFound);
  printResult("TARGET_NETWORK_2G_CHANNEL", targetIs2G);
  return targetFound && targetIs2G;
}

bool runDnsTest() {
  IPAddress resolvedAddress;
  const int result = WiFi.hostByName(kDnsHost, resolvedAddress);
  const bool passed = result == 1;
  Serial.printf("[HW-003] DNS_RESULT=%d HOST=%s ADDRESS=%s\n", result, kDnsHost,
                resolvedAddress.toString().c_str());
  printResult("DNS_RESOLUTION", passed);
  return passed;
}

bool runHttpTest() {
  WiFiClient client;
  if (!client.connect(kHttpHost, 80, kHttpTimeoutMs)) {
    printResult("HTTP_CONNECTION", false);
    return false;
  }

  client.printf("GET / HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", kHttpHost);
  const uint32_t startedAt = millis();
  while (!client.available() && millis() - startedAt < kHttpTimeoutMs) {
    delay(50);
  }

  char responseLine[96] = {};
  const size_t length = client.readBytesUntil('\n', responseLine, sizeof(responseLine) - 1);
  responseLine[length] = '\0';
  client.stop();
  const bool passed = length >= 8 && std::strncmp(responseLine, "HTTP/1.", 7) == 0;
  Serial.printf("[HW-003] HTTP_RESPONSE=%s\n", responseLine);
  printResult("HTTP_TRANSACTION", passed);
  return passed;
}

bool runUnavailableAccessPointTest() {
  Serial.println("[HW-003] UNAVAILABLE_AP_TEST_START");
  WiFi.disconnect(false, false);
  delay(500);
  WiFi.begin(kUnavailableSsid, kUnavailablePassword);
  const uint32_t startedAt = millis();
  uint32_t lastHeartbeatAt = startedAt;

  while (millis() - startedAt < kUnavailableTimeoutMs) {
    if (WiFi.status() == WL_CONNECTED) {
      WiFi.disconnect(false, false);
      printResult("UNAVAILABLE_AP_REJECTED", false);
      return false;
    }
    if (millis() - lastHeartbeatAt >= 1000) {
      lastHeartbeatAt = millis();
      Serial.printf("[HW-003] UNAVAILABLE_AP_WAIT STATUS=%d ELAPSED_MS=%lu\n",
                    WiFi.status(), lastHeartbeatAt - startedAt);
    }
    delay(50);
  }

  const bool handled = WiFi.status() != WL_CONNECTED;
  printResult("UNAVAILABLE_AP_REJECTED", handled);
  return handled;
}

void printCycleResult(uint8_t cycle, const char* phase, bool passed) {
  Serial.printf("[HW-003][%s] RECONNECT_CYCLE=%u PHASE=%s\n",
                passed ? "PASS" : "FAIL", cycle, phase);
  gAutomatedChecksPassed = gAutomatedChecksPassed && passed;
}

bool runReconnectCycles() {
  bool allPassed = true;
  for (uint8_t cycle = 1; cycle <= kReconnectCycleCount; ++cycle) {
    WiFi.disconnect(false, false);
    const bool disconnected = waitForDisconnected(kDisconnectTimeoutMs);
    printCycleResult(cycle, "DISCONNECTED", disconnected);
    const bool reconnected = disconnected && connectToTarget();
    printCycleResult(cycle, "RECONNECTED", reconnected);
    allPassed = allPassed && disconnected && reconnected;
  }
  return allPassed;
}

void startSoftAp() {
  if (gSoftApActive) {
    return;
  }
  WiFi.mode(WIFI_AP_STA);
  gSoftApActive = WiFi.softAP(kSoftApSsid, kSoftApPassword);
  printResult("OPTIONAL_SOFTAP_STARTED", gSoftApActive);
  if (gSoftApActive) {
    gSoftApServer.begin();
    gSoftApServer.setNoDelay(true);
    Serial.printf("[HW-003] SOFTAP_SSID=%s IP=%s CLIENTS=%d\n", kSoftApSsid,
                  WiFi.softAPIP().toString().c_str(), WiFi.softAPgetStationNum());
  }
}

void stopSoftAp() {
  if (gSoftApActive) {
    gSoftApServer.end();
    WiFi.softAPdisconnect(true);
    gSoftApActive = false;
    Serial.println("[HW-003] OPTIONAL_SOFTAP_STOPPED");
  }
}

void handleSoftApClient() {
  if (!gSoftApActive) {
    return;
  }

  WiFiClient client = gSoftApServer.available();
  if (!client) {
    return;
  }

  const uint32_t startedAt = millis();
  while (!client.available() && millis() - startedAt < 2000) {
    delay(10);
  }
  if (client.available()) {
    client.readStringUntil('\n');
    client.print("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n"
                 "Connection: close\r\nContent-Length: 20\r\n\r\n"
                 "NOVA-HW-003-AP-PASS\n");
    gSoftApPayloadPassed = true;
    printResult("OPTIONAL_SOFTAP_PAYLOAD", true);
  }
  client.stop();
}

void printSummary() {
  Serial.printf("[HW-003] AUTOMATED_CHECKS=%s\n",
                gAutomatedChecksPassed ? "PASS" : "FAIL");
  Serial.printf("[HW-003] OPTIONAL_SOFTAP=%s\n",
                !gSoftApActive ? (gSoftApPayloadPassed ? "PASS" : "NOT_RUN")
                               : "ACTIVE_CLIENT_CHECK_PENDING");
  Serial.println("[HW-003] WIFI_TEST_COMPLETE");
}

void handleCommand() {
  while (Serial.available()) {
    const char command = static_cast<char>(Serial.read());
    if (command == 'a') {
      startSoftAp();
    } else if (command == 'x') {
      stopSoftAp();
    } else if (command == 's') {
      printSummary();
    } else if (command == 'h') {
      Serial.println("[HW-003] COMMANDS: a=start optional SoftAP, x=stop SoftAP, s=summary, h=help");
    }
  }
}

void haltAfterFailure() {
  Serial.println("[HW-003][FAIL] WIFI_TEST_HALTED");
  while (true) {
    delay(1000);
    Serial.println("[HW-003][FAIL] RESET_REQUIRED");
  }
}

}  // namespace

void setup() {
  Serial.begin(kSerialBaudRate);
  const uint32_t serialStartedAt = millis();
  while (!Serial && millis() - serialStartedAt < kSerialConnectTimeoutMs) {
    delay(10);
  }

  const bool ssidDecoded = decodeHex(WIFI_TEST_STRINGIFY(WIFI_TEST_SSID_HEX), gTargetSsid,
                                     sizeof(gTargetSsid)) > 0;
  const bool passwordDecoded =
      decodeHex(WIFI_TEST_STRINGIFY(WIFI_TEST_PASSWORD_HEX), gTargetPassword,
                sizeof(gTargetPassword)) > 0;
  printHeader();
  if (!ssidDecoded || !passwordDecoded) {
    printResult("CREDENTIALS_INJECTED", false);
    haltAfterFailure();
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  runNetworkScan();
  if (!connectToTarget()) {
    printSummary();
    haltAfterFailure();
  }

  runDnsTest();
  runHttpTest();
  runUnavailableAccessPointTest();
  runReconnectCycles();
  printSummary();
  Serial.println("[HW-003] COMMANDS_READY");
}

void loop() {
  handleCommand();
  handleSoftApClient();
  if (millis() - gLastStatusReportAt >= kStatusReportIntervalMs) {
    gLastStatusReportAt = millis();
    Serial.printf("[HW-003] STATUS=%d RSSI=%d IP=%s AP_CLIENTS=%d\n",
                  WiFi.status(), WiFi.RSSI(), WiFi.localIP().toString().c_str(),
                  gSoftApActive ? WiFi.softAPgetStationNum() : 0);
  }
  delay(10);
}
