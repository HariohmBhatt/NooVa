#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <TCA9554.h>
#include <TouchDrvFT6X36.hpp>

namespace {

// These values are from Waveshare's official Arduino examples.
constexpr uint8_t kBacklightPin = 6;
constexpr uint8_t kSpiMisoPin = 2;
constexpr uint8_t kSpiMosiPin = 1;
constexpr uint8_t kSpiClockPin = 5;
constexpr int8_t kLcdChipSelectPin = -1;
constexpr uint8_t kLcdDataCommandPin = 3;
constexpr int8_t kLcdResetPin = -1;
constexpr uint16_t kDisplayWidth = 320;
constexpr uint16_t kDisplayHeight = 480;
constexpr uint8_t kI2cSdaPin = 8;
constexpr uint8_t kI2cSclPin = 7;
constexpr uint8_t kIoExpanderAddress = 0x20;
constexpr uint8_t kLcdResetExpanderPin = 1;

constexpr uint32_t kSerialBaudRate = 115200;
constexpr uint32_t kSerialConnectTimeoutMs = 3000;
constexpr uint32_t kPollIntervalMs = 20;
constexpr uint32_t kContactLogIntervalMs = 250;
constexpr uint32_t kIdleReportIntervalMs = 10000;
constexpr uint8_t kRequestedTouchPoints = 2;
constexpr uint16_t kGridSpacing = 40;

TCA9554 ioExpander(kIoExpanderAddress);
Arduino_ESP32SPI displayBus(kLcdDataCommandPin, kLcdChipSelectPin,
                            kSpiClockPin, kSpiMosiPin, kSpiMisoPin);
Arduino_ST7796 display(&displayBus, kLcdResetPin, 0, true, kDisplayWidth,
                      kDisplayHeight);
TouchDrvFT6X36 touch;

uint8_t gRotation = 0;
uint8_t gPreviousPointCount = 0;
uint32_t gLastPollAt = 0;
uint32_t gLastContactLogAt = 0;
uint32_t gLastIdleReportAt = 0;
uint32_t gPressCount = 0;
uint32_t gReleaseCount = 0;
uint32_t gContactSampleCount = 0;
uint32_t gMultiTouchSampleCount = 0;
bool gTouchInitialized = false;

void printResult(const char* check, bool passed) {
  Serial.printf("[HW-002][%s] %s\n", passed ? "PASS" : "FAIL", check);
}

void printHeader() {
  Serial.println();
  Serial.println("[HW-002] TOUCH_TEST_START");
  Serial.println("[HW-002] CONTROLLER=FT6336_USING_FT6X36_DRIVER");
  Serial.printf("[HW-002] DISPLAY_GEOMETRY=%ux%u\n", kDisplayWidth,
                kDisplayHeight);
  Serial.printf("[HW-002] I2C_SDA=%u I2C_SCL=%u TOUCH_ADDRESS=0x%02X\n",
                kI2cSdaPin, kI2cSclPin, FT6X36_SLAVE_ADDRESS);
  Serial.printf("[HW-002] REQUESTED_TOUCH_POINTS=%u\n",
                kRequestedTouchPoints);
  Serial.println("[HW-002] CONTACT_IDS=NOT_EXPOSED_BY_DRIVER");
}

void resetDisplayThroughExpander() {
  ioExpander.write1(kLcdResetExpanderPin, HIGH);
  delay(10);
  ioExpander.write1(kLcdResetExpanderPin, LOW);
  delay(10);
  ioExpander.write1(kLcdResetExpanderPin, HIGH);
  delay(200);
}

bool initializeHardware() {
  Wire.begin(kI2cSdaPin, kI2cSclPin);
  const bool ioConnected = ioExpander.begin();
  printResult("TCA9554_CONNECTED", ioConnected);
  if (!ioConnected || !ioExpander.pinMode1(kLcdResetExpanderPin, OUTPUT)) {
    printResult("TCA9554_RESET_OUTPUT", false);
    return false;
  }

  resetDisplayThroughExpander();
  const bool touchStarted = touch.begin(Wire, FT6X36_SLAVE_ADDRESS);
  printResult("FT6X36_INITIALIZED", touchStarted);
  if (!touchStarted) {
    return false;
  }

  const bool displayStarted = display.begin();
  printResult("ST7796_INITIALIZED", displayStarted);
  if (!displayStarted) {
    return false;
  }

  display.fillScreen(BLACK);
  pinMode(kBacklightPin, OUTPUT);
  digitalWrite(kBacklightPin, HIGH);
  gTouchInitialized = true;
  return true;
}

void drawTouchBackground() {
  const int16_t width = display.width();
  const int16_t height = display.height();
  display.fillScreen(BLACK);
  display.drawRect(0, 0, width, height, WHITE);
  for (int16_t x = kGridSpacing; x < width; x += kGridSpacing) {
    display.drawFastVLine(x, 0, height, 0x7BEF);
  }
  for (int16_t y = kGridSpacing; y < height; y += kGridSpacing) {
    display.drawFastHLine(0, y, width, 0x7BEF);
  }
  display.setTextSize(2, 2, 1);
  display.setTextColor(WHITE);
  display.setCursor(8, 8);
  display.print("HW-002 TOUCH");
  display.setCursor(8, height - 24);
  display.printf("ROT=%u", gRotation);
}

void drawTouchPoints(const int16_t* x, const int16_t* y, uint8_t count) {
  drawTouchBackground();
  for (uint8_t index = 0; index < count; ++index) {
    const uint16_t color = index == 0 ? RED : CYAN;
    display.drawCircle(x[index], y[index], 14, color);
    display.drawFastHLine(x[index] - 20, y[index], 40, color);
    display.drawFastVLine(x[index], y[index] - 20, 40, color);
    display.setCursor(8, 32 + index * 20);
    display.setTextColor(color);
    display.printf("P%u: %d,%d", index, x[index], y[index]);
  }
}

void printPointState(uint8_t count, const int16_t* x, const int16_t* y) {
  Serial.printf("[HW-002] POINTS=%u\n", count);
  for (uint8_t index = 0; index < count; ++index) {
    Serial.printf("[HW-002] POINT_INDEX=%u X=%d Y=%d\n", index, x[index],
                  y[index]);
  }
}

void processTouch() {
  int16_t x[kRequestedTouchPoints] = {};
  int16_t y[kRequestedTouchPoints] = {};
  uint8_t pointCount = touch.getPoint(x, y, kRequestedTouchPoints);
  if (pointCount > kRequestedTouchPoints) {
    Serial.printf("[HW-002] RAW_POINTS=%u BUFFERED_POINTS=%u\n", pointCount,
                  kRequestedTouchPoints);
    pointCount = kRequestedTouchPoints;
  }

  const uint32_t now = millis();
  const bool started = gPreviousPointCount == 0 && pointCount > 0;
  const bool released = gPreviousPointCount > 0 && pointCount == 0;
  const bool countChanged = pointCount != gPreviousPointCount;
  const bool logContact = pointCount > 0 &&
                          (started || countChanged ||
                           now - gLastContactLogAt >= kContactLogIntervalMs);

  if (started) {
    ++gPressCount;
    Serial.printf("[HW-002] EVENT=PUT_DOWN COUNT=%lu\n", gPressCount);
  }
  if (released) {
    ++gReleaseCount;
    Serial.printf("[HW-002] EVENT=PUT_UP COUNT=%lu\n", gReleaseCount);
  }
  if (pointCount > 0) {
    ++gContactSampleCount;
    if (pointCount > 1) {
      ++gMultiTouchSampleCount;
    }
    if (logContact) {
      Serial.printf("[HW-002] EVENT=CONTACT POINT_COUNT=%u\n", pointCount);
      printPointState(pointCount, x, y);
      gLastContactLogAt = now;
    }
    drawTouchPoints(x, y, pointCount);
  } else if (now - gLastIdleReportAt >= kIdleReportIntervalMs) {
    gLastIdleReportAt = now;
    Serial.printf("[HW-002] EVENT=IDLE RELEASES=%lu\n", gReleaseCount);
    drawTouchBackground();
  }

  gPreviousPointCount = pointCount;
}

void printSummary() {
  Serial.printf("[HW-002] SUMMARY PRESSES=%lu RELEASES=%lu CONTACT_SAMPLES=%lu "
                "MULTI_TOUCH_SAMPLES=%lu ROTATION=%u\n",
                gPressCount, gReleaseCount, gContactSampleCount,
                gMultiTouchSampleCount, gRotation);
}

void printHelp() {
  Serial.println("[HW-002] COMMANDS: 0-3=set rotation, r=next rotation, c=clear, s=summary, h=help");
}

void handleCommand() {
  while (Serial.available()) {
    const char command = static_cast<char>(Serial.read());
    if (command >= '0' && command <= '3') {
      gRotation = static_cast<uint8_t>(command - '0');
      display.setRotation(gRotation);
      drawTouchBackground();
      Serial.printf("[HW-002] ROTATION=%u WIDTH=%d HEIGHT=%d\n", gRotation,
                    display.width(), display.height());
    } else if (command == 'r') {
      gRotation = static_cast<uint8_t>((gRotation + 1) % 4);
      display.setRotation(gRotation);
      drawTouchBackground();
      Serial.printf("[HW-002] ROTATION=%u WIDTH=%d HEIGHT=%d\n", gRotation,
                    display.width(), display.height());
    } else if (command == 'c') {
      drawTouchBackground();
      Serial.println("[HW-002] DISPLAY_CLEARED");
    } else if (command == 's') {
      printSummary();
    } else if (command == 'h') {
      printHelp();
    }
  }
}

void haltAfterFailure() {
  Serial.println("[HW-002][FAIL] TOUCH_TEST_HALTED");
  while (true) {
    delay(1000);
    Serial.println("[HW-002][FAIL] RESET_REQUIRED");
  }
}

}  // namespace

void setup() {
  Serial.begin(kSerialBaudRate);
  const uint32_t serialStartedAt = millis();
  while (!Serial && millis() - serialStartedAt < kSerialConnectTimeoutMs) {
    delay(10);
  }

  printHeader();
  if (!initializeHardware()) {
    haltAfterFailure();
  }

  drawTouchBackground();
  printResult("DISPLAY_DIMENSIONS_320X480", display.width() == kDisplayWidth &&
                                             display.height() == kDisplayHeight);
  Serial.printf("[HW-002] CHIP_ID=0x%02lX MODEL=%s VENDOR_ID=0x%02X\n",
                touch.getChipID(), touch.getModelName(), touch.getVendorID());
  Serial.printf("[HW-002] LIBRARY_VERSION=0x%04X ERROR_CODE=0x%02X\n",
                touch.getLibraryVersion(), touch.getErrorCode());
  Serial.println("[HW-002] TOUCH_TEST_READY");
  printHelp();
}

void loop() {
  handleCommand();
  const uint32_t now = millis();
  if (gTouchInitialized && now - gLastPollAt >= kPollIntervalMs) {
    gLastPollAt = now;
    processTouch();
  }
  delay(1);
}
