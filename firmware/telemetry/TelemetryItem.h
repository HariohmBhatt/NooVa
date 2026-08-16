#pragma once

#include <Arduino.h>

#include <cstddef>
#include <cstdint>

namespace nova {

/** Four-byte synchronization marker at the start of every item. */
inline constexpr uint8_t kTelemetryMagic[] = {'N', 'V', 'T', '1'};
/** Current fixed-width stream version. */
constexpr uint8_t kTelemetryStreamVersion = 1;
/** Item kind emitted by the server telemetry stream. */
constexpr uint8_t kServerTelemetryItemKind = 1;
/** Item kind emitted by the local ESP health recorder. */
constexpr uint8_t kEspHealthItemKind = 2;
/** Flag indicating that the server exposed a usable GPU. */
constexpr uint8_t kTelemetryGpuAvailableFlag = 1 << 0;
/** Flag indicating that one or more server collectors degraded. */
constexpr uint8_t kTelemetryDegradedFlag = 1 << 1;
/** Flag indicating that the local TF journal is mounted. */
constexpr uint8_t kTelemetryStorageMountedFlag = 1 << 2;
/** Size of one wire and journal item in bytes. */
constexpr uint16_t kTelemetryFrameSize = 64;

/** Fixed-width item shared by the server stream and the SD journal. */
struct __attribute__((packed)) TelemetryItem {
  uint8_t magic[4];
  uint8_t version;
  uint8_t kind;
  uint16_t frameSize;
  uint32_t sequence;
  uint64_t timestampSeconds;
  int16_t cpuTenths;
  int16_t gpuUtilizationTenths;
  int16_t gpuTemperatureTenths;
  uint16_t flags;
  uint64_t metricA;
  uint64_t metricB;
  uint32_t uptimeSeconds;
  uint32_t errorCount;
  uint8_t reserved[8];
  uint32_t crc32;
};

static_assert(sizeof(TelemetryItem) == kTelemetryFrameSize);
static_assert(offsetof(TelemetryItem, crc32) == 60);

/** Calculate the wire-compatible CRC32 for a byte range. */
uint32_t telemetryCrc32(const uint8_t* bytes, size_t length);

/** Finalize a new item by writing its header and checksum. */
void finalizeTelemetryItem(TelemetryItem& item);

/** Return whether an item has a supported header and valid checksum. */
bool isValidTelemetryItem(const TelemetryItem& item);

/** Convert a finite metric to tenths without overflowing the wire field. */
int16_t metricToTenths(float value);

}  // namespace nova
