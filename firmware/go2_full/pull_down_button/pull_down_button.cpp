#include "go2_full/pull_down_button/pull_down_button.h"

#include <Arduino.h>

namespace go2 {

void PullDownButton::begin(int pin, uint32_t now) {
  pin_ = pin;
  if (pin_ >= 0) pinMode(pin_, INPUT_PULLDOWN);
  core_.begin(pin_ >= 0 && digitalRead(pin_) == HIGH, now);
}

bool PullDownButton::poll(uint32_t now) {
  return pin_ >= 0 && core_.update(digitalRead(pin_) == HIGH, now);
}

}  // namespace go2
