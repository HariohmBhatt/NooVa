#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>

#include <algorithm>

namespace {

// Waveshare's official examples use these pins in 1-bit mode at 20 MHz.
constexpr int kSdClockPin = 11;
constexpr int kSdCommandPin = 10;
constexpr int kSdData0Pin = 9;
constexpr bool kOneBitMode = true;
constexpr bool kFormatIfMountFails = false;
constexpr int kSdFrequencyKHz = SDMMC_FREQ_DEFAULT;
constexpr char kMountPoint[] = "/sdcard";

constexpr char kTestRoot[] = "/nova-hw-006";
constexpr char kSmallPath[] = "/nova-hw-006/deterministic.bin";
constexpr char kRenamedPath[] = "/nova-hw-006/deterministic-renamed.bin";
constexpr char kLargePath[] = "/nova-hw-006/transfer.bin";
constexpr size_t kSmallFileBytes = 4096;
constexpr size_t kLargeFileBytes = 1024U * 1024U;
constexpr size_t kIoBufferBytes = 4096;
constexpr uint32_t kFnvOffsetBasis = 2166136261U;
constexpr uint32_t kFnvPrime = 16777619U;
constexpr uint32_t kSerialBaudRate = 115200;
constexpr uint32_t kStartupCaptureDelayMs = 2000;
constexpr uint32_t kRemountDelayMs = 100;

struct TransferResult {
  bool passed;
  uint32_t hash;
  uint32_t elapsedMs;
};

uint8_t gIoBuffer[kIoBufferBytes] = {};
bool gAllPassed = true;
bool gBlocked = false;

void printResult(const char* check, bool passed) {
  Serial.printf("[HW-006][%s] %s\n", passed ? "PASS" : "FAIL", check);
  gAllPassed = gAllPassed && passed;
}

uint8_t deterministicByte(size_t offset) {
  return static_cast<uint8_t>((offset * 73U + 0xA5U) ^ (offset >> 7U));
}

uint32_t updateFnv1a(uint32_t hash, const uint8_t* data, size_t length) {
  for (size_t index = 0; index < length; ++index) {
    hash = (hash ^ data[index]) * kFnvPrime;
  }
  return hash;
}

void fillDeterministicBuffer(size_t offset, size_t length) {
  for (size_t index = 0; index < length; ++index) {
    gIoBuffer[index] = deterministicByte(offset + index);
  }
}

bool configurePins() {
  const bool configured =
      SD_MMC.setPins(kSdClockPin, kSdCommandPin, kSdData0Pin);
  printResult("PINS_CONFIGURED", configured);
  return configured;
}

bool mountCard() {
  const bool mounted = SD_MMC.begin(kMountPoint, kOneBitMode,
                                    kFormatIfMountFails, kSdFrequencyKHz);
  if (!mounted || SD_MMC.cardType() == CARD_NONE) {
    SD_MMC.end();
    return false;
  }
  Serial.printf(
      "[HW-006] CARD_TYPE=%u CARD_SIZE_MB=%llu TOTAL_MB=%llu USED_MB=%llu\n",
      SD_MMC.cardType(), SD_MMC.cardSize() / (1024U * 1024U),
      SD_MMC.totalBytes() / (1024U * 1024U),
      SD_MMC.usedBytes() / (1024U * 1024U));
  return true;
}

bool removeIfPresent(const char* path) {
  return !SD_MMC.exists(path) || SD_MMC.remove(path);
}

bool cleanupWorkspace() {
  const bool filesRemoved = removeIfPresent(kSmallPath) &&
                            removeIfPresent(kRenamedPath) &&
                            removeIfPresent(kLargePath);
  const bool rootRemoved = !SD_MMC.exists(kTestRoot) || SD_MMC.rmdir(kTestRoot);
  return filesRemoved && rootRemoved && !SD_MMC.exists(kTestRoot);
}

bool createWorkspace() {
  if (!cleanupWorkspace()) {
    return false;
  }
  return SD_MMC.mkdir(kTestRoot) && SD_MMC.exists(kTestRoot);
}

TransferResult writeDeterministicFile(const char* path, size_t length) {
  File file = SD_MMC.open(path, FILE_WRITE);
  if (!file) {
    return {false, 0, 0};
  }
  uint32_t hash = kFnvOffsetBasis;
  const uint32_t startedAt = millis();
  size_t offset = 0;
  while (offset < length) {
    const size_t chunk = std::min(kIoBufferBytes, length - offset);
    fillDeterministicBuffer(offset, chunk);
    if (file.write(gIoBuffer, chunk) != chunk) {
      file.close();
      return {false, hash, millis() - startedAt};
    }
    hash = updateFnv1a(hash, gIoBuffer, chunk);
    offset += chunk;
  }
  file.flush();
  file.close();
  return {true, hash, millis() - startedAt};
}

bool bufferMatchesPattern(size_t offset, size_t length) {
  for (size_t index = 0; index < length; ++index) {
    if (gIoBuffer[index] != deterministicByte(offset + index)) {
      return false;
    }
  }
  return true;
}

TransferResult readDeterministicFile(const char* path, size_t expectedLength,
                                     uint32_t expectedHash) {
  File file = SD_MMC.open(path, FILE_READ);
  if (!file || file.size() != expectedLength) {
    return {false, 0, 0};
  }
  uint32_t hash = kFnvOffsetBasis;
  const uint32_t startedAt = millis();
  size_t offset = 0;
  while (offset < expectedLength) {
    const size_t chunk = std::min(kIoBufferBytes, expectedLength - offset);
    if (file.read(gIoBuffer, chunk) != chunk ||
        !bufferMatchesPattern(offset, chunk)) {
      file.close();
      return {false, hash, millis() - startedAt};
    }
    hash = updateFnv1a(hash, gIoBuffer, chunk);
    offset += chunk;
  }
  file.close();
  return {hash == expectedHash, hash, millis() - startedAt};
}

uint32_t throughputKiBPerSecond(size_t bytes, uint32_t elapsedMs) {
  return static_cast<uint32_t>((bytes * 1000ULL) /
                               (std::max<uint32_t>(elapsedMs, 1U) * 1024ULL));
}

void printTransfer(const char* operation, size_t bytes,
                   const TransferResult& result) {
  Serial.printf(
      "[HW-006] %s BYTES=%u ELAPSED_MS=%lu THROUGHPUT_KIB_S=%lu "
      "FNV1A32=0x%08lX\n",
      operation, bytes, result.elapsedMs,
      throughputKiBPerSecond(bytes, result.elapsedMs), result.hash);
}

bool verifyDirectoryContents() {
  File directory = SD_MMC.open(kTestRoot, FILE_READ);
  if (!directory || !directory.isDirectory()) {
    return false;
  }
  uint8_t fileCount = 0;
  File entry = directory.openNextFile();
  while (entry) {
    Serial.printf("[HW-006] WORKSPACE_ENTRY=%s SIZE=%u\n", entry.name(),
                  entry.size());
    fileCount += entry.isDirectory() ? 0 : 1;
    entry.close();
    entry = directory.openNextFile();
  }
  directory.close();
  return fileCount == 2;
}

bool writeAndVerifyFiles(uint32_t& smallHash, uint32_t& largeHash) {
  const TransferResult smallWrite =
      writeDeterministicFile(kSmallPath, kSmallFileBytes);
  const TransferResult smallRead =
      readDeterministicFile(kSmallPath, kSmallFileBytes, smallWrite.hash);
  printResult("DETERMINISTIC_READBACK", smallWrite.passed && smallRead.passed);
  smallHash = smallWrite.hash;
  const bool renamed = SD_MMC.rename(kSmallPath, kRenamedPath);
  printResult("FILE_RENAME", renamed);

  const TransferResult largeWrite =
      writeDeterministicFile(kLargePath, kLargeFileBytes);
  const TransferResult largeRead =
      readDeterministicFile(kLargePath, kLargeFileBytes, largeWrite.hash);
  printTransfer("LARGE_WRITE", kLargeFileBytes, largeWrite);
  printTransfer("LARGE_READ", kLargeFileBytes, largeRead);
  largeHash = largeWrite.hash;
  printResult("LARGE_TRANSFER_FNV1A32",
              largeWrite.passed && largeRead.passed);
  printResult("THROUGHPUT_RECORDED",
              largeWrite.elapsedMs > 0 && largeRead.elapsedMs > 0);
  return smallWrite.passed && smallRead.passed && renamed &&
         largeWrite.passed && largeRead.passed;
}

bool remountAndVerify(uint32_t smallHash, uint32_t largeHash) {
  SD_MMC.end();
  delay(kRemountDelayMs);
  if (!mountCard()) {
    printResult("REMOUNT", false);
    return false;
  }
  const TransferResult smallRead = readDeterministicFile(
      kRenamedPath, kSmallFileBytes, smallHash);
  const TransferResult largeRead =
      readDeterministicFile(kLargePath, kLargeFileBytes, largeHash);
  const bool passed = smallRead.passed && largeRead.passed;
  printResult("REMOUNT_READBACK", passed);
  return passed;
}

void runAcceptance() {
  const bool workspaceCreated = createWorkspace();
  printResult("WORKSPACE_CREATED", workspaceCreated);
  if (!workspaceCreated) {
    return;
  }
  uint32_t smallHash = 0;
  uint32_t largeHash = 0;
  const bool filesPassed = writeAndVerifyFiles(smallHash, largeHash);
  printResult("WORKSPACE_ENUMERATION", verifyDirectoryContents());
  const bool remountPassed = remountAndVerify(smallHash, largeHash);
  if (remountPassed) {
    printResult("WORKSPACE_CLEANUP", cleanupWorkspace());
  } else {
    Serial.println("[HW-006][FAIL] WORKSPACE_CLEANUP_REQUIRES_REMOUNT");
    gAllPassed = false;
  }
  gAllPassed = gAllPassed && filesPassed;
}

void printHeader() {
  Serial.println();
  Serial.println("[HW-006] SD_ACCEPTANCE_START NON_DESTRUCTIVE=true");
  Serial.printf(
      "[HW-006] SD_CLK=%d SD_CMD=%d SD_D0=%d MODE=1BIT "
      "FREQUENCY_KHZ=%d\n",
      kSdClockPin, kSdCommandPin, kSdData0Pin, kSdFrequencyKHz);
  Serial.printf("[HW-006] TEST_ROOT=%s LARGE_TRANSFER_BYTES=%u\n", kTestRoot,
                kLargeFileBytes);
}

}  // namespace

void setup() {
  Serial.begin(kSerialBaudRate);
  delay(kStartupCaptureDelayMs);
  printHeader();
  if (!configurePins()) {
    Serial.println("[HW-006] COMPLETE RESULT=FAIL");
    return;
  }
  if (!mountCard()) {
    printResult("MISSING_CARD_HANDLED", true);
    Serial.println("[HW-006][BLOCKED] CARD_ABSENT_OR_FILESYSTEM_UNMOUNTABLE");
    gBlocked = true;
  } else {
    runAcceptance();
    SD_MMC.end();
  }
  Serial.printf("[HW-006] COMPLETE RESULT=%s\n",
                gBlocked ? "BLOCKED" : (gAllPassed ? "PASS" : "FAIL"));
}

void loop() {
  delay(1000);
}
