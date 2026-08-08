#pragma once

#include <cstdint>

namespace nova::board {

// Values match the official Waveshare Arduino display and touch examples.
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
constexpr uint8_t kBootButtonPin = 0;
constexpr bool kBootButtonPressedLevel = false;
constexpr uint8_t kPwrButtonExpanderPin = 6;
constexpr bool kPwrButtonPressedLevel = true;

}  // namespace nova::board
