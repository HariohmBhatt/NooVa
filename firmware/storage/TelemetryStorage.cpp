#include "TelemetryStorage.h"

#include <SD_MMC.h>

#include <algorithm>
#include <cstring>

#include "../hardware/BoardPins.h"

namespace nova {

TelemetryStorage::TelemetryStorage(Logger& logger) : logger_(logger) {}

bool TelemetryStorage::begin() {
  if (!SD_MMC.setPins(board::kSdClockPin, board::kSdCommandPin,
                      board::kSdData0Pin) ||
      !SD_MMC.begin(kMountPoint, kOneBitMode, false, kSdFrequencyHz,
                    kMaxOpenFiles)) {
    logger_.write(LogLevel::Warning, "Telemetry storage unavailable");
    return false;
  }
  mounted_ = true;
  if (!SD_MMC.exists(kDirectory) && !SD_MMC.mkdir(kDirectory)) {
    logger_.write(LogLevel::Warning, "Telemetry storage directory unavailable");
    SD_MMC.end();
    mounted_ = false;
    return false;
  }
  if (!openFiles()) {
    logger_.write(LogLevel::Warning, "Telemetry journal files unavailable");
    SD_MMC.end();
    mounted_ = false;
    return false;
  }
  if (!loadMetadata()) {
    logger_.write(LogLevel::Warning, "Telemetry storage index unavailable");
    SD_MMC.end();
    mounted_ = false;
    return false;
  }
  if (metadata_.quotaBytes > SD_MMC.totalBytes()) {
    if (!resetMetadata()) {
      logger_.write(LogLevel::Warning, "Telemetry storage quota unavailable");
      SD_MMC.end();
      mounted_ = false;
      return false;
    }
  }
  if (metadata_.quotaBytes == 0) {
    logger_.write(LogLevel::Warning, "Telemetry storage has no usable quota");
    SD_MMC.end();
    mounted_ = false;
    return false;
  }
  refreshPhysicalSnapshot();
  logger_.writef(LogLevel::Info, "Telemetry storage mounted quota=%lu MB",
                 static_cast<unsigned long>(snapshot_.journalQuotaBytes /
                                             kBytesPerMegabyte));
  return true;
}

bool TelemetryStorage::append(const TelemetryItem& item) {
  if (!mounted_ || metadata_.quotaBytes == 0 ||
      !isValidTelemetryItem(item) || !journal_) {
    return false;
  }
  if (!journal_.seek(metadata_.writeOffset) ||
      journal_.write(reinterpret_cast<const uint8_t*>(&item), sizeof(item)) !=
          sizeof(item)) {
    logger_.write(LogLevel::Warning, "Telemetry journal write failed");
    return false;
  }
  journal_.flush();
  metadata_.writeOffset += sizeof(item);
  if (metadata_.writeOffset >= metadata_.quotaBytes) {
    metadata_.writeOffset = 0;
  }
  metadata_.usedBytes = std::min(
      metadata_.quotaBytes, metadata_.usedBytes + static_cast<uint32_t>(sizeof(item)));
  const uint32_t maxRecords = metadata_.quotaBytes / sizeof(item);
  metadata_.recordCount = std::min(maxRecords, metadata_.recordCount + 1);
  ++metadata_.sequence;
  if (!persistMetadata()) {
    logger_.write(LogLevel::Warning, "Telemetry journal index write failed");
    return false;
  }
  refreshJournalSnapshot();
  return true;
}

const StorageSnapshot& TelemetryStorage::snapshot() const { return snapshot_; }

bool TelemetryStorage::openFiles() {
  journal_ = openReadWrite(kJournalPath);
  metadataFile_ = openReadWrite(kMetadataPath);
  return journal_ && metadataFile_;
}

bool TelemetryStorage::loadMetadata() {
  JournalMetadata first = {};
  JournalMetadata second = {};
  const bool firstValid = readMetadataSlot(0, first);
  const bool secondValid = readMetadataSlot(1, second);
  if (!firstValid && !secondValid) {
    return resetMetadata();
  }
  metadata_ = !secondValid || (firstValid && first.sequence >= second.sequence)
                  ? first
                  : second;
  return true;
}

bool TelemetryStorage::persistMetadata() {
  if (!metadataFile_ || metadata_.quotaBytes == 0) {
    return false;
  }
  metadata_.magic = kMetadataMagic;
  metadata_.version = kMetadataVersion;
  metadata_.recordSize = sizeof(TelemetryItem);
  metadata_.crc32 = telemetryCrc32(
      reinterpret_cast<const uint8_t*>(&metadata_), kMetadataCrcLength);
  const uint8_t slot = metadata_.sequence % 2;
  if (!metadataFile_.seek(slot * kMetadataSlotSize) ||
      metadataFile_.write(reinterpret_cast<const uint8_t*>(&metadata_),
                          sizeof(metadata_)) != sizeof(metadata_)) {
    return false;
  }
  metadataFile_.flush();
  return true;
}

bool TelemetryStorage::readMetadataSlot(uint8_t slot,
                                        JournalMetadata& metadata) {
  if (!metadataFile_ || !metadataFile_.seek(slot * kMetadataSlotSize) ||
      metadataFile_.read(reinterpret_cast<uint8_t*>(&metadata),
                         sizeof(metadata)) != sizeof(metadata)) {
    return false;
  }
  return validMetadata(metadata);
}

bool TelemetryStorage::validMetadata(const JournalMetadata& metadata) const {
  if (metadata.magic != kMetadataMagic || metadata.version != kMetadataVersion ||
      metadata.recordSize != sizeof(TelemetryItem) ||
      metadata.quotaBytes == 0 || metadata.quotaBytes > kConfiguredQuotaBytes ||
      metadata.usedBytes > metadata.quotaBytes ||
      metadata.writeOffset >= metadata.quotaBytes ||
      metadata.writeOffset % sizeof(TelemetryItem) != 0 ||
      metadata.usedBytes % sizeof(TelemetryItem) != 0 ||
      metadata.recordCount > metadata.quotaBytes / sizeof(TelemetryItem) ||
      metadata.recordCount != metadata.usedBytes / sizeof(TelemetryItem)) {
    return false;
  }
  const uint32_t expected = telemetryCrc32(
      reinterpret_cast<const uint8_t*>(&metadata), kMetadataCrcLength);
  return expected == metadata.crc32;
}

void TelemetryStorage::refreshPhysicalSnapshot() {
  snapshot_.physicalTotalBytes = SD_MMC.totalBytes();
  snapshot_.physicalUsedBytes = SD_MMC.usedBytes();
  refreshJournalSnapshot();
}

void TelemetryStorage::refreshJournalSnapshot() {
  snapshot_.mounted = mounted_;
  snapshot_.journalQuotaBytes = metadata_.quotaBytes;
  snapshot_.journalUsedBytes = metadata_.usedBytes;
  snapshot_.journalAvailableBytes = metadata_.quotaBytes - metadata_.usedBytes;
  snapshot_.recordCount = metadata_.recordCount;
}

bool TelemetryStorage::resetMetadata() {
  memset(&metadata_, 0, sizeof(metadata_));
  const uint64_t physicalBytes = SD_MMC.totalBytes();
  const uint64_t capacity = std::min<uint64_t>(kConfiguredQuotaBytes,
                                               physicalBytes);
  metadata_.quotaBytes = static_cast<uint32_t>(
      capacity / sizeof(TelemetryItem) * sizeof(TelemetryItem));
  metadata_.sequence = 0;
  return persistMetadata();
}

fs::File TelemetryStorage::openReadWrite(const char* path) {
  if (SD_MMC.exists(path)) {
    return SD_MMC.open(path, "r+");
  }
  return SD_MMC.open(path, "w+");
}

}  // namespace nova
