#pragma once

#include <Arduino.h>

#include "../config.h"

namespace go2 {

using SystemTickFn = void (*)();
using HitCallback = void (*)(int targetId, uint16_t peak);

class HitSensor {
 public:
  void begin();
  void resetFlags();
  void poll(uint32_t now, SystemTickFn systemTick, HitCallback onHit);
};

}  // namespace go2
