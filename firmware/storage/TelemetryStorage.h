#pragma once

#include <FS.h>

#include <cstdint>

#include "../core/Logger.h"
#include "../telemetry/TelemetryItem.h"

namespace nova {

/** Current state of the bounded telemetry journal and physical card. */
struct StorageSnapshot {
  bool mounted = false;
  uint64_t physicalTotalBytes = 0;
  uint64_t physicalUsedBytes = 0;
  uint64_t journalQuotaBytes = 0;
  uint64_t journalUsedBytes = 0;
  uint64_t journalAvailableBytes = 0;
  uint32_t recordCount = 0;
};

/** Owns the fixed-record, power-loss-tolerant telemetry journal on TF storage. */
class TelemetryStorage {
 public:
  /** Construct a journal adapter; no card access occurs here. */
  explicit TelemetryStorage(Logger& logger);

  /** Mount the official 1-bit Waveshare TF interface without formatting. */
  bool begin();

  /** Append one validated item inside the logical 2 GiB quota. */
  bool append(const TelemetryItem& item);

  /** Return the latest storage and journal accounting snapshot. */
  const StorageSnapshot& snapshot() const;

 private:
  struct __attribute__((packed)) JournalMetadata {
    uint32_t magic;
    uint16_t version;
    uint16_t recordSize;
    uint32_t quotaBytes;
    uint32_t usedBytes;
    uint32_t writeOffset;
    uint32_t recordCount;
    uint32_t sequence;
    uint32_t crc32;
    uint8_t reserved[32];
  };

  static_assert(sizeof(JournalMetadata) == 64);

  static constexpr uint32_t kMetadataMagic = 0x4E4A4D31U;
  static constexpr uint16_t kMetadataVersion = 1;
  static constexpr uint32_t kConfiguredQuotaBytes =
      static_cast<uint32_t>(2ULL * 1024ULL * 1024ULL * 1024ULL);
  static constexpr uint32_t kBytesPerMegabyte = 1024U * 1024U;
  static constexpr uint32_t kMetadataCrcLength = offsetof(JournalMetadata, crc32);
  static constexpr uint32_t kMetadataSlotSize = sizeof(JournalMetadata);
  static constexpr uint8_t kMaxOpenFiles = 5;
  static constexpr uint32_t kSdFrequencyHz = 20000;
  static constexpr bool kOneBitMode = true;
  static constexpr char kMountPoint[] = "/sdcard";
  // SD_MMC paths are relative to the /sdcard VFS mount point.
  static constexpr char kDirectory[] = "/nova";
  static constexpr char kJournalPath[] = "/nova/telemetry.jrn";
  static constexpr char kMetadataPath[] = "/nova/telemetry.idx";

  bool openFiles();
  bool loadMetadata();
  bool persistMetadata();
  bool readMetadataSlot(uint8_t slot, JournalMetadata& metadata);
  bool validMetadata(const JournalMetadata& metadata) const;
  void refreshPhysicalSnapshot();
  void refreshJournalSnapshot();
  bool resetMetadata();
  static fs::File openReadWrite(const char* path);

  Logger& logger_;
  fs::File journal_;
  fs::File metadataFile_;
  StorageSnapshot snapshot_ = {};
  JournalMetadata metadata_ = {};
  bool mounted_ = false;
};

}  // namespace nova
