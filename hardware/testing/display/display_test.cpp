#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <TCA9554.h>

namespace {

// These values are from Waveshare's official 08_gfx_helloworld example.
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
constexpr uint32_t kColorHoldMs = 1500;
constexpr uint32_t kGeometryHoldMs = 5000;
constexpr uint32_t kRotationHoldMs = 4000;
constexpr uint32_t kBacklightHoldMs = 2000;
constexpr uint32_t kSoakDurationMs = 10UL * 60UL * 1000UL;
constexpr uint32_t kSoakFrameDelayMs = 250;
constexpr uint32_t kSoakReportPeriodMs = 10000;
constexpr uint16_t kGridSpacing = 40;

constexpr uint16_t kNeutralGray = 0x8410;
constexpr uint16_t kGridColor = 0x7BEF;

struct ColorStep {
  const char* name;
  uint16_t value;
};

constexpr ColorStep kColorSteps[] = {
    {"BLACK", BLACK},
    {"WHITE", WHITE},
    {"RED", RED},
    {"GREEN", GREEN},
    {"BLUE", BLUE},
    {"YELLOW", YELLOW},
    {"CYAN", CYAN},
    {"MAGENTA", MAGENTA},
    {"GRAY", kNeutralGray},
};

constexpr uint16_t kSoakColors[] = {BLACK, WHITE, RED, GREEN, BLUE};
constexpr size_t kColorStepCount = sizeof(kColorSteps) / sizeof(kColorSteps[0]);
constexpr size_t kSoakColorCount = sizeof(kSoakColors) / sizeof(kSoakColors[0]);

TCA9554 ioExpander(kIoExpanderAddress);
Arduino_ESP32SPI displayBus(kLcdDataCommandPin, kLcdChipSelectPin,
                            kSpiClockPin, kSpiMosiPin, kSpiMisoPin);
Arduino_ST7796 display(&displayBus, kLcdResetPin, 0, true, kDisplayWidth,
                      kDisplayHeight);

bool gAutomatedChecksPassed = true;
bool gVisualReviewRequired = true;
uint32_t gTestStartedAt = 0;

void printResult(const char* check, bool passed) {
  Serial.printf("[HW-001][%s] %s\n", passed ? "PASS" : "FAIL", check);
  gAutomatedChecksPassed = gAutomatedChecksPassed && passed;
}

void printHeader() {
  Serial.println();
  Serial.println("[HW-001] DISPLAY_TEST_START");
  Serial.println("[HW-001] PANEL=ST7796");
  Serial.printf("[HW-001] GEOMETRY=%ux%u\n", kDisplayWidth, kDisplayHeight);
  Serial.printf("[HW-001] SPI_MISO=%u SPI_MOSI=%u SPI_SCLK=%u\n", kSpiMisoPin,
                kSpiMosiPin, kSpiClockPin);
  Serial.printf("[HW-001] LCD_CS=%d LCD_DC=%u LCD_RST=%d\n", kLcdChipSelectPin,
                kLcdDataCommandPin, kLcdResetPin);
  Serial.printf("[HW-001] I2C_SDA=%u I2C_SCL=%u TCA9554=0x%02X RESET_PIN=%u\n",
                kI2cSdaPin, kI2cSclPin, kIoExpanderAddress,
                kLcdResetExpanderPin);
  Serial.printf("[HW-001] BACKLIGHT_GPIO=%u\n", kBacklightPin);
}

void resetDisplayThroughExpander() {
  ioExpander.write1(kLcdResetExpanderPin, HIGH);
  delay(10);
  ioExpander.write1(kLcdResetExpanderPin, LOW);
  delay(10);
  ioExpander.write1(kLcdResetExpanderPin, HIGH);
  delay(200);
}

bool initializeDisplay() {
  Wire.begin(kI2cSdaPin, kI2cSclPin);
  const bool ioConnected = ioExpander.begin();
  printResult("TCA9554_CONNECTED", ioConnected);
  if (!ioConnected || !ioExpander.pinMode1(kLcdResetExpanderPin, OUTPUT)) {
    printResult("TCA9554_RESET_OUTPUT", false);
    return false;
  }

  resetDisplayThroughExpander();
  const bool displayStarted = display.begin();
  printResult("ST7796_INITIALIZED", displayStarted);
  if (!displayStarted) {
    return false;
  }

  display.fillScreen(BLACK);
  pinMode(kBacklightPin, OUTPUT);
  analogWrite(kBacklightPin, 255);
  Serial.printf("[HW-001][PASS] DISPLAY_DIMENSIONS=%dx%d\n", display.width(),
                display.height());
  return display.width() == kDisplayWidth && display.height() == kDisplayHeight;
}

void runColorTest() {
  Serial.println("[HW-001] COLOR_TEST_START visual_review_required=true");
  for (const ColorStep& step : kColorSteps) {
    display.fillScreen(step.value);
    Serial.printf("[HW-001] COLOR=%s RGB565=0x%04X HOLD_MS=%lu\n", step.name,
                  step.value, kColorHoldMs);
    delay(kColorHoldMs);
  }
  printResult("COLOR_SEQUENCE_RENDERED", true);
}

void drawGeometryPattern(const char* label) {
  const int16_t width = display.width();
  const int16_t height = display.height();
  display.fillScreen(BLACK);
  display.drawRect(0, 0, width, height, WHITE);
  display.drawRect(1, 1, width - 2, height - 2, kNeutralGray);

  for (int16_t x = kGridSpacing; x < width; x += kGridSpacing) {
    display.drawFastVLine(x, 0, height, kGridColor);
  }
  for (int16_t y = kGridSpacing; y < height; y += kGridSpacing) {
    display.drawFastHLine(0, y, width, kGridColor);
  }

  display.drawLine(0, 0, width - 1, height - 1, RED);
  display.drawLine(width - 1, 0, 0, height - 1, GREEN);
  display.fillCircle(width / 2, height / 2, 8, BLUE);
  display.setTextSize(2, 2, 1);
  display.setTextColor(WHITE);
  display.setCursor(8, 8);
  display.print(label);
  display.setCursor(8, height - 24);
  display.print("0,0");
  display.setCursor(width - 72, height - 24);
  display.print("MAX");
}

void runGeometryTest() {
  Serial.printf("[HW-001] GEOMETRY_TEST_START WIDTH=%d HEIGHT=%d\n", display.width(),
                display.height());
  drawGeometryPattern("HW-001 GEOMETRY");
  Serial.printf("[HW-001] GEOMETRY_PATTERN HOLD_MS=%lu\n", kGeometryHoldMs);
  delay(kGeometryHoldMs);
  printResult("GEOMETRY_PATTERN_RENDERED", true);
}

void runRotationTest() {
  Serial.println("[HW-001] ROTATION_TEST_START visual_review_required=true");
  for (uint8_t rotation = 0; rotation < 4; ++rotation) {
    display.setRotation(rotation);
    drawGeometryPattern("HW-001 ROTATION");
    Serial.printf("[HW-001] ROTATION=%u WIDTH=%d HEIGHT=%d HOLD_MS=%lu\n", rotation,
                  display.width(), display.height(), kRotationHoldMs);
    delay(kRotationHoldMs);
  }
  display.setRotation(0);
  printResult("ROTATION_SEQUENCE_RENDERED", true);
}

void runBacklightTest() {
  constexpr uint8_t kBacklightLevels[] = {0, 64, 128, 255};
  constexpr size_t kBacklightLevelCount =
      sizeof(kBacklightLevels) / sizeof(kBacklightLevels[0]);

  Serial.println("[HW-001] BACKLIGHT_TEST_START visual_review_required=true");
  for (size_t index = 0; index < kBacklightLevelCount; ++index) {
    analogWrite(kBacklightPin, kBacklightLevels[index]);
    Serial.printf("[HW-001] BACKLIGHT_PWM=%u HOLD_MS=%lu\n",
                  kBacklightLevels[index], kBacklightHoldMs);
    delay(kBacklightHoldMs);
  }
  analogWrite(kBacklightPin, 255);
  printResult("BACKLIGHT_SEQUENCE_RENDERED", true);
}

void drawSoakFrame(uint32_t frame) {
  display.fillScreen(kSoakColors[frame % kSoakColorCount]);
  display.setRotation(0);
  display.drawRect(0, 0, display.width(), display.height(), WHITE);
  display.drawFastHLine(0, display.height() / 2, display.width(), BLACK);
  display.drawFastVLine(display.width() / 2, 0, display.height(), BLACK);
}

void runSoakTest() {
  Serial.printf("[HW-001] SOAK_TEST_START DURATION_MS=%lu FRAME_DELAY_MS=%lu\n",
                kSoakDurationMs, kSoakFrameDelayMs);
  const uint32_t startedAt = millis();
  uint32_t lastReportAt = startedAt;
  uint32_t frame = 0;

  while (millis() - startedAt < kSoakDurationMs) {
    drawSoakFrame(frame++);
    delay(kSoakFrameDelayMs);
    if (millis() - lastReportAt >= kSoakReportPeriodMs) {
      lastReportAt = millis();
      Serial.printf("[HW-001] SOAK_PROGRESS ELAPSED_MS=%lu FRAMES=%lu\n",
                    lastReportAt - startedAt, frame);
    }
  }

  drawGeometryPattern("HW-001 COMPLETE");
  analogWrite(kBacklightPin, 255);
  printResult("TEN_MINUTE_SOAK_COMPLETE", true);
}

void haltAfterFailure() {
  Serial.println("[HW-001][FAIL] DISPLAY_TEST_HALTED");
  while (true) {
    delay(1000);
    Serial.println("[HW-001][FAIL] RESET_REQUIRED");
  }
}

void printSummary() {
  Serial.printf("[HW-001] AUTOMATED_CHECKS=%s\n",
                gAutomatedChecksPassed ? "PASS" : "FAIL");
  Serial.printf("[HW-001] VISUAL_CHECKS=%s\n",
                gVisualReviewRequired ? "REQUIRED" : "PASS");
  Serial.printf("[HW-001] TEST_ELAPSED_MS=%lu\n", millis() - gTestStartedAt);
  Serial.println("[HW-001] DISPLAY_TEST_COMPLETE");
}

}  // namespace

void setup() {
  Serial.begin(kSerialBaudRate);
  const uint32_t serialStartedAt = millis();
  while (!Serial && millis() - serialStartedAt < kSerialConnectTimeoutMs) {
    delay(10);
  }

  gTestStartedAt = millis();
  printHeader();
  if (!initializeDisplay()) {
    haltAfterFailure();
  }

  runColorTest();
  runGeometryTest();
  runRotationTest();
  runBacklightTest();
  runSoakTest();
  printSummary();
}

void loop() {
  static uint32_t lastSummaryAt = 0;
  if (millis() - lastSummaryAt >= 10000) {
    lastSummaryAt = millis();
    printSummary();
  }
  delay(10);
}
