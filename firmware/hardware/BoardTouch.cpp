#include "BoardTouch.h"

namespace nova {

BoardTouch::BoardTouch() = default;

bool BoardTouch::begin() {
  ready_ = touch_.begin(Wire, FT6X36_SLAVE_ADDRESS);
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
  point.x = x;
  point.y = y;
  return true;
}

bool BoardTouch::isReady() const { return ready_; }

}  // namespace nova
