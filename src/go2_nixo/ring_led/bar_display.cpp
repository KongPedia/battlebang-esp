#include "go2_nixo/ring_led/bar_display.h"

namespace go2 {

void BarDisplay::begin() {
  FastLED.addLeds<WS2815, HP_BAR_LED_PIN, RGB>(leds_, HP_BAR_NUM_LEDS);
  FastLED.setBrightness(HP_BAR_LED_BRIGHTNESS);
  FastLED.setMaxPowerInVoltsAndMilliamps(LED_MAX_VOLTS, LED_MAX_MA);
  dirty_ = true;
}

void BarDisplay::tick(uint32_t now) {
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

void BarDisplay::markDirty() {
  dirty_ = true;
}

void BarDisplay::setRemoteDisplay(float fillRatio, const String& mode, bool down, uint32_t ttlMs, uint32_t now) {
  remoteActive_ = true;
  remoteDown_ = down;
  remoteFillRatio_ = constrain(fillRatio, 0.0f, 1.0f);
  remoteMode_ = mode.length() > 0 ? mode : String("idle");
  if (ttlMs < 1) ttlMs = 1;
  if (ttlMs > 0x7ffffffful) ttlMs = 0x7ffffffful;
  remoteExpiresMs_ = now + ttlMs;
  dirty_ = true;
}

void BarDisplay::clearRemoteDisplay() {
  remoteActive_ = false;
  remoteDown_ = false;
  remoteFillRatio_ = 1.0f;
  remoteMode_ = "idle";
  remoteExpiresMs_ = 0;
  dirty_ = true;
}

bool BarDisplay::remoteDisplayActive() const {
  return remoteActive_;
}

bool BarDisplay::remoteExpired(uint32_t now) const {
  return remoteActive_ && remoteExpiresMs_ != 0 && (int32_t)(now - remoteExpiresMs_) >= 0;
}

void BarDisplay::handleRemoteExpiry(uint32_t now) {
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

void BarDisplay::renderRemote(uint32_t now) {
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
    for (int i = 0; i < HP_BAR_NUM_LEDS; i++) leds_[i] = downBlinkOn_ ? CRGB::Red : CRGB::Black;
    return;
  }

  CRGB healthyColor = CRGB::Green;
  CRGB damagedColor = CRGB::Red;
  if (remoteMode_ == "hit_flash") {
    healthyColor = blinkOn_ ? CRGB::White : CRGB::Green;
    damagedColor = blinkOn_ ? CRGB::White : CRGB::Red;
  } else if (remoteMode_ == "stale") {
    healthyColor = CRGB::Orange;
  }

  renderHpBar(remoteFillRatio_, healthyColor, damagedColor);
}

void BarDisplay::renderFullIdle() {
  renderHpBar(1.0f, CRGB::Green, CRGB::Red);
}

void BarDisplay::renderBlank() {
  for (int i = 0; i < HP_BAR_NUM_LEDS; i++) leds_[i] = CRGB::Black;
}

void BarDisplay::renderHpBar(float fillRatio, const CRGB& healthyColor, const CRGB& damagedColor) {
  int healthyGroups = constrain((int)(fillRatio * HP_BAR_GROUP_COUNT + 0.5f),
                                0,
                                HP_BAR_GROUP_COUNT);
  for (int group = 1; group <= HP_BAR_GROUP_COUNT; group++) {
    setHpBarGroup(group, group <= healthyGroups ? healthyColor : damagedColor);
  }
}

void BarDisplay::setHpBarGroup(int group1Based, const CRGB& color) {
  if (group1Based < 1 || group1Based > HP_BAR_GROUP_COUNT) return;

  // 84-LED bar layout from the Go2 HP harness reference sketch:
  // group 1  -> LEDs 1, 56, 57
  // group 2  -> LEDs 2, 55, 58
  // ...
  // group 28 -> LEDs 28, 29, 84
  int row1Index = group1Based - 1;
  int row2Index = 2 * HP_BAR_GROUP_COUNT - group1Based;
  int row3Index = 2 * HP_BAR_GROUP_COUNT - 1 + group1Based;

  leds_[row1Index] = color;
  leds_[row2Index] = color;
  leds_[row3Index] = color;
}

void BarDisplay::showTick(uint32_t now) {
  if (!dirty_) return;
  if (now - lastShowMs_ < LED_SHOW_PERIOD_MS) return;
  lastShowMs_ = now;
  FastLED.show();
  dirty_ = false;
}

}  // namespace go2
