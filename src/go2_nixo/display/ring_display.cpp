#include "go2_nixo/display/ring_display.h"

namespace go2 {

void RingDisplay::begin() {
  FastLED.addLeds<WS2811, LED_PIN, RGB>(leds_, NUM_LEDS);
  FastLED.setBrightness(LED_BRIGHTNESS);
  FastLED.setMaxPowerInVoltsAndMilliamps(LED_MAX_VOLTS, LED_MAX_MA);
  dirty_ = true;
}

void RingDisplay::tick(uint32_t now) {
  if (now - lastBlinkMs_ >= LED_BLINK_MS) {
    lastBlinkMs_ = now;
    blinkOn_ = !blinkOn_;
    dirty_ = true;
  }

  handleRemoteExpiry(now);

  if (remoteActive_) {
    renderRemote(now);
  } else {
    renderFullIdle();
  }
  showTick(now);
}

void RingDisplay::markDirty() {
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
  remoteExpiresMs_ = 0;
  dirty_ = true;
}

bool RingDisplay::remoteDisplayActive() const {
  return remoteActive_;
}

bool RingDisplay::remoteExpired(uint32_t now) const {
  return remoteActive_ && remoteExpiresMs_ != 0 && (int32_t)(now - remoteExpiresMs_) >= 0;
}

void RingDisplay::handleRemoteExpiry(uint32_t now) {
  if (!remoteExpired(now)) return;

  if (remoteDown_ || remoteMode_ == "down") {
    // Down is a Command Center display command. Keep it latched until the
    // server sends a non-down command or a local reset clears the display.
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

  clearRemoteDisplay();
}

void RingDisplay::renderRemote(uint32_t now) {
  if (remoteMode_ == "disabled") {
    renderBlank();
    return;
  }

  if (remoteDown_ || remoteMode_ == "down") {
    if (now - lastDownBlinkMs_ >= LED_DEAD_BLINK_MS) {
      lastDownBlinkMs_ = now;
      downBlinkOn_ = !downBlinkOn_;
      dirty_ = true;
    }
    for (int i = 0; i < NUM_LEDS; i++) leds_[i] = downBlinkOn_ ? CRGB::Red : CRGB::Black;
    return;
  }

  int lit = constrain((int)(remoteFillRatio_ * NUM_LEDS + 0.5f), 0, NUM_LEDS);
  CRGB fillColor = CRGB::Green;
  if (remoteMode_ == "hit_flash") {
    fillColor = blinkOn_ ? CRGB::White : CRGB::Red;
  } else if (remoteMode_ == "stale") {
    fillColor = CRGB::Orange;
  }

  for (int i = 0; i < NUM_LEDS; i++) leds_[i] = (i < lit) ? fillColor : CRGB::Black;
}

void RingDisplay::renderFullIdle() {
  for (int i = 0; i < NUM_LEDS; i++) leds_[i] = CRGB::Green;
}

void RingDisplay::renderBlank() {
  for (int i = 0; i < NUM_LEDS; i++) leds_[i] = CRGB::Black;
}

void RingDisplay::showTick(uint32_t now) {
  if (!dirty_) return;
  if (now - lastShowMs_ < LED_SHOW_PERIOD_MS) return;
  lastShowMs_ = now;
  FastLED.show();
  dirty_ = false;
}

}  // namespace go2
