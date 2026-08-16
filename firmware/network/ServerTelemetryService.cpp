#include "ServerTelemetryService.h"

#include <WiFi.h>

#include <cstdio>
#include <cstring>

#include "HubTrustAnchor.h"

namespace nova {
namespace {

constexpr char kHubHost[] = "nova-hub.local";
constexpr uint16_t kHubPort = 443;
constexpr char kTelemetryPath[] = "/v1/telemetry/stream";

}  // namespace

ServerTelemetryService::ServerTelemetryService(Logger& logger, WifiService& wifi)
    : logger_(logger), wifi_(wifi) {}

bool ServerTelemetryService::begin() {
  if (kHubRootCa[0] == '\0') {
    setError("hub CA trust anchor missing");
    return false;
  }
  tls_.setCACert(kHubRootCa);
  state_ = ServerTelemetryState::Offline;
  ready_ = true;
  return true;
}

void ServerTelemetryService::update() {
  if (!ready_) {
    return;
  }
  if (wifi_.state() != WifiState::Connected) {
    closeStream();
    state_ = ServerTelemetryState::Offline;
    return;
  }

  const uint32_t now = millis();
  if (!streamOpen_) {
    if (lastConnectAttemptAt_ != 0 &&
        now - lastConnectAttemptAt_ < kReconnectPeriodMs) {
      return;
    }
    lastConnectAttemptAt_ = now;
    if (!openStream()) {
      return;
    }
  }

  pumpStream(now);
  if (snapshot_.valid && now - lastFrameAt_ >= kStaleAfterMs) {
    state_ = ServerTelemetryState::Stale;
  }
}

const ServerTelemetrySnapshot& ServerTelemetryService::snapshot() const {
  return snapshot_;
}

const TelemetryItem& ServerTelemetryService::latestItem() const {
  return latestItem_;
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

bool ServerTelemetryService::openStream() {
  char url[128] = {};
  snprintf(url, sizeof(url), "https://%s:%u%s", kHubHost, kHubPort,
           kTelemetryPath);
  http_.end();
  tls_.stop();
  http_.setReuse(false);
  http_.useHTTP10(true);
  if (!http_.begin(tls_, url)) {
    setError("telemetry HTTPS setup failed");
    return false;
  }
  http_.setTimeout(kRequestTimeoutMs);
  const int responseCode = http_.GET();
  if (responseCode != HTTP_CODE_OK) {
    http_.end();
    tls_.stop();
    setError("telemetry stream request failed");
    return false;
  }
  frameBytes_ = 0;
  streamOpen_ = true;
  return true;
}

void ServerTelemetryService::closeStream() {
  if (!streamOpen_ && !http_.connected()) {
    frameBytes_ = 0;
    return;
  }
  http_.end();
  tls_.stop();
  frameBytes_ = 0;
  streamOpen_ = false;
}

void ServerTelemetryService::pumpStream(uint32_t now) {
  if (!http_.connected()) {
    closeStream();
    setError("telemetry stream disconnected");
    return;
  }

  Stream& body = http_.getStream();
  size_t bytesRead = 0;
  while (body.available() > 0 && bytesRead < kMaxBytesPerUpdate) {
    const int value = body.read();
    if (value < 0) {
      break;
    }
    frameBuffer_[frameBytes_++] = static_cast<uint8_t>(value);
    ++bytesRead;
    if (frameBytes_ < sizeof(kTelemetryMagic)) {
      continue;
    }
    if (memcmp(frameBuffer_, kTelemetryMagic, sizeof(kTelemetryMagic)) != 0) {
      discardLeadingByte();
      continue;
    }
    if (frameBytes_ < kTelemetryFrameSize) {
      continue;
    }

    TelemetryItem item = {};
    memcpy(&item, frameBuffer_, sizeof(item));
    if (isValidTelemetryItem(item) && item.kind == kServerTelemetryItemKind) {
      processFrame(item, now);
      frameBytes_ = 0;
    } else {
      discardLeadingByte();
    }
  }

  if (!http_.connected() && body.available() == 0) {
    closeStream();
    setError("telemetry stream ended");
  }
}

void ServerTelemetryService::processFrame(const TelemetryItem& item,
                                           uint32_t now) {
  latestItem_ = item;
  snapshot_.valid = true;
  snapshot_.gpuAvailable = (item.flags & kTelemetryGpuAvailableFlag) != 0;
  snapshot_.cpuPercent = item.cpuTenths < 0 ? -1.0F : item.cpuTenths / 10.0F;
  snapshot_.gpuUtilizationPercent =
      item.gpuUtilizationTenths < 0 ? -1.0F
                                    : item.gpuUtilizationTenths / 10.0F;
  snapshot_.gpuTemperatureC =
      item.gpuTemperatureTenths < 0 ? -1.0F
                                    : item.gpuTemperatureTenths / 10.0F;
  snapshot_.gpuVramUsedBytes = item.metricA;
  snapshot_.gpuVramTotalBytes = item.metricB;
  snapshot_.uptimeSeconds = item.uptimeSeconds;
  snapshot_.errorCount = item.errorCount;
  snapshot_.sequence = item.sequence;
  snapshot_.timestampSeconds = item.timestampSeconds;
  snapshot_.receivedAtMs = now;
  snapshot_.gpuName[0] = '\0';
  snprintf(snapshot_.dependencyStatus, sizeof(snapshot_.dependencyStatus),
           "%s", (item.flags & kTelemetryDegradedFlag) ? "degraded"
                                                         : "healthy");
  snapshot_.error[0] = '\0';
  lastFrameAt_ = now;
  state_ = (item.flags & kTelemetryDegradedFlag)
               ? ServerTelemetryState::Degraded
               : ServerTelemetryState::Live;
}

void ServerTelemetryService::discardLeadingByte() {
  if (frameBytes_ == 0) {
    return;
  }
  memmove(frameBuffer_, frameBuffer_ + 1, frameBytes_ - 1);
  --frameBytes_;
}

void ServerTelemetryService::setError(const char* message) {
  snprintf(snapshot_.error, sizeof(snapshot_.error), "%s",
           message == nullptr ? "telemetry stream error" : message);
  state_ = snapshot_.valid ? ServerTelemetryState::Stale
                           : ServerTelemetryState::Error;
  logger_.writef(LogLevel::Warning, "Telemetry: %s",
                 message == nullptr ? "telemetry stream error" : message);
}

}  // namespace nova
