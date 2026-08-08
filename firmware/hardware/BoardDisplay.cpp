#include "BoardDisplay.h"

#include "BoardPins.h"

namespace nova {

BoardDisplay::BoardDisplay()
    : ioExpander_(board::kIoExpanderAddress),
      displayBus_(board::kLcdDataCommandPin, board::kLcdChipSelectPin,
                  board::kSpiClockPin, board::kSpiMosiPin, board::kSpiMisoPin),
      display_(&displayBus_, board::kLcdResetPin, 0, true,
               board::kDisplayWidth, board::kDisplayHeight) {}

bool BoardDisplay::begin() {
  Wire.begin(board::kI2cSdaPin, board::kI2cSclPin);
  if (!ioExpander_.begin() ||
      !ioExpander_.pinMode1(board::kLcdResetExpanderPin, OUTPUT)) {
    return false;
  }

  resetPanel();
  ready_ = display_.begin();
  if (!ready_) {
    return false;
  }

  display_.fillScreen(BLACK);
  pinMode(board::kBacklightPin, OUTPUT);
  setBacklight(255);
  return true;
}

bool BoardDisplay::isReady() const { return ready_; }

void BoardDisplay::setBacklight(uint8_t level) {
  analogWrite(board::kBacklightPin, level);
}

void BoardDisplay::drawPixels(int16_t x, int16_t y, const uint16_t* pixels,
                              int16_t width, int16_t height) {
  if (!ready_ || pixels == nullptr || width <= 0 || height <= 0) {
    return;
  }
  display_.draw16bitRGBBitmap(x, y, pixels, width, height);
}

void BoardDisplay::resetPanel() {
  ioExpander_.write1(board::kLcdResetExpanderPin, HIGH);
  delay(10);
  ioExpander_.write1(board::kLcdResetExpanderPin, LOW);
  delay(10);
  ioExpander_.write1(board::kLcdResetExpanderPin, HIGH);
  delay(200);
}

}  // namespace nova
