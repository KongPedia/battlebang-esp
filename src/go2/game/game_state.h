#pragma once

#include <Arduino.h>

#include "../config.h"
#include "../led/led_ring.h"

namespace go2 {

class GameState {
 public:
  void begin(HardwareSerial& hpSerial);
  void reset(LedRing& ledRing);
  void applyDamage(int damage, LedRing& ledRing);
  void applyAuthorityDown(bool down, LedRing& ledRing);
  void sendHpToJetson();
  int hp() const;
  bool isDead() const;

 private:
  HardwareSerial* hpSerial_ = nullptr;
  int hp_ = HP_MAX;
  bool dead_ = false;
};

}  // namespace go2
