#pragma once

#include <Arduino.h>

#include "../build_config.h"

namespace go2 {

using HitCallback = void (*)(int targetId, bool hit);

class PiezoSensor {
 public:
  void begin();
  void resetFlags();
  void poll(uint32_t now, HitCallback onHit);
};

}  // namespace go2
