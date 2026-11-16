#ifdef ARDUINO

#include "status_led.h"

#include <Arduino.h>

namespace olvar {

StatusLed::StatusLed(uint8_t pin, bool activeHigh) : pin_(pin), activeHigh_(activeHigh) {}

void StatusLed::begin() {
  pinMode(pin_, OUTPUT);
  write(false);
  pattern_.restart(millis());
}

void StatusLed::setMode(LedMode mode) { pattern_.setMode(mode, millis()); }

void StatusLed::flashPublished() { pattern_.flash(millis()); }

void StatusLed::update() {
  bool on = pattern_.isOn(millis());
  if (on != on_) write(on);
}

void StatusLed::write(bool on) {
  on_ = on;
  digitalWrite(pin_, on == activeHigh_ ? HIGH : LOW);
}

}  // namespace olvar

#endif  // ARDUINO
