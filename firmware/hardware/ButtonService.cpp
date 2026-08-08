#include "ButtonService.h"

#include "BoardPins.h"

namespace nova {

ButtonService::ButtonService(Logger& logger)
    : logger_(logger), ioExpander_(board::kIoExpanderAddress) {}

bool ButtonService::begin() {
  const uint32_t now = millis();
  pinMode(board::kBootButtonPin, INPUT_PULLUP);
  initializeInput(bootInput_,
                  digitalRead(board::kBootButtonPin) ==
                      (board::kBootButtonPressedLevel ? HIGH : LOW),
                  now);

  pwrReady_ = ioExpander_.isConnected() &&
              ioExpander_.pinMode1(board::kPwrButtonExpanderPin, INPUT);
  if (pwrReady_) {
    initializeInput(pwrInput_, readPwrPressed(), now);
    logger_.write(LogLevel::Info, "BOOT GPIO0 and PWR EXIO6 initialized");
  } else {
    logger_.write(LogLevel::Warning, "PWR button input unavailable");
  }
  return true;
}

ButtonEvent ButtonService::update() {
  const uint32_t now = millis();
  if (now - lastPollAt_ < kPollPeriodMs) {
    return ButtonEvent::None;
  }
  lastPollAt_ = now;

  const bool bootPressed =
      digitalRead(board::kBootButtonPin) ==
      (board::kBootButtonPressedLevel ? HIGH : LOW);
  const bool bootChanged = updateInput(bootInput_, bootPressed, now);
  if (bootChanged && !bootInput_.stablePressed) {
    logger_.write(LogLevel::Info,
                  "BOOT button released; pausing remote services");
    return ButtonEvent::BootReleased;
  }

  if (pwrReady_ && updateInput(pwrInput_, readPwrPressed(), now)) {
    logger_.write(pwrInput_.stablePressed ? LogLevel::Info : LogLevel::Debug,
                  pwrInput_.stablePressed ? "PWR button pressed"
                                          : "PWR button released");
  }
  return ButtonEvent::None;
}

bool ButtonService::isPwrReady() const { return pwrReady_; }

void ButtonService::initializeInput(DebouncedInput& input, bool pressed,
                                    uint32_t now) {
  input.rawPressed = pressed;
  input.stablePressed = pressed;
  input.rawChangedAt = now;
}

bool ButtonService::updateInput(DebouncedInput& input, bool pressed,
                                uint32_t now) {
  if (pressed != input.rawPressed) {
    input.rawPressed = pressed;
    input.rawChangedAt = now;
  }
  if (input.rawPressed == input.stablePressed ||
      now - input.rawChangedAt < kDebouncePeriodMs) {
    return false;
  }
  input.stablePressed = input.rawPressed;
  return true;
}

bool ButtonService::readPwrPressed() {
  return ioExpander_.read1(board::kPwrButtonExpanderPin) ==
         (board::kPwrButtonPressedLevel ? HIGH : LOW);
}

}  // namespace nova
