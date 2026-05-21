#include "go2/display/ring_display.h"

namespace go2 {

void RingDisplay::begin() {
  FastLED.addLeds<WS2811, LED_PIN, RGB>(leds_, NUM_LEDS);
  FastLED.setBrightness(LED_BRIGHTNESS);
  FastLED.setMaxPowerInVoltsAndMilliamps(LED_MAX_VOLTS, LED_MAX_MA);
  dirty_ = true;
}

void RingDisplay::tick(uint32_t now, int hp, bool dead) {
  if (now - lastBlinkMs_ >= LED_BLINK_MS) {
    lastBlinkMs_ = now;
    blinkOn_ = !blinkOn_;
    dirty_ = true;
  }

  handleRemoteExpiry(now);

  if (remoteActive_) {
    renderRemote(now);
  } else {
    renderLocal(now, hp, dead);
  }
  showTick(now);
}

void RingDisplay::markDirty() {
  dirty_ = true;
}

void RingDisplay::clearDamageBlink() {
  for (int i = 0; i < NUM_LEDS; i++) blinkMask_[i] = false;
  dirty_ = true;
}

void RingDisplay::applyDamageBlink(int oldHp, int newHp) {
  clearDamageBlink();
  if (newHp <= 0) return;

  int oldLit = constrain((long)oldHp * NUM_LEDS / HP_MAX, 0L, (long)NUM_LEDS);
  int newLit = constrain((long)newHp * NUM_LEDS / HP_MAX, 0L, (long)NUM_LEDS);
  if (newLit < oldLit) {
    for (int i = newLit; i < oldLit; i++) {
      if (i >= 0 && i < NUM_LEDS) blinkMask_[i] = true;
    }
  }
  dirty_ = true;
}

void RingDisplay::setRemoteDisplay(float fillRatio, const String& mode, bool down, uint32_t ttlMs, uint32_t now) {
  remoteActive_ = true;
  remoteDown_ = down;
  remoteFillRatio_ = constrain(fillRatio, 0.0f, 1.0f);
  remoteMode_ = mode.length() > 0 ? mode : String("idle");
  if (ttlMs < 1) ttlMs = 1;
  remoteExpiresMs_ = now + ttlMs;
  dirty_ = true;
}

void RingDisplay::clearRemoteDisplay() {
  remoteActive_ = false;
  remoteDown_ = false;
  remoteFillRatio_ = 1.0f;
  remoteMode_ = "idle";
  dirty_ = true;
}

bool RingDisplay::remoteDisplayActive() const {
  return remoteActive_;
}

int RingDisplay::hpToBand(int hpVal) {
  if (hpVal <= 0) return -1;
  return (hpVal - 1) / HP_PER_LAP;
}

int RingDisplay::hpToLapHp(int hpVal) {
  if (hpVal <= 0) return 0;
  int r = hpVal % HP_PER_LAP;
  return (r == 0) ? HP_PER_LAP : r;
}

int RingDisplay::lapHpToLit(int lapHp) {
  lapHp = constrain(lapHp, 0, HP_PER_LAP);
  return (long)lapHp * NUM_LEDS / HP_PER_LAP;
}

CRGB RingDisplay::bandColor(int band) {
  if (band >= 2) return CRGB::Green;
  if (band == 1) return CRGB::Yellow;
  if (band == 0) return CRGB::Red;
  return CRGB::Black;
}

CRGB RingDisplay::nextBandColor(int band) {
  if (band <= 0) return CRGB::Black;
  return bandColor(band - 1);
}

bool RingDisplay::remoteExpired(uint32_t now) const {
  return remoteActive_ && remoteExpiresMs_ != 0 && (int32_t)(now - remoteExpiresMs_) >= 0;
}

void RingDisplay::handleRemoteExpiry(uint32_t now) {
  if (!remoteExpired(now)) return;

  if (remoteDown_ || remoteMode_ == "down") {
    // Down is a Command Center display command, not ESP-owned HP state.
    // Keep it latched until Command Center sends a non-down display command
    // or local reset clears the remote display.
    remoteExpiresMs_ = 0;
    dirty_ = true;
    return;
  }

  if (remoteMode_ == "hit_flash") {
    remoteMode_ = "active";
    remoteExpiresMs_ = 0;
    dirty_ = true;
    return;
  }

  remoteActive_ = false;
  dirty_ = true;
}

void RingDisplay::renderRemote(uint32_t now) {
  if (remoteMode_ == "disabled") {
    for (int i = 0; i < NUM_LEDS; i++) leds_[i] = CRGB::Black;
    return;
  }

  if (remoteDown_ || remoteMode_ == "down") {
    if (now - lastDeadBlinkMs_ >= LED_DEAD_BLINK_MS) {
      lastDeadBlinkMs_ = now;
      deadOn_ = !deadOn_;
      dirty_ = true;
    }
    for (int i = 0; i < NUM_LEDS; i++) leds_[i] = deadOn_ ? CRGB::Red : CRGB::Black;
    return;
  }

  int lit = constrain((int)(remoteFillRatio_ * NUM_LEDS + 0.5f), 0, NUM_LEDS);
  CRGB fillColor = CRGB::Green;
  if (remoteMode_ == "hit_flash") {
    fillColor = blinkOn_ ? CRGB::White : CRGB::Red;
  } else if (remoteMode_ == "active") {
    fillColor = CRGB::Green;
  } else if (remoteMode_ == "stale") {
    fillColor = CRGB::Orange;
  } else if (remoteMode_ == "idle") {
    fillColor = CRGB(0, 40, 0);
  }

  for (int i = 0; i < NUM_LEDS; i++) leds_[i] = (i < lit) ? fillColor : CRGB::Black;
}

void RingDisplay::renderLocal(uint32_t now, int hp, bool dead) {
  if (dead) {
    if (now - lastDeadBlinkMs_ >= LED_DEAD_BLINK_MS) {
      lastDeadBlinkMs_ = now;
      deadOn_ = !deadOn_;
      dirty_ = true;
    }
    for (int i = 0; i < NUM_LEDS; i++) leds_[i] = deadOn_ ? CRGB::Red : CRGB::Black;
    return;
  }

  float ratio = constrain((float)hp / (float)HP_MAX, 0.0f, 1.0f);
  int lit = constrain((int)(ratio * NUM_LEDS + 0.5f), 0, NUM_LEDS);
  CRGB base = CRGB::Red;
  if (ratio > 0.66f) {
    base = CRGB::Green;
  } else if (ratio > 0.33f) {
    base = CRGB::Yellow;
  }

  for (int i = 0; i < NUM_LEDS; i++) {
    if (i < lit) leds_[i] = base;
    else if (blinkMask_[i]) leds_[i] = blinkOn_ ? CRGB::White : CRGB::Black;
    else leds_[i] = CRGB::Black;
  }
}

void RingDisplay::showTick(uint32_t now) {
  if (!dirty_) return;
  if (now - lastShowMs_ < LED_SHOW_PERIOD_MS) return;
  lastShowMs_ = now;
  FastLED.show();
  dirty_ = false;
}

}  // namespace go2
