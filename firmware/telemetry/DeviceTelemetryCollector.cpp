#include "DeviceTelemetryCollector.h"

#include <Arduino.h>

#include <cstdio>

namespace nova {

DeviceTelemetryCollector::DeviceTelemetryCollector(WifiService& wifi,
                                                   BoardTouch& touch)
    : wifi_(wifi), touch_(touch) {}

void DeviceTelemetryCollector::collect(DeviceTelemetrySnapshot& snapshot) {
  const DeviceTelemetrySnapshot emptySnapshot = {};
  snapshot = emptySnapshot;
  snapshot.sequence = ++sequence_;
  snapshot.uptimeSeconds = millis() / 1000UL;
  snapshot.wifiRssiDbm = wifi_.state() == WifiState::Connected
                             ? wifi_.rssi()
                             : DeviceTelemetrySnapshot::kUnavailableRssiDbm;
  snapshot.freeHeapBytes = ESP.getFreeHeap();
  snapshot.touchReady = touch_.isReady();
  snapshot.touchActive = snapshot.touchReady && touch_.isPressed();
  snprintf(snapshot.audioState, sizeof(snapshot.audioState), "%s",
           DeviceTelemetrySnapshot::kUnavailableAudioState);
}

}  // namespace nova
