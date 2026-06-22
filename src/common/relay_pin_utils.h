#pragma once

#include <Arduino.h>

namespace battlebang {

inline void configureRelayPinOffWithLevel(int pin, int offLevel) {
  if (pin < 0) return;
  pinMode(pin, offLevel == HIGH ? INPUT_PULLUP : INPUT_PULLDOWN);
  digitalWrite(pin, offLevel);
  pinMode(pin, OUTPUT);
  digitalWrite(pin, offLevel);
}

}  // namespace battlebang
