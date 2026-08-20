#pragma once

#include <Arduino_GFX_Library.h>
#include <TCA9554.h>

#include <cstdint>

namespace nova {

/** Narrow adapter for the Waveshare ST7796 panel and its reset expander. */
class BoardDisplay {
 public:
  BoardDisplay();

  bool begin();
  bool isReady() const;
  uint16_t width() const;
  uint16_t height() const;
  void setBacklight(uint8_t level);
  void clear(uint16_t rgb565);

  /** Flush one inclusive RGB565 rectangle, suitable for an LVGL callback. */
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
