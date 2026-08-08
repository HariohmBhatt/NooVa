#include <Arduino.h>
#include <Wire.h>

#include <REG/AXP2101Constants.h>
#include <REG/FT6X36Constants.h>
#include <REG/PCF85063Constants.h>
#include <REG/QMI8658Constants.h>
#include <esp_heap_caps.h>

#include <cstring>

namespace {

// Waveshare's official examples use this shared I2C bus and these addresses.
constexpr uint8_t kI2cSdaPin = 8;
constexpr uint8_t kI2cSclPin = 7;
constexpr uint32_t kI2cFrequencyHz = 100000;
constexpr uint8_t kTca9554Address = 0x20;
constexpr uint8_t kEs8311Address = 0x18;

constexpr uint32_t kExpectedFlashBytes = 16U * 1024U * 1024U;
constexpr uint32_t kExpectedPsramBytes = 8U * 1024U * 1024U;
constexpr size_t kPsramProbeBytes = 64U * 1024U;
constexpr uint32_t kSerialBaudRate = 115200;
constexpr uint32_t kStartupCaptureDelayMs = 2000;
constexpr uint32_t kRtcSampleDelayMs = 1200;

class Pcf85063RegisterMap final : public PCF85063Constants {
 public:
  /** Return the official PCF85063 I2C slave address. */
  static constexpr uint8_t address() { return PCF85063_SLAVE_ADDRESS; }

  /** Return the first read-only time register. */
  static constexpr uint8_t secondsRegister() { return PCF85063_SEC_REG; }
};

struct ExpectedI2cDevice {
  const char* name;
  uint8_t address;
};

constexpr ExpectedI2cDevice kExpectedI2cDevices[] = {
    {"ES8311", kEs8311Address},
    {"TCA9554", kTca9554Address},
    {"AXP2101", AXP2101_SLAVE_ADDRESS},
    {"FT6336", FT6X36_SLAVE_ADDRESS},
    {"PCF85063", Pcf85063RegisterMap::address()},
    {"QMI8658", QMI8658_L_SLAVE_ADDRESS},
};

struct RtcTime {
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
  bool clockIntegrityLost;
};

bool gAllPassed = true;

void printResult(const char* check, bool passed) {
  Serial.printf("[BOARD-DIAG][%s] %s\n", passed ? "PASS" : "FAIL", check);
  gAllPassed = gAllPassed && passed;
}

uint8_t psramPattern(size_t offset) {
  return static_cast<uint8_t>((offset * 131U + 0x5AU) ^ (offset >> 8U));
}

void testBoardIdentityAndMemory() {
  const char* model = ESP.getChipModel();
  const uint32_t flashBytes = ESP.getFlashChipSize();
  const uint32_t psramBytes = ESP.getPsramSize();
  Serial.printf("[BOARD-DIAG] CHIP=%s REVISION=%u CORES=%u\n", model,
                ESP.getChipRevision(), ESP.getChipCores());
  Serial.printf("[BOARD-DIAG] FLASH_BYTES=%lu EXPECTED=%lu\n", flashBytes,
                kExpectedFlashBytes);
  Serial.printf("[BOARD-DIAG] PSRAM_BYTES=%lu EXPECTED=%lu\n", psramBytes,
                kExpectedPsramBytes);
  printResult("ESP32_S3_IDENTITY", std::strcmp(model, "ESP32-S3") == 0);
  printResult("FLASH_EXACT_16MB", flashBytes == kExpectedFlashBytes);
  printResult("PSRAM_EXACT_8MB", psramBytes == kExpectedPsramBytes);
}

void testPsramPattern() {
  auto* probe = static_cast<uint8_t*>(
      heap_caps_malloc(kPsramProbeBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (probe == nullptr) {
    printResult("PSRAM_PATTERN", false);
    return;
  }
  for (size_t offset = 0; offset < kPsramProbeBytes; ++offset) {
    probe[offset] = psramPattern(offset);
  }
  size_t mismatch = kPsramProbeBytes;
  for (size_t offset = 0; offset < kPsramProbeBytes; ++offset) {
    if (probe[offset] != psramPattern(offset)) {
      mismatch = offset;
      break;
    }
  }
  heap_caps_free(probe);
  Serial.printf("[BOARD-DIAG] PSRAM_PROBE_BYTES=%u MISMATCH_OFFSET=%d\n",
                kPsramProbeBytes,
                mismatch == kPsramProbeBytes ? -1 : static_cast<int>(mismatch));
  printResult("PSRAM_PATTERN", mismatch == kPsramProbeBytes);
}

bool readRegisters(uint8_t address, uint8_t firstRegister, uint8_t* values,
                   uint8_t length) {
  Wire.beginTransmission(address);
  Wire.write(firstRegister);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(address, static_cast<size_t>(length), true) != length) {
    return false;
  }
  for (uint8_t index = 0; index < length; ++index) {
    if (!Wire.available()) {
      return false;
    }
    values[index] = static_cast<uint8_t>(Wire.read());
  }
  return true;
}

bool scanI2cBus(bool* found) {
  uint8_t deviceCount = 0;
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    found[address] = Wire.endTransmission(true) == 0;
    if (found[address]) {
      ++deviceCount;
      Serial.printf("[BOARD-DIAG] I2C_FOUND=0x%02X\n", address);
    }
  }
  Serial.printf("[BOARD-DIAG] I2C_DEVICE_COUNT=%u\n", deviceCount);
  return deviceCount > 0;
}

void testExpectedI2cDevices() {
  bool found[127] = {};
  bool allExpectedFound = scanI2cBus(found);
  for (const ExpectedI2cDevice& device : kExpectedI2cDevices) {
    const bool present = found[device.address];
    Serial.printf("[BOARD-DIAG] I2C_EXPECTED=%s ADDRESS=0x%02X PRESENT=%s\n",
                  device.name, device.address, present ? "YES" : "NO");
    allExpectedFound = allExpectedFound && present;
  }
  printResult("I2C_EXPECTED_DEVICES", allExpectedFound);
}

uint8_t fromBcd(uint8_t value) {
  return static_cast<uint8_t>((value >> 4U) * 10U + (value & 0x0FU));
}

uint8_t daysInMonth(uint16_t year, uint8_t month) {
  constexpr uint8_t kMonthDays[] = {31, 28, 31, 30, 31, 30,
                                    31, 31, 30, 31, 30, 31};
  if (month == 2 && year % 4U == 0) {
    return 29;
  }
  return month >= 1 && month <= 12 ? kMonthDays[month - 1] : 0;
}

bool decodeRtcTime(const uint8_t* data, RtcTime& time) {
  time.clockIntegrityLost = (data[0] & 0x80U) != 0;
  time.second = fromBcd(data[0] & 0x7FU);
  time.minute = fromBcd(data[1] & 0x7FU);
  time.hour = fromBcd(data[2] & 0x3FU);
  time.day = fromBcd(data[3] & 0x3FU);
  time.month = fromBcd(data[5] & 0x1FU);
  time.year = 2000U + fromBcd(data[6]);
  return time.second < 60 && time.minute < 60 && time.hour < 24 &&
         time.month >= 1 && time.month <= 12 && time.day >= 1 &&
         time.day <= daysInMonth(time.year, time.month);
}

uint32_t rtcSecondsSince2000(const RtcTime& time) {
  uint32_t days = 0;
  for (uint16_t year = 2000; year < time.year; ++year) {
    days += year % 4U == 0 ? 366U : 365U;
  }
  for (uint8_t month = 1; month < time.month; ++month) {
    days += daysInMonth(time.year, month);
  }
  days += time.day - 1U;
  return ((days * 24U + time.hour) * 60U + time.minute) * 60U + time.second;
}

bool readRtcTime(RtcTime& time) {
  uint8_t data[7] = {};
  return readRegisters(Pcf85063RegisterMap::address(),
                       Pcf85063RegisterMap::secondsRegister(), data,
                       sizeof(data)) &&
         decodeRtcTime(data, time);
}

void printRtcTime(const char* sample, const RtcTime& time) {
  Serial.printf(
      "[BOARD-DIAG] RTC_SAMPLE=%s TIME=%04u-%02u-%02uT%02u:%02u:%02u "
      "INTEGRITY_LOST=%s\n",
      sample, time.year, time.month, time.day, time.hour, time.minute,
      time.second, time.clockIntegrityLost ? "YES" : "NO");
}

void testRtcMonotonicRead() {
  RtcTime first = {};
  RtcTime second = {};
  const bool firstRead = readRtcTime(first);
  if (firstRead) {
    printRtcTime("FIRST", first);
  }
  delay(kRtcSampleDelayMs);
  const bool secondRead = readRtcTime(second);
  if (secondRead) {
    printRtcTime("SECOND", second);
  }
  const uint32_t firstSeconds = firstRead ? rtcSecondsSince2000(first) : 0;
  const uint32_t secondSeconds = secondRead ? rtcSecondsSince2000(second) : 0;
  const bool monotonic = firstRead && secondRead && !first.clockIntegrityLost &&
                         !second.clockIntegrityLost &&
                         secondSeconds > firstSeconds &&
                         secondSeconds - firstSeconds <= 3U;
  printResult("RTC_MONOTONIC", monotonic);
}

uint16_t decodeAdc14(uint8_t high, uint8_t low) {
  return static_cast<uint16_t>((high & 0x3FU) << 8U) | low;
}

uint16_t decodeAdc13(uint8_t high, uint8_t low) {
  return static_cast<uint16_t>((high & 0x1FU) << 8U) | low;
}

void testAxp2101ReadOnlyTelemetry() {
  uint8_t chipId = 0;
  uint8_t status[2] = {};
  uint8_t adcControl = 0;
  uint8_t adc[10] = {};
  uint8_t batteryPercent = 0;
  const bool readPassed =
      readRegisters(AXP2101_SLAVE_ADDRESS, XPOWERS_AXP2101_IC_TYPE, &chipId, 1) &&
      readRegisters(AXP2101_SLAVE_ADDRESS, XPOWERS_AXP2101_STATUS1, status, 2) &&
      readRegisters(AXP2101_SLAVE_ADDRESS, XPOWERS_AXP2101_ADC_CHANNEL_CTRL,
                    &adcControl, 1) &&
      readRegisters(AXP2101_SLAVE_ADDRESS, XPOWERS_AXP2101_ADC_DATA_RELUST0,
                    adc, sizeof(adc)) &&
      readRegisters(AXP2101_SLAVE_ADDRESS, XPOWERS_AXP2101_BAT_PERCENT_DATA,
                    &batteryPercent, 1);
  Serial.printf(
      "[BOARD-DIAG] AXP2101_ID=0x%02X STATUS1=0x%02X STATUS2=0x%02X "
      "ADC_ENABLE=0x%02X\n",
      chipId, status[0], status[1], adcControl);
  Serial.printf(
      "[BOARD-DIAG] AXP2101_BATTERY_MV=%u VBUS_MV=%u SYSTEM_MV=%u "
      "BATTERY_PERCENT=%u\n",
      decodeAdc13(adc[0], adc[1]), decodeAdc14(adc[4], adc[5]),
      decodeAdc14(adc[6], adc[7]), batteryPercent);
  printResult("AXP2101_READ_ONLY_TELEMETRY",
              readPassed && chipId == XPOWERS_AXP2101_CHIP_ID);
}

}  // namespace

void setup() {
  Serial.begin(kSerialBaudRate);
  delay(kStartupCaptureDelayMs);
  Serial.println();
  Serial.println("[BOARD-DIAG] START READ_ONLY=true MUTATIONS=NONE");
  testBoardIdentityAndMemory();
  testPsramPattern();
  Wire.begin(kI2cSdaPin, kI2cSclPin, kI2cFrequencyHz);
  testExpectedI2cDevices();
  testRtcMonotonicRead();
  testAxp2101ReadOnlyTelemetry();
  Serial.printf("[BOARD-DIAG] COMPLETE RESULT=%s\n",
                gAllPassed ? "PASS" : "FAIL");
}

void loop() {
  delay(1000);
}
