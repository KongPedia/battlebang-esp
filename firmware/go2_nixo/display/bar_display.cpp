#include "go2_nixo/display/bar_display.h"

namespace go2 {

namespace {

constexpr uint32_t STARTUP_LOADING_MS = 3000;

int groupsForFill(float fillRatio) {
  return constrain((int)(fillRatio * HP_BAR_GROUP_COUNT + 0.5f), 0, HP_BAR_GROUP_COUNT);
}

}  // namespace

void BarDisplay::begin(uint16_t brightness) {
  startupStartedMs_ = millis();
  startupLoading_ = true;
  FastLED.addLeds<WS2815, HP_BAR_LED_PIN, RGB>(leds_, HP_BAR_NUM_LEDS);
  setBrightness(brightness);
  FastLED.setMaxPowerInVoltsAndMilliamps(LED_MAX_VOLTS, LED_MAX_MA);
  dirty_ = true;
}

void BarDisplay::tick(uint32_t now) {
  if (now - lastBlinkMs_ >= LED_BLINK_MS) {
    lastBlinkMs_ = now;
    blinkOn_ = !blinkOn_;
    dirty_ = true;
  }

  handleLocalFlashExpiry(now);
  handleRemoteExpiry(now);

  if (startupLoading_ && !startupReady(now)) {
    renderStartupLoading(now);
  } else {
    if (startupLoading_) {
      startupLoading_ = false;
      dirty_ = true;
    }
    if (remoteActive_) {
      renderRemote(now);
    } else {
      renderLocal(now);
    }
  }
  showTick(now);
}

bool BarDisplay::startupReady(uint32_t now) const {
  return now - startupStartedMs_ >= STARTUP_LOADING_MS;
}

void BarDisplay::markDirty() {
  dirty_ = true;
}

void BarDisplay::setBrightness(uint16_t brightness) {
  if (brightness > 255) brightness = 255;
  FastLED.setBrightness(static_cast<uint8_t>(brightness));
  dirty_ = true;
}

void BarDisplay::setLocalHpState(uint16_t hpRemaining, uint16_t maxHits, bool down, uint32_t hitFlashMs, uint32_t now) {
  if (maxHits < 1) maxHits = 1;
  if (hitFlashMs > 0x7ffffffful) hitFlashMs = 0x7ffffffful;
  localMaxHits_ = maxHits;
  localHpRemaining_ = hpRemaining > maxHits ? maxHits : hpRemaining;
  localDown_ = down || localHpRemaining_ == 0;
  localMode_ = hitFlashMs > 0 && !localDown_ ? String("hit_flash") : String("active");
  localFlashExpiresMs_ = hitFlashMs > 0 && !localDown_ ? now + hitFlashMs : 0;
  dirty_ = true;
}

void BarDisplay::resetLocalHpState(uint16_t maxHits) {
  if (maxHits < 1) maxHits = 1;
  localMaxHits_ = maxHits;
  localHpRemaining_ = maxHits;
  localDown_ = false;
  localMode_ = "active";
  localFlashExpiresMs_ = 0;
  clearRemoteDisplay();
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

float BarDisplay::localFillRatio() const {
  if (localMaxHits_ < 1) return 1.0f;
  return constrain(static_cast<float>(localHpRemaining_) / static_cast<float>(localMaxHits_), 0.0f, 1.0f);
}

bool BarDisplay::remoteExpired(uint32_t now) const {
  return remoteActive_ && remoteExpiresMs_ != 0 && (int32_t)(now - remoteExpiresMs_) >= 0;
}

void BarDisplay::handleLocalFlashExpiry(uint32_t now) {
  if (localFlashExpiresMs_ == 0) return;
  if ((int32_t)(now - localFlashExpiresMs_) < 0) return;
  localFlashExpiresMs_ = 0;
  localMode_ = "active";
  dirty_ = true;
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

void BarDisplay::renderStartupLoading(uint32_t now) {
  renderBlank();
  for (int group = 0; group < HP_BAR_GROUP_COUNT; group++) {
    setHpBarPixel(group, 0, CRGB::White);
    setHpBarPixel(group, HP_BAR_LEDS_PER_GROUP - 1, CRGB::White);
  }
  for (int strip = 0; strip < HP_BAR_LEDS_PER_GROUP; strip++) {
    setHpBarPixel(0, strip, CRGB::White);
    setHpBarPixel(HP_BAR_GROUP_COUNT - 1, strip, CRGB::White);
  }

  const uint32_t elapsedMs = now - startupStartedMs_;
  const int interiorGroups = HP_BAR_GROUP_COUNT - 2;
  const int filledGroups = constrain(
      (int)((elapsedMs * interiorGroups + STARTUP_LOADING_MS - 1) / STARTUP_LOADING_MS), 0, interiorGroups);
  for (int group = 1; group <= filledGroups; group++) setHpBarPixel(group, 1, CRGB::Blue);
  dirty_ = true;
}

void BarDisplay::renderLocal(uint32_t now) {
  if (localDown_) {
    if (now - lastDownBlinkMs_ >= LED_DEAD_BLINK_MS) {
      lastDownBlinkMs_ = now;
      downBlinkOn_ = !downBlinkOn_;
      dirty_ = true;
    }
    for (int i = 0; i < HP_BAR_NUM_LEDS; i++) leds_[i] = downBlinkOn_ ? CRGB::Red : CRGB::Black;
    return;
  }

  const float fillRatio = localFillRatio();
  renderHpBar(fillRatio, fillRatio <= 0.30f ? CRGB::Yellow : CRGB::Green, CRGB::Black);
  if (localMode_ == "hit_flash") {
    const int healthyGroups = groupsForFill(fillRatio);
    const float previousFillRatio = constrain(
        static_cast<float>(localHpRemaining_ + 1) / static_cast<float>(localMaxHits_), 0.0f, 1.0f);
    const int previousHealthyGroups = groupsForFill(previousFillRatio);
    for (int group = healthyGroups + 1; group <= previousHealthyGroups; group++) {
      setHpBarGroup(group, CRGB::Red);
    }
  }
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

  CRGB healthyColor = remoteFillRatio_ <= 0.30f ? CRGB::Yellow : CRGB::Green;
  CRGB damagedColor = CRGB::Black;
  if (remoteMode_ == "hit_flash") {
    damagedColor = CRGB::Red;
  } else if (remoteMode_ == "stale") {
    healthyColor = CRGB::Orange;
  }

  renderHpBar(remoteFillRatio_, healthyColor, damagedColor);
}

void BarDisplay::renderBlank() {
  for (int i = 0; i < HP_BAR_NUM_LEDS; i++) leds_[i] = CRGB::Black;
}

void BarDisplay::renderHpBar(float fillRatio, const CRGB& healthyColor, const CRGB& damagedColor) {
  int healthyGroups = groupsForFill(fillRatio);
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

void BarDisplay::setHpBarPixel(int group, int strip, const CRGB& color) {
  if (group < 0 || group >= HP_BAR_GROUP_COUNT || strip < 0 || strip >= HP_BAR_LEDS_PER_GROUP) return;
  group = HP_BAR_GROUP_COUNT - 1 - group;
  strip = HP_BAR_LEDS_PER_GROUP - 1 - strip;
  if (strip == 0) {
    leds_[group] = color;
  } else if (strip == 1) {
    leds_[2 * HP_BAR_GROUP_COUNT - 1 - group] = color;
  } else {
    leds_[2 * HP_BAR_GROUP_COUNT + group] = color;
  }
}

void BarDisplay::showTick(uint32_t now) {
  if (!dirty_) return;
  if (now - lastShowMs_ < LED_SHOW_PERIOD_MS) return;
  lastShowMs_ = now;
  FastLED.show();
  dirty_ = false;
}

}  // namespace go2
