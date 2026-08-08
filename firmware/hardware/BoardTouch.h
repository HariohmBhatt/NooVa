#pragma once

#include <TouchDrvFT6X36.hpp>

#include <cstdint>

namespace nova {

struct TouchPoint {
  bool pressed = false;
  int16_t x = 0;
  int16_t y = 0;
};

class BoardTouch {
 public:
  /** Construct the verified FT6336-compatible touch driver. */
  BoardTouch();

  /** Initialize the touch controller on the shared I2C bus. */
  bool begin();

  /** Read the first current touch point without allocating memory. */
  bool read(TouchPoint& point);

  /** Return whether touch initialization succeeded. */
  bool isReady() const;

 private:
  TouchDrvFT6X36 touch_;
  bool ready_ = false;
};

}  // namespace nova
