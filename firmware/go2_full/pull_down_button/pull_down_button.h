#pragma once

#include <stdint.h>

#include "go2_full/pull_down_button/pull_down_button_core.h"

namespace go2 {

class PullDownButton {
 public:
  void begin(int pin, uint32_t now);
  bool poll(uint32_t now);
  bool enabled() const { return core_.enabled(); }
  bool pressed() const { return core_.pressed(); }
  int pin() const { return pin_; }

 private:
  int pin_ = -1;
  PullDownButtonCore core_;
};

}  // namespace go2
