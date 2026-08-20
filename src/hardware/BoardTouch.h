#pragma once

#include <TouchDrvFT6X36.hpp>

#include <cstdint>

namespace nova {

struct TouchPoint {
  bool pressed = false;
  int16_t x = 0;
  int16_t y = 0;
};

/** Narrow single-pointer adapter for the FT6336-compatible controller. */
class BoardTouch {
 public:
  bool begin();
  bool read(TouchPoint& point);
  bool isReady() const;

 private:
  TouchDrvFT6X36 touch_;
  bool ready_ = false;
};

}  // namespace nova
