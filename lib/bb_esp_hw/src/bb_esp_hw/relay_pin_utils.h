#pragma once

#include <Arduino.h>

namespace battlebang::esp::hw {

inline void configureRelayPinOffWithLevel(int pin, int offLevel) {
  if (pin < 0) return;
  pinMode(pin, offLevel == HIGH ? INPUT_PULLUP : INPUT_PULLDOWN);
  digitalWrite(pin, offLevel);
  pinMode(pin, OUTPUT);
  digitalWrite(pin, offLevel);
}

}  // namespace battlebang::esp::hw

namespace battlebang {

// Compatibility alias for legacy firmware that still calls the pre-library API.
inline void configureRelayPinOffWithLevel(int pin, int offLevel) {
  ::battlebang::esp::hw::configureRelayPinOffWithLevel(pin, offLevel);
}

}  // namespace battlebang
