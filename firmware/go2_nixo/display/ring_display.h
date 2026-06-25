#pragma once

#include <Arduino.h>
#include <FastLED.h>

#include "../build_config.h"

namespace go2 {

class RingDisplay {
 public:
  void begin(uint16_t brightness = RING_LED_BRIGHTNESS);
  void tick(uint32_t now);
  void markDirty();
  void setBrightness(uint16_t brightness);
  void startFire(uint32_t fireDurationMs, uint32_t cooldownMs, uint32_t now);
  void startCooldown(uint32_t durationMs, uint32_t now);
  void clearCooldown();
  void setCooldownState(bool firing,
                        uint32_t remainingMs,
                        uint32_t durationMs,
                        bool inhibited);
  bool cooldownActive(uint32_t now) const;

 private:
  CRGB leds_[RING_NUM_LEDS] = {};
  bool dirty_ = true;
  bool externalState_ = false;
  bool firing_ = false;
  bool inhibited_ = false;
  uint16_t brightness_ = RING_LED_BRIGHTNESS;
  uint32_t firingStartedMs_ = 0;
  uint32_t fireDurationMs_ = NIXO_FIRE_DEFAULT_DURATION_MS;
  uint32_t pendingCooldownDurationMs_ = NIXO_FIRE_COOLDOWN_MS;
  uint32_t cooldownStartedMs_ = 0;
  uint32_t cooldownDurationMs_ = 0;
  uint32_t cooldownRemainingMs_ = 0;
  uint32_t lastShowMs_ = 0;

  void updateInternalFire(uint32_t now);
  uint32_t remainingMs(uint32_t now) const;
  void render(uint32_t now);
  void renderReady();
  void renderFiring();
  void renderCooldown(uint32_t now);
  CRGB scaled(uint8_t r, uint8_t g, uint8_t b) const;
  void showTick(uint32_t now);
};

}  // namespace go2
