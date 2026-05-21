#pragma once

#include <Arduino.h>

#include "../build_config.h"
#include "../display/ring_display.h"

namespace go2 {

class OfflineHitFallback {
 public:
  void begin(HardwareSerial& hpSerial);
  void reset(RingDisplay& ringDisplay);
  void applyDamage(int damage, RingDisplay& ringDisplay);
  void sendHpToJetson();
  int hp() const;
  bool down() const;

 private:
  HardwareSerial* hpSerial_ = nullptr;
  int hp_ = HP_MAX;
  bool down_ = false;
};

}  // namespace go2
