#include "BoardTouch.h"

namespace nova {

BoardTouch::BoardTouch() = default;

bool BoardTouch::begin() {
  pressed_ = false;
  ready_ = touch_.begin(Wire, FT6X36_SLAVE_ADDRESS);
  return ready_;
}

bool BoardTouch::read(TouchPoint& point) {
  point = {};
  if (!ready_) {
    pressed_ = false;
    return false;
  }

  int16_t x = 0;
  int16_t y = 0;
  const uint8_t count = touch_.getPoint(&x, &y, 1);
  point.pressed = count > 0;
  point.x = x;
  point.y = y;
  pressed_ = point.pressed;
  return true;
}

bool BoardTouch::isReady() const { return ready_; }

bool BoardTouch::isPressed() const { return pressed_; }

}  // namespace nova
