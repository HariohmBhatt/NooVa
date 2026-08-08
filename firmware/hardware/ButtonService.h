#pragma once

#include <Arduino.h>
#include <TCA9554.h>

#include <cstdint>

#include "../core/Logger.h"

namespace nova {

enum class ButtonEvent : uint8_t {
  None,
  BootPressed,
  PwrPressed,
};

class ButtonService {
 public:
  /** Construct the board-button monitor using the shared diagnostic logger. */
  explicit ButtonService(Logger& logger);

  /** Initialize BOOT GPIO0 and the PWR TCA9554 input without changing LCD pins. */
  bool begin();

  /** Poll and debounce both board buttons, returning an action when pressed. */
  ButtonEvent update();

  /** Return whether the PWR input on the shared expander was initialized. */
  bool isPwrReady() const;

 private:
  struct DebouncedInput {
    bool rawPressed = false;
    bool stablePressed = false;
    uint32_t rawChangedAt = 0;
  };

  static constexpr uint32_t kPollPeriodMs = 10;
  static constexpr uint32_t kDebouncePeriodMs = 30;

  static void initializeInput(DebouncedInput& input, bool pressed,
                              uint32_t now);
  static bool updateInput(DebouncedInput& input, bool pressed, uint32_t now);
  bool readPwrPressed();

  Logger& logger_;
  TCA9554 ioExpander_;
  DebouncedInput bootInput_;
  DebouncedInput pwrInput_;
  uint32_t lastPollAt_ = 0;
  bool pwrReady_ = false;
};

}  // namespace nova
