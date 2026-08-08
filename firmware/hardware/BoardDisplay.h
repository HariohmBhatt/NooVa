#pragma once

#include <Arduino_GFX_Library.h>
#include <TCA9554.h>

#include <cstdint>

namespace nova {

class BoardDisplay {
 public:
  /** Construct the verified ST7796 display driver. */
  BoardDisplay();

  /** Initialize the I2C expander, panel, and backlight. */
  bool begin();

  /** Return whether panel initialization succeeded. */
  bool isReady() const;

  /** Set the panel backlight duty cycle from 0 through 255. */
  void setBacklight(uint8_t level);

  /** Copy an RGB565 rectangle into the panel. */
  void drawPixels(int16_t x, int16_t y, const uint16_t* pixels, int16_t width,
                  int16_t height);

 private:
  void resetPanel();

  TCA9554 ioExpander_;
  Arduino_ESP32SPI displayBus_;
  Arduino_ST7796 display_;
  bool ready_ = false;
};

}  // namespace nova
