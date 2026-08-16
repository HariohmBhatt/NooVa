#include "EspHealthService.h"

#include <Arduino.h>
#include <esp_freertos_hooks.h>
#include <freertos/FreeRTOS.h>

#include <algorithm>

namespace nova {
namespace {

constexpr uint8_t kIdleCoreCount = 2;
volatile uint32_t gIdleTicks[kIdleCoreCount] = {};
volatile uint32_t gTotalTicks[kIdleCoreCount] = {};

bool IRAM_ATTR recordIdleTick() {
  const BaseType_t core = xPortGetCoreID();
  if (core >= 0 && core < kIdleCoreCount) {
    ++gIdleTicks[core];
  }
  return true;
}

void IRAM_ATTR recordTotalTick() {
  const BaseType_t core = xPortGetCoreID();
  if (core >= 0 && core < kIdleCoreCount) {
    ++gTotalTicks[core];
  }
}

}  // namespace

EspHealthService::EspHealthService(Logger& logger, TelemetryStorage& storage)
    : logger_(logger), storage_(storage) {}

bool EspHealthService::begin() {
  hooksReady_ = registerCpuHooks();
  snapshot_.ready = hooksReady_;
  update();
  return hooksReady_;
}

void EspHealthService::update() {
  const uint32_t now = millis();
  if (lastSampleAt_ != 0 && now - lastSampleAt_ < kSamplePeriodMs) {
    return;
  }
  lastSampleAt_ = now;
  sampleCpu();
  const StorageSnapshot& storage = storage_.snapshot();
  snapshot_.storageMounted = storage.mounted;
  snapshot_.storageAvailableBytes = storage.journalAvailableBytes;
  snapshot_.storageQuotaBytes = storage.journalQuotaBytes;
  snapshot_.uptimeSeconds = now / 1000;
}

const EspHealthSnapshot& EspHealthService::snapshot() const { return snapshot_; }

TelemetryItem EspHealthService::makeItem(uint32_t sequence,
                                         uint64_t timestampSeconds) const {
  TelemetryItem item = {};
  item.kind = kEspHealthItemKind;
  item.sequence = sequence;
  item.timestampSeconds = timestampSeconds;
  item.cpuTenths = localCpuPercent_ < 0 ? -1 : localCpuPercent_ * 10;
  item.metricA = snapshot_.storageAvailableBytes;
  item.metricB = snapshot_.storageQuotaBytes;
  item.uptimeSeconds = snapshot_.uptimeSeconds;
  if (snapshot_.storageMounted) {
    item.flags |= kTelemetryStorageMountedFlag;
  }
  finalizeTelemetryItem(item);
  return item;
}

bool EspHealthService::registerCpuHooks() {
  for (uint8_t core = 0; core < kCpuCoreCount; ++core) {
    if (esp_register_freertos_idle_hook_for_cpu(recordIdleTick, core) !=
        ESP_OK) {
      for (uint8_t registered = 0; registered < core; ++registered) {
        esp_deregister_freertos_idle_hook_for_cpu(recordIdleTick, registered);
        esp_deregister_freertos_tick_hook_for_cpu(recordTotalTick, registered);
      }
      logger_.write(LogLevel::Warning, "ESP CPU hooks unavailable");
      return false;
    }
    if (esp_register_freertos_tick_hook_for_cpu(recordTotalTick, core) !=
        ESP_OK) {
      esp_deregister_freertos_idle_hook_for_cpu(recordIdleTick, core);
      for (uint8_t registered = 0; registered < core; ++registered) {
        esp_deregister_freertos_idle_hook_for_cpu(recordIdleTick, registered);
        esp_deregister_freertos_tick_hook_for_cpu(recordTotalTick, registered);
      }
      logger_.write(LogLevel::Warning, "ESP CPU tick hooks unavailable");
      return false;
    }
  }
  return true;
}

void EspHealthService::sampleCpu() {
  if (!hooksReady_) {
    localCpuPercent_ = -1;
    return;
  }
  if (!sampleReady_) {
    for (uint8_t core = 0; core < kCpuCoreCount; ++core) {
      lastIdleTicks_[core] = gIdleTicks[core];
      lastTotalTicks_[core] = gTotalTicks[core];
    }
    sampleReady_ = true;
    return;
  }
  uint32_t totalTicks = 0;
  uint32_t idleTicks = 0;
  for (uint8_t core = 0; core < kCpuCoreCount; ++core) {
    totalTicks += gTotalTicks[core] - lastTotalTicks_[core];
    idleTicks += gIdleTicks[core] - lastIdleTicks_[core];
    lastTotalTicks_[core] = gTotalTicks[core];
    lastIdleTicks_[core] = gIdleTicks[core];
  }
  if (totalTicks == 0) {
    return;
  }
  idleTicks = std::min(idleTicks, totalTicks);
  localCpuPercent_ = static_cast<int16_t>(
      ((totalTicks - idleTicks) * 100U) / totalTicks);
}

}  // namespace nova
