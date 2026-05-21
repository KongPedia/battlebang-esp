#pragma once

#include <Arduino.h>
#include <FastLED.h>

#include "../config.h"

namespace go2 {

class LedRing {
 public:
  void begin();
  void tick(uint32_t now, int hp, bool dead);
  void markDirty();
  void clearDamageBlink();
  void applyDamageBlink(int oldHp, int newHp);
  void setRemoteDisplay(float fillRatio, const String& mode, bool down, uint32_t ttlMs, uint32_t now);
  void clearRemoteDisplay();
  bool remoteDisplayActive() const;

 private:
  CRGB leds_[NUM_LEDS] = {};
  bool blinkMask_[NUM_LEDS] = {false};
  bool blinkOn_ = false;
  bool deadOn_ = false;
  bool dirty_ = true;
  uint32_t lastBlinkMs_ = 0;
  uint32_t lastDeadBlinkMs_ = 0;
  uint32_t lastShowMs_ = 0;
  bool remoteActive_ = false;
  bool remoteDown_ = false;
  float remoteFillRatio_ = 1.0f;
  String remoteMode_ = "idle";
  uint32_t remoteExpiresMs_ = 0;

  static int hpToBand(int hpVal);
  static int hpToLapHp(int hpVal);
  static int lapHpToLit(int lapHp);
  static CRGB bandColor(int band);
  static CRGB nextBandColor(int band);
  bool remoteExpired(uint32_t now) const;
  void handleRemoteExpiry(uint32_t now);
  void renderRemote(uint32_t now);
  void renderLocal(uint32_t now, int hp, bool dead);
  void showTick(uint32_t now);
};

}  // namespace go2
