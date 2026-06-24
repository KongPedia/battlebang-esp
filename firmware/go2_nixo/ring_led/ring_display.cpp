#include "go2_nixo/ring_led/ring_display.h"

namespace go2 {

namespace {

constexpr uint8_t RING_COOLDOWN_FILL_STEPS = 10;

}

void RingDisplay::begin(uint16_t brightness) {
  FastLED.addLeds<WS2811, RING_LED_PIN, RGB>(leds_, RING_NUM_LEDS);
  setBrightness(brightness);
  dirty_ = true;
}

void RingDisplay::tick(uint32_t now) {
  updateInternalFire(now);
  render(now);
  showTick(now);
}

void RingDisplay::markDirty() {
  dirty_ = true;
}

void RingDisplay::setBrightness(uint16_t brightness) {
  if (brightness > 255) brightness = 255;
  brightness_ = brightness;
  dirty_ = true;
}

void RingDisplay::startFire(uint32_t fireDurationMs, uint32_t cooldownMs, uint32_t now) {
  if (fireDurationMs < 1) fireDurationMs = 1;
  if (cooldownMs < 1) cooldownMs = 1;
  if (cooldownActive(now)) return;

  externalState_ = false;
  firing_ = true;
  inhibited_ = false;
  firingStartedMs_ = now;
  fireDurationMs_ = fireDurationMs;
  pendingCooldownDurationMs_ = cooldownMs;
  cooldownStartedMs_ = 0;
  cooldownDurationMs_ = 0;
  cooldownRemainingMs_ = 0;
  dirty_ = true;
}

void RingDisplay::startCooldown(uint32_t durationMs, uint32_t now) {
  if (durationMs < 1) durationMs = 1;
  if (cooldownActive(now)) return;

  externalState_ = false;
  firing_ = false;
  inhibited_ = false;
  firingStartedMs_ = 0;
  fireDurationMs_ = 0;
  pendingCooldownDurationMs_ = durationMs;
  cooldownStartedMs_ = now;
  cooldownDurationMs_ = durationMs;
  cooldownRemainingMs_ = durationMs;
  dirty_ = true;
}

void RingDisplay::clearCooldown() {
  externalState_ = false;
  firing_ = false;
  inhibited_ = false;
  firingStartedMs_ = 0;
  fireDurationMs_ = 0;
  cooldownStartedMs_ = 0;
  cooldownDurationMs_ = 0;
  cooldownRemainingMs_ = 0;
  dirty_ = true;
}

void RingDisplay::setCooldownState(bool firing,
                                   uint32_t remainingMs,
                                   uint32_t durationMs,
                                   bool inhibited) {
  if (durationMs < 1) durationMs = 1;
  uint32_t boundedRemainingMs = min(remainingMs, durationMs);
  bool changed = !externalState_ ||
                 firing_ != firing ||
                 inhibited_ != inhibited ||
                 cooldownRemainingMs_ != boundedRemainingMs ||
                 cooldownDurationMs_ != durationMs;
  externalState_ = true;
  firing_ = firing;
  inhibited_ = inhibited;
  firingStartedMs_ = 0;
  fireDurationMs_ = 0;
  cooldownRemainingMs_ = boundedRemainingMs;
  cooldownDurationMs_ = durationMs;
  if (changed) dirty_ = true;
}

bool RingDisplay::cooldownActive(uint32_t now) const {
  return firing_ || inhibited_ || remainingMs(now) > 0;
}

void RingDisplay::updateInternalFire(uint32_t now) {
  if (externalState_) return;
  if (!firing_) return;
  if (now - firingStartedMs_ < fireDurationMs_) return;

  firing_ = false;
  firingStartedMs_ = 0;
  cooldownStartedMs_ = now;
  cooldownDurationMs_ = pendingCooldownDurationMs_;
  cooldownRemainingMs_ = cooldownDurationMs_;
  dirty_ = true;
}

uint32_t RingDisplay::remainingMs(uint32_t now) const {
  if (externalState_) return cooldownRemainingMs_;
  if (cooldownDurationMs_ == 0) return 0;
  uint32_t elapsed = now - cooldownStartedMs_;
  if (elapsed >= cooldownDurationMs_) return 0;
  return cooldownDurationMs_ - elapsed;
}

void RingDisplay::render(uint32_t now) {
  if (firing_) {
    renderFiring();
    return;
  }

  uint32_t remaining = remainingMs(now);
  if (remaining > 0) {
    renderCooldown(now);
    return;
  }
  if (inhibited_) {
    renderFiring();
    return;
  }
  if (!externalState_ && cooldownDurationMs_ != 0) {
    cooldownStartedMs_ = 0;
    cooldownDurationMs_ = 0;
    cooldownRemainingMs_ = 0;
    dirty_ = true;
  }
  renderReady();
}

void RingDisplay::renderReady() {
  CRGB color = scaled(0, 64, 0);
  for (int i = 0; i < RING_NUM_LEDS; i++) leds_[i] = color;
}

void RingDisplay::renderFiring() {
  CRGB color = scaled(96, 0, 0);
  for (int i = 0; i < RING_NUM_LEDS; i++) leds_[i] = color;
}

void RingDisplay::renderCooldown(uint32_t now) {
  uint32_t remaining = remainingMs(now);
  uint32_t elapsed = cooldownDurationMs_ > remaining ? cooldownDurationMs_ - remaining : 0;
  uint32_t completedSteps = (elapsed * RING_COOLDOWN_FILL_STEPS) / cooldownDurationMs_;
  if (completedSteps > RING_COOLDOWN_FILL_STEPS) completedSteps = RING_COOLDOWN_FILL_STEPS;
  int lit = constrain((int)((completedSteps * RING_NUM_LEDS) / RING_COOLDOWN_FILL_STEPS),
                      0,
                      RING_NUM_LEDS);
  CRGB cooldownColor = scaled(0, 64, 0);
  bool mismatch = false;
  for (int i = 0; i < RING_NUM_LEDS; i++) {
    CRGB expected = (i < lit) ? cooldownColor : CRGB::Black;
    if (leds_[i] != expected) {
      mismatch = true;
      break;
    }
  }
  if (mismatch) {
    for (int i = 0; i < RING_NUM_LEDS; i++) {
      leds_[i] = (i < lit) ? cooldownColor : CRGB::Black;
    }
    dirty_ = true;
  }
}

CRGB RingDisplay::scaled(uint8_t r, uint8_t g, uint8_t b) const {
  uint16_t scale = brightness_;
  return CRGB((uint8_t)((uint16_t)r * scale / 255),
              (uint8_t)((uint16_t)g * scale / 255),
              (uint8_t)((uint16_t)b * scale / 255));
}

void RingDisplay::showTick(uint32_t now) {
  if (!dirty_) return;
  if (now - lastShowMs_ < LED_SHOW_PERIOD_MS) return;
  lastShowMs_ = now;
  FastLED.show();
  dirty_ = false;
}

}  // namespace go2
