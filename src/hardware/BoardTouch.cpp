#include "BoardTouch.h"

#include <Wire.h>

#include "config/BoardConfig.h"

namespace nova {

bool BoardTouch::begin() {
  // Repeating begin with the same pins is safe and lets this adapter remain
  // independently usable in focused hardware smoke firmware.
  Wire.begin(board::kI2cSdaPin, board::kI2cSclPin);
  ready_ = touch_.begin(Wire, board::kTouchAddress);
  return ready_;
}

bool BoardTouch::read(TouchPoint& point) {
  point = {};
  if (!ready_) {
    return false;
  }

  int16_t x = 0;
  int16_t y = 0;
  const uint8_t count = touch_.getPoint(&x, &y, 1);
  point.pressed = count > 0;
  if (point.pressed) {
    // Guard the UI against occasional out-of-panel controller samples.
    point.x = x < 0 ? 0 : (x >= board::kDisplayWidth ? board::kDisplayWidth - 1 : x);
    point.y = y < 0 ? 0 : (y >= board::kDisplayHeight ? board::kDisplayHeight - 1 : y);
  }
  return true;
}

bool BoardTouch::isReady() const { return ready_; }

}  // namespace nova
