#pragma once

#include <cstdint>

namespace nova::board {

// Verified against the Waveshare schematic/example and the archived firmware's
// physical HW-001/HW-002 reports. Keep every board-specific electrical choice
// in this file so higher layers never depend on raw GPIO numbers.
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
constexpr uint8_t kTouchAddress = 0x38;

}  // namespace nova::board
