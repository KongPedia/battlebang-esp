#include "heavy_blaster_controller.h"

namespace battlebang {
namespace heavy_blaster {

namespace {
#ifndef PI
constexpr float PI = 3.14159265358979323846f;
#endif
}  // namespace

void HeavyBlasterController::begin(const RuntimeConfig& config, EventCallback callback, void* ctx) {
  callback_ = callback;
  callbackCtx_ = ctx;
  config_ = config;

  pinMode(::heavy_blaster::RELAY_PIN, OUTPUT);
  relayWrite(false);

  FastLED.addLeds<WS2812B, ::heavy_blaster::MATRIX1_PIN, GRB>(matrix1_, ::heavy_blaster::MAX_MATRIX_NUM_LEDS);
  FastLED.addLeds<WS2812B, ::heavy_blaster::MATRIX2_PIN, GRB>(matrix2_, ::heavy_blaster::MAX_MATRIX_NUM_LEDS);
  FastLED.addLeds<WS2812B, ::heavy_blaster::MATRIX3_PIN, GRB>(matrix3_, ::heavy_blaster::MAX_MATRIX_NUM_LEDS);
  FastLED.addLeds<WS2812B, ::heavy_blaster::MATRIX4_PIN, GRB>(matrix4_, ::heavy_blaster::MAX_MATRIX_NUM_LEDS);
  FastLED.setBrightness(config_.led.brightness);
  FastLED.setMaxPowerInVoltsAndMilliamps(::heavy_blaster::LED_MAX_VOLTS, config_.led.maxMa);

  reset("boot");
}

void HeavyBlasterController::applyConfig(const RuntimeConfig& config, bool resetState, const char* source) {
  const bool relayPolarityChanged = config_.hardware.relayActiveLow != config.hardware.relayActiveLow;
  if (relayPolarityChanged) relayWrite(false);
  config_ = config;
  FastLED.setBrightness(config_.led.brightness);
  FastLED.setMaxPowerInVoltsAndMilliamps(::heavy_blaster::LED_MAX_VOLTS, config_.led.maxMa);
  if (resetState || relayPolarityChanged || !config_.configured) {
    reset(source);
    return;
  }
  evaluateUnlockState(millis(), source);
  showNormalState();
}

void HeavyBlasterController::loop(uint32_t now) {
  evaluateUnlockState(now, "loop");
  render(now);
}

void HeavyBlasterController::reset(const char* source) {
  for (uint8_t i = 0; i < ::heavy_blaster::kSlotCount; ++i) slots_[i] = false;
  preUnlockActive_ = false;
  unlockedEffectActive_ = false;
  otaPrepared_ = false;
  rainbowOffset_ = 0;
  effectFrame_ = ::heavy_blaster::ARROW_FRAMES;
  preUnlockStartMs_ = 0;
  lastPreEffectUpdateMs_ = 0;
  lastEffectUpdateMs_ = 0;
  lastChangedSlot_ = -1;
  forceSafeOff(source);
  showNormalState();
  lastEvent_ = "reset";
  emit("reset", -1, source);
}

void HeavyBlasterController::forceSafeOff(const char* source) {
  (void) source;
  relayWrite(false);
  relayOn_ = false;
  preUnlockActive_ = false;
  unlockedEffectActive_ = false;
}

void HeavyBlasterController::prepareForOta() {
  otaPrepared_ = true;
  forceSafeOff("ota");
  for (uint8_t i = 0; i < ::heavy_blaster::kSlotCount; ++i) setMatrix(i, CRGB::Black);
  FastLED.show();
  lastEvent_ = "ota_prepared";
  emit("ota_prepared", -1, "ota");
}

void HeavyBlasterController::recoverFromFailedOta(const char* source) {
  otaPrepared_ = false;
  evaluateUnlockState(millis(), source);
  showNormalState();
  lastEvent_ = "ota_failed";
  emit("ota_failed", -1, source);
}

bool HeavyBlasterController::isSafeForOta() const {
  return !relayOn_ || otaPrepared_ || !config_.configured;
}

bool HeavyBlasterController::setSlot(uint8_t index, bool active, const char* source) {
  if (!config_.configured || otaPrepared_ || index >= slotCount()) return false;
  if (slots_[index] == active) {
    evaluateUnlockState(millis(), source);
    return true;
  }
  slots_[index] = active;
  lastChangedSlot_ = index;
  lastEvent_ = active ? "slot_active" : "slot_inactive";
  evaluateUnlockState(millis(), source);
  showNormalState();
  emit(lastEvent_.c_str(), index, source);
  return true;
}

bool HeavyBlasterController::setSlots(const bool slots[::heavy_blaster::kSlotCount], uint8_t count, const char* source) {
  if (!config_.configured || otaPrepared_ || count == 0 || count > slotCount()) return false;
  bool changed = false;
  for (uint8_t i = 0; i < slotCount(); ++i) {
    const bool next = i < count ? slots[i] : false;
    if (slots_[i] != next) {
      slots_[i] = next;
      lastChangedSlot_ = i;
      changed = true;
    }
  }
  lastEvent_ = changed ? "slots_updated" : "slots_unchanged";
  evaluateUnlockState(millis(), source);
  showNormalState();
  emit(lastEvent_.c_str(), lastChangedSlot_, source);
  return true;
}

bool HeavyBlasterController::unlock(const char* source) {
  bool slots[::heavy_blaster::kSlotCount] = {false, false, false, false};
  for (uint8_t i = 0; i < slotCount(); ++i) slots[i] = true;
  return setSlots(slots, slotCount(), source);
}

uint8_t HeavyBlasterController::slotCount() const {
  return constrain(config_.led.slotCount, static_cast<uint8_t>(1), static_cast<uint8_t>(::heavy_blaster::kSlotCount));
}

uint8_t HeavyBlasterController::requiredSlots() const {
  return constrain(config_.unlock.requiredSlots, static_cast<uint8_t>(1), slotCount());
}

uint16_t HeavyBlasterController::matrixLedCount() const {
  return constrain(config_.led.matrixNumLeds,
                   static_cast<uint16_t>(1),
                   static_cast<uint16_t>(::heavy_blaster::MAX_MATRIX_NUM_LEDS));
}

CRGB HeavyBlasterController::colorFromRgb(uint32_t rgb) const {
  return CRGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

const char* HeavyBlasterController::modeString() const {
  switch (currentMode()) {
    case Mode::UNCONFIGURED: return "UNCONFIGURED";
    case Mode::LOCKED: return "LOCKED";
    case Mode::PARTIAL: return "PARTIAL";
    case Mode::UNLOCK_PRE_EFFECT: return "UNLOCK_PRE_EFFECT";
    case Mode::UNLOCKED: return "UNLOCKED";
    case Mode::OTA_PREPARED: return "OTA_PREPARED";
  }
  return "UNKNOWN";
}

const char* HeavyBlasterController::effectString() const {
  if (preUnlockActive_) return "pre_unlock";
  if (unlockedEffectActive_) return "rainbow_arrow";
  return "normal";
}

uint8_t HeavyBlasterController::activeSlotCount() const {
  uint8_t count = 0;
  for (uint8_t i = 0; i < slotCount(); ++i) {
    if (slots_[i]) count++;
  }
  return count;
}

bool HeavyBlasterController::allRequiredSlotsActive() const {
  return activeSlotCount() >= requiredSlots();
}

HeavyBlasterController::Mode HeavyBlasterController::currentMode() const {
  if (otaPrepared_) return Mode::OTA_PREPARED;
  if (!config_.configured) return Mode::UNCONFIGURED;
  if (allRequiredSlotsActive()) return preUnlockActive_ ? Mode::UNLOCK_PRE_EFFECT : Mode::UNLOCKED;
  return activeSlotCount() == 0 ? Mode::LOCKED : Mode::PARTIAL;
}

void HeavyBlasterController::emit(const char* name, int slotIndex, const char* source) {
  if (callback_ == nullptr) return;
  HeavyBlasterEvent event;
  event.name = name;
  event.source = source;
  event.slotIndex = slotIndex;
  callback_(event, callbackCtx_);
}

int HeavyBlasterController::relayOnLevel() const {
  return config_.hardware.relayActiveLow ? LOW : HIGH;
}

int HeavyBlasterController::relayOffLevel() const {
  return config_.hardware.relayActiveLow ? HIGH : LOW;
}

void HeavyBlasterController::relayWrite(bool on) {
  digitalWrite(::heavy_blaster::RELAY_PIN, on ? relayOnLevel() : relayOffLevel());
  relayOn_ = on;
}

void HeavyBlasterController::evaluateUnlockState(uint32_t now, const char* source) {
  const bool shouldUnlock = config_.configured && !otaPrepared_ && config_.unlock.relayOnAfterAllSlots && allRequiredSlotsActive();
  if (shouldUnlock && !relayOn_) {
    relayWrite(true);
    preUnlockActive_ = true;
    unlockedEffectActive_ = false;
    preUnlockStartMs_ = now;
    lastPreEffectUpdateMs_ = 0;
    lastEffectUpdateMs_ = 0;
    effectFrame_ = ::heavy_blaster::ARROW_FRAMES;
    rainbowOffset_ = 0;
    lastEvent_ = "unlocked";
    emit("unlocked", -1, source);
  }
  if (!shouldUnlock && relayOn_) {
    relayWrite(false);
    preUnlockActive_ = false;
    unlockedEffectActive_ = false;
    lastEvent_ = "locked";
    emit("locked", -1, source);
  }
  if (!shouldUnlock) {
    preUnlockActive_ = false;
    unlockedEffectActive_ = false;
  }
}

void HeavyBlasterController::setMatrix(uint8_t index, const CRGB& color) {
  if (index >= ::heavy_blaster::kSlotCount) return;
  fill_solid(matrices_[index], matrixLedCount(), color);
  for (uint16_t i = matrixLedCount(); i < ::heavy_blaster::MAX_MATRIX_NUM_LEDS; ++i) matrices_[index][i] = CRGB::Black;
}

void HeavyBlasterController::showNormalState() {
  if (preUnlockActive_ || unlockedEffectActive_) return;
  for (uint8_t i = 0; i < ::heavy_blaster::kSlotCount; ++i) {
    if (i < slotCount() && slots_[i] && config_.configured && !otaPrepared_) {
      setMatrix(i, colorFromRgb(config_.led.activeColor));
    } else {
      setMatrix(i, CRGB::Black);
    }
  }
  FastLED.show();
}

void HeavyBlasterController::updatePreUnlockEffect(uint32_t now) {
  const uint32_t elapsed = now - preUnlockStartMs_;
  if (elapsed >= config_.unlock.preEffectMs) {
    preUnlockActive_ = false;
    unlockedEffectActive_ = true;
    lastEffectUpdateMs_ = 0;
    lastEvent_ = "unlock_effect";
    emit("unlock_effect", -1, "effect");
    return;
  }
  if (now - lastPreEffectUpdateMs_ < ::heavy_blaster::PRE_EFFECT_UPDATE_MS) return;
  lastPreEffectUpdateMs_ = now;

  uint8_t brightness = 0;
  if (elapsed < config_.unlock.fadeOutMs && config_.unlock.fadeOutMs > 0) {
    const float progress = elapsed / static_cast<float>(config_.unlock.fadeOutMs);
    const float fade = (cos(progress * PI) + 1.0f) * 0.5f;
    brightness = static_cast<uint8_t>(fade * 255.0f);
  } else {
    const uint32_t periodMs = 60000UL / max<uint16_t>(config_.unlock.blinkBpm, 1);
    const uint32_t blinkElapsed = elapsed > config_.unlock.fadeOutMs ? elapsed - config_.unlock.fadeOutMs : 0;
    brightness = ((blinkElapsed % periodMs) < (periodMs / 2)) ? 255 : 0;
  }

  for (uint8_t i = 0; i < slotCount(); ++i) {
    fill_solid(matrices_[i], matrixLedCount(), CHSV(45, 255, brightness));
  }
  FastLED.show();
}

void HeavyBlasterController::updateUnlockedEffect(uint32_t now) {
  if (now - lastEffectUpdateMs_ < ::heavy_blaster::EFFECT_UPDATE_MS) return;
  lastEffectUpdateMs_ = now;

  const uint16_t cycleFrame = effectFrame_ % (::heavy_blaster::ARROW_FRAMES + ::heavy_blaster::RAINBOW_FRAMES);
  const bool arrowMode = cycleFrame < ::heavy_blaster::ARROW_FRAMES;
  for (uint8_t m = 0; m < slotCount(); ++m) {
    fill_solid(matrices_[m], matrixLedCount(), CRGB::Black);
    if (arrowMode) {
      drawMovingArrow(m, cycleFrame);
      drawRainbowTrail(m, cycleFrame);
    } else {
      drawFullRainbow(m);
    }
  }
  FastLED.show();
  rainbowOffset_ += ::heavy_blaster::RAINBOW_SPEED;
  effectFrame_++;
}

void HeavyBlasterController::render(uint32_t now) {
  if (now - lastShowMs_ < ::heavy_blaster::LED_SHOW_PERIOD_MS) return;
  lastShowMs_ = now;
  if (preUnlockActive_) {
    updatePreUnlockEffect(now);
    return;
  }
  if (unlockedEffectActive_) {
    updateUnlockedEffect(now);
    return;
  }
  showNormalState();
}

void HeavyBlasterController::drawMovingArrow(uint8_t matrixIndex, int frame) {
  const int baseRow = ::heavy_blaster::MATRIX_HEIGHT - 1 - frame;
  const int arrow[][2] = {
      {3, 0}, {4, 0},
      {2, 1}, {3, 1}, {4, 1}, {5, 1},
      {1, 2}, {2, 2}, {3, 2}, {4, 2}, {5, 2}, {6, 2},
      {3, 3}, {4, 3}, {3, 4}, {4, 4}, {3, 5}, {4, 5}, {3, 6}, {4, 6},
  };
  const int arrowSize = sizeof(arrow) / sizeof(arrow[0]);
  for (int i = 0; i < arrowSize; ++i) {
    const int col = arrow[i][0];
    const int row = arrow[i][1] + baseRow;
    if (row >= 0 && row < ::heavy_blaster::MATRIX_HEIGHT) setVisualPixel(matrixIndex, col, row, CRGB::White);
  }
}

void HeavyBlasterController::drawRainbowTrail(uint8_t matrixIndex, int frame) {
  const int baseRow = ::heavy_blaster::MATRIX_HEIGHT - 1 - frame;
  constexpr int kTrailLength = 8;
  for (int trail = 1; trail <= kTrailLength; ++trail) {
    const int row = baseRow + trail + 6;
    if (row < 0 || row >= ::heavy_blaster::MATRIX_HEIGHT) continue;
    const uint8_t hue = rainbowOffset_ + trail * 25;
    for (int col = 0; col < ::heavy_blaster::MATRIX_WIDTH; ++col) {
      setVisualPixel(matrixIndex, col, row, CHSV(hue, 255, 255));
    }
  }
}

void HeavyBlasterController::drawFullRainbow(uint8_t matrixIndex) {
  for (int y = 0; y < ::heavy_blaster::MATRIX_HEIGHT; ++y) {
    for (int x = 0; x < ::heavy_blaster::MATRIX_WIDTH; ++x) {
      const uint8_t hue = rainbowOffset_ + (::heavy_blaster::MATRIX_WIDTH - 1 - x) * 20;
      matrices_[matrixIndex][xy(x, y)] = CHSV(hue, 255, 255);
    }
  }
}

int HeavyBlasterController::xy(int x, int y) const {
  if (y % 2 == 0) return y * ::heavy_blaster::MATRIX_WIDTH + x;
  return y * ::heavy_blaster::MATRIX_WIDTH + (::heavy_blaster::MATRIX_WIDTH - 1 - x);
}

void HeavyBlasterController::setVisualPixel(uint8_t matrixIndex, int col, int row, CRGB color) {
  if (matrixIndex >= slotCount()) return;
  const int x = ::heavy_blaster::MATRIX_WIDTH - 1 - row;
  const int y = col;
  const int index = xy(x, y);
  if (index >= 0 && index < matrixLedCount()) matrices_[matrixIndex][index] = color;
}

void HeavyBlasterController::appendStatus(JsonObject obj) const {
  obj["blaster_id"] = config_.blasterId;
  obj["device_id"] = config_.deviceId;
  obj["device_mac"] = config_.deviceMac;
  obj["display_name"] = config_.displayName;
  obj["name"] = config_.displayName;
  obj["configured"] = config_.configured;
  obj["config_version"] = config_.configVersion;
  obj["mode"] = modeString();
  obj["effect"] = effectString();
  obj["slot_count"] = slotCount();
  obj["required_slots"] = requiredSlots();
  obj["active_slot_count"] = activeSlotCount();
  obj["unlock_ready"] = allRequiredSlotsActive();
  obj["relay_on"] = relayOn_;
  obj["relay_pin"] = ::heavy_blaster::RELAY_PIN;
  obj["relay_active_low"] = config_.hardware.relayActiveLow;
  obj["relay_profile"] = config_.hardware.relayProfile;
  obj["debug_allow_local_control"] = config_.debugAllowLocalControl;
  obj["ota_safe"] = isSafeForOta();
  obj["last_event"] = lastEvent_;
  obj["last_changed_slot"] = lastChangedSlot_;
  JsonArray slots = obj["slots_active"].to<JsonArray>();
  for (uint8_t i = 0; i < slotCount(); ++i) slots.add(slots_[i]);
}

String HeavyBlasterController::statusSignature() const {
  String s;
  s.reserve(128);
  s += modeString();
  s += '|';
  for (uint8_t i = 0; i < slotCount(); ++i) s += slots_[i] ? '1' : '0';
  s += '|';
  s += relayOn_ ? '1' : '0';
  s += '|';
  s += preUnlockActive_ ? '1' : '0';
  s += '|';
  s += unlockedEffectActive_ ? '1' : '0';
  s += '|';
  s += lastEvent_;
  return s;
}

void HeavyBlasterController::printBootBanner() const {
  Serial.print("[heavy-blaster] app=");
  Serial.print(::heavy_blaster::FIRMWARE_NAME);
  Serial.print(" blaster_id=");
  Serial.print(config_.blasterId);
  Serial.print(" display_name=");
  Serial.print(config_.displayName);
  Serial.print(" slots=");
  Serial.print(slotCount());
  Serial.print(" relay_pin=");
  Serial.print(::heavy_blaster::RELAY_PIN);
  Serial.print(" relay_active_low=");
  Serial.println(config_.hardware.relayActiveLow ? "true" : "false");
}

}  // namespace heavy_blaster
}  // namespace battlebang
