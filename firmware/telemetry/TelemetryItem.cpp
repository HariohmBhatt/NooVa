#include "TelemetryItem.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace nova {
namespace {

constexpr uint32_t kCrcPolynomial = 0xEDB88320U;

}  // namespace

uint32_t telemetryCrc32(const uint8_t* bytes, size_t length) {
  uint32_t crc = 0xFFFFFFFFU;
  for (size_t index = 0; index < length; ++index) {
    crc ^= bytes[index];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      const uint32_t mask = -(crc & 1U);
      crc = (crc >> 1) ^ (kCrcPolynomial & mask);
    }
  }
  return crc ^ 0xFFFFFFFFU;
}

void finalizeTelemetryItem(TelemetryItem& item) {
  memcpy(item.magic, kTelemetryMagic, sizeof(item.magic));
  item.version = kTelemetryStreamVersion;
  item.frameSize = kTelemetryFrameSize;
  item.crc32 = telemetryCrc32(reinterpret_cast<const uint8_t*>(&item),
                               offsetof(TelemetryItem, crc32));
}

bool isValidTelemetryItem(const TelemetryItem& item) {
  if (memcmp(item.magic, kTelemetryMagic, sizeof(item.magic)) != 0 ||
      item.version != kTelemetryStreamVersion ||
      item.frameSize != kTelemetryFrameSize) {
    return false;
  }
  const uint32_t expected =
      telemetryCrc32(reinterpret_cast<const uint8_t*>(&item),
                     offsetof(TelemetryItem, crc32));
  return expected == item.crc32;
}

int16_t metricToTenths(float value) {
  if (!std::isfinite(value)) {
    return -1;
  }
  const float scaled = value * 10.0F;
  const float bounded = std::max(-3276.8F, std::min(3276.7F, scaled));
  return static_cast<int16_t>(std::lround(bounded));
}

}  // namespace nova
