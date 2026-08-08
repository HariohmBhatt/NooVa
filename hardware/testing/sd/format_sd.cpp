#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>

#include <cstring>

namespace {

// These pins and 1-bit mode are copied from Waveshare's official 07_sd_card_test.
constexpr int kSdClockPin = 11;
constexpr int kSdCommandPin = 10;
constexpr int kSdData0Pin = 9;
constexpr bool kOneBitMode = true;
constexpr int kSdFrequencyHz = 20000;
constexpr char kMountPoint[] = "/sdcard";
constexpr char kCheckFile[] = "/nova-format-check.txt";
constexpr char kCheckContents[] = "NOVA-SD-FORMAT-CHECK\n";
constexpr uint32_t kSerialBaudRate = 115200;
constexpr uint32_t kStartupCaptureDelayMs = 2000;

bool gFormatArmed = false;
bool gCompleted = false;

void printResult(const char* check, bool passed) {
  Serial.printf("[HW-006][%s] %s\n", passed ? "PASS" : "FAIL", check);
}

void printCardInfo() {
  const uint8_t cardType = SD_MMC.cardType();
  Serial.printf("[HW-006] CARD_TYPE=%u CARD_SIZE_MB=%llu TOTAL_MB=%llu USED_MB=%llu\n",
                cardType, SD_MMC.cardSize() / (1024 * 1024),
                SD_MMC.totalBytes() / (1024 * 1024),
                SD_MMC.usedBytes() / (1024 * 1024));
}

bool configurePins() {
  const bool configured = SD_MMC.setPins(kSdClockPin, kSdCommandPin, kSdData0Pin);
  printResult("PINS_CONFIGURED", configured);
  return configured;
}

bool mountCard(bool formatIfMountFails) {
  const bool mounted = SD_MMC.begin(kMountPoint, kOneBitMode,
                                    formatIfMountFails, kSdFrequencyHz);
  Serial.printf("[HW-006] MOUNT_ATTEMPT FORMAT_IF_FAILED=%s RESULT=%s\n",
                formatIfMountFails ? "YES" : "NO", mounted ? "SUCCESS" : "FAIL");
  return mounted;
}

bool verifyFilesystem() {
  SD_MMC.remove(kCheckFile);
  File file = SD_MMC.open(kCheckFile, FILE_WRITE);
  if (!file) {
    return false;
  }
  const bool writePassed = file.print(kCheckContents) == sizeof(kCheckContents) - 1;
  file.close();
  if (!writePassed) {
    SD_MMC.remove(kCheckFile);
    return false;
  }

  file = SD_MMC.open(kCheckFile, FILE_READ);
  if (!file) {
    SD_MMC.remove(kCheckFile);
    return false;
  }
  char contents[sizeof(kCheckContents)] = {};
  const size_t bytesRead = file.readBytes(contents, sizeof(contents) - 1);
  file.close();
  const bool readPassed = bytesRead == sizeof(kCheckContents) - 1 &&
                          std::strcmp(contents, kCheckContents) == 0;
  const bool removePassed = SD_MMC.remove(kCheckFile);
  return readPassed && removePassed;
}

void formatCard() {
  if (!gFormatArmed || gCompleted) {
    return;
  }
  Serial.println("[HW-006] FORMAT_CONFIRMATION_RECEIVED");
  if (!mountCard(true)) {
    printResult("FORMAT_AND_MOUNT", false);
    return;
  }
  printResult("FORMAT_AND_MOUNT", true);
  printCardInfo();
  printResult("FILESYSTEM_WRITE_READ_DELETE", verifyFilesystem());
  gCompleted = true;
  Serial.println("[HW-006] FORMAT_COMPLETE");
}

void printHelp() {
  Serial.println("[HW-006] COMMANDS: F=format and verify, s=card status, h=help");
}

void handleCommand() {
  while (Serial.available()) {
    const char command = static_cast<char>(Serial.read());
    if (command == 'F' || command == 'f') {
      formatCard();
    } else if (command == 's' && gCompleted) {
      printCardInfo();
    } else if (command == 'h') {
      printHelp();
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(kSerialBaudRate);
  delay(kStartupCaptureDelayMs);
  Serial.println();
  Serial.println("[HW-006] SD_FORMAT_TEST_START");
  Serial.printf("[HW-006] SD_CLK=%d SD_CMD=%d SD_D0=%d MODE=1BIT FREQUENCY_HZ=%d\n",
                kSdClockPin, kSdCommandPin, kSdData0Pin, kSdFrequencyHz);
  Serial.println("[HW-006] FORMAT_IS_DESTRUCTIVE=true");

  if (!configurePins()) {
    Serial.println("[HW-006][FAIL] SD_FORMAT_TEST_HALTED");
    return;
  }

  if (mountCard(false)) {
    printResult("CARD_MOUNTED_WITHOUT_FORMAT", true);
    printCardInfo();
    printResult("FILESYSTEM_WRITE_READ_DELETE", verifyFilesystem());
    gCompleted = true;
    Serial.println("[HW-006] FORMAT_NOT_NEEDED FILESYSTEM_VERIFIED=true");
    return;
  }

  SD_MMC.end();
  gFormatArmed = true;
  Serial.println("[HW-006] FORMAT_ARMED");
  Serial.println("[HW-006] Inserted-card confirmation required. Send uppercase F to erase and format.");
  printHelp();
}

void loop() {
  handleCommand();
  delay(10);
}
