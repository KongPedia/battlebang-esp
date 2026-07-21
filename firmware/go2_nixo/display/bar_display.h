#pragma once

#include <Arduino.h>
#include <FastLED.h>

#include "../build_config.h"

namespace go2 {

class BarDisplay {
 public:
  void begin(uint16_t brightness = HP_BAR_LED_BRIGHTNESS);
  void tick(uint32_t now);
  bool startupReady(uint32_t now) const;
  void markDirty();
  void setBrightness(uint16_t brightness);
  void setLocalHpState(uint16_t hpRemaining, uint16_t maxHits, bool down, uint32_t hitFlashMs, uint32_t now);
  void resetLocalHpState(uint16_t maxHits);
  void setRemoteDisplay(float fillRatio, const String& mode, bool down, uint32_t ttlMs, uint32_t now);
  void clearRemoteDisplay();
  bool remoteDisplayActive() const;

 private:
  CRGB leds_[HP_BAR_NUM_LEDS] = {};
  bool blinkOn_ = false;
  bool downBlinkOn_ = false;
  bool dirty_ = true;
  uint32_t lastBlinkMs_ = 0;
  uint32_t lastDownBlinkMs_ = 0;
  uint32_t lastShowMs_ = 0;
  uint32_t startupStartedMs_ = 0;
  bool startupLoading_ = true;
  uint16_t localHpRemaining_ = 1;
  uint16_t localMaxHits_ = 1;
  bool localDown_ = false;
  String localMode_ = "active";
  uint32_t localFlashExpiresMs_ = 0;
  bool remoteActive_ = false;
  bool remoteDown_ = false;
  float remoteFillRatio_ = 1.0f;
  String remoteMode_ = "idle";
  uint32_t remoteExpiresMs_ = 0;

  float localFillRatio() const;
  bool remoteExpired(uint32_t now) const;
  void handleLocalFlashExpiry(uint32_t now);
  void handleRemoteExpiry(uint32_t now);
  void renderStartupLoading(uint32_t now);
  void renderLocal(uint32_t now);
  void renderRemote(uint32_t now);
  void renderBlank();
  void renderHpBar(float fillRatio, const CRGB& healthyColor, const CRGB& damagedColor);
  void setHpBarGroup(int group1Based, const CRGB& color);
  void setHpBarPixel(int group, int strip, const CRGB& color);
  void showTick(uint32_t now);
};

}  // namespace go2
