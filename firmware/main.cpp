#include <Arduino.h>

namespace {

constexpr uint32_t kSerialBaudRate = 115200;
constexpr uint32_t kStatusPeriodMs = 2000;
constexpr uint32_t kSerialConnectTimeoutMs = 3000;

void printBoardStatus() {
  Serial.println(F("Nova firmware bring-up diagnostic"));
  Serial.printf("Chip: %s, revision %d\n", ESP.getChipModel(), ESP.getChipRevision());
  Serial.printf("CPU: %lu MHz\n", ESP.getCpuFreqMHz());
  Serial.printf("Flash: %lu bytes\n", ESP.getFlashChipSize());
  Serial.printf("PSRAM: %s, %lu bytes\n", psramFound() ? "available" : "not detected",
                ESP.getPsramSize());
}

}  // namespace

void setup() {
  Serial.begin(kSerialBaudRate);

  const uint32_t waitStartedAt = millis();
  while (!Serial && millis() - waitStartedAt < kSerialConnectTimeoutMs) {
    delay(10);
  }

  printBoardStatus();
}

void loop() {
  static uint32_t lastStatusAt = 0;
  const uint32_t now = millis();

  if (now - lastStatusAt >= kStatusPeriodMs) {
    lastStatusAt = now;
    Serial.printf("Uptime: %lu s\n", now / 1000);
  }

  delay(10);
}
