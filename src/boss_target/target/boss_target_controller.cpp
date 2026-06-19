#include "boss_target_controller.h"

#include <Esp.h>

namespace battlebang {
namespace boss_target {

BossTargetController* BossTargetController::isrInstance_ = nullptr;

void BossTargetController::begin(const RuntimeConfig& config, EventCallback callback, void* ctx) {
  callback_ = callback;
  callbackCtx_ = ctx;
  config_ = config;
  FastLED.addLeds<WS2811, ::boss_target::RING1_PIN, RGB>(ring1_, ::boss_target::MAX_RING_NUM_LEDS);
  FastLED.addLeds<WS2811, ::boss_target::RING2_PIN, RGB>(ring2_, ::boss_target::MAX_RING_NUM_LEDS);
  FastLED.addLeds<WS2811, ::boss_target::RING3_PIN, RGB>(ring3_, ::boss_target::MAX_RING_NUM_LEDS);
  FastLED.addLeds<WS2811, ::boss_target::RING4_PIN, RGB>(ring4_, ::boss_target::MAX_RING_NUM_LEDS);
  FastLED.addLeds<WS2811, ::boss_target::HP_BAR_PIN, RGB>(hpBar_, ::boss_target::MAX_HP_BAR_NUM_LEDS);
  FastLED.setBrightness(config_.hpBar.brightness);
  FastLED.setMaxPowerInVoltsAndMilliamps(::boss_target::LED_MAX_VOLTS, config_.hpBar.maxMa);
  configurePins(RuntimeConfig{}, config_);
  randomSeed(esp_random());
  reset("boot");
}

void BossTargetController::applyConfig(const RuntimeConfig& config, bool resetState, const char* source) {
  RuntimeConfig previous = config_;
  config_ = config;
  FastLED.setBrightness(config_.hpBar.brightness);
  FastLED.setMaxPowerInVoltsAndMilliamps(::boss_target::LED_MAX_VOLTS, config_.hpBar.maxMa);
  if (resetState) {
    reset(source);
  } else if (!config_.configured && mode_ != Mode::UNCONFIGURED) {
    mode_ = Mode::UNCONFIGURED;
    clearActiveTarget();
  } else if (config_.configured && mode_ == Mode::UNCONFIGURED) {
    reset(source);
  }
  if (!pinsConfigured_ || sensorPinsChanged(previous, config_)) configurePins(previous, config_);
}

void BossTargetController::configurePins(const RuntimeConfig& previous, const RuntimeConfig& next) {
  (void) previous;
  isrInstance_ = this;
  for (uint8_t i = 0; i < ::boss_target::kMaxTargets; ++i) {
    if (next.hardware.piezoDoPins[i] >= 0) pinMode(next.hardware.piezoDoPins[i], INPUT_PULLDOWN);
  }
  attachInterrupt(digitalPinToInterrupt(::boss_target::PIEZO_DO_PINS[0]), piezoIsr0, RISING);
  attachInterrupt(digitalPinToInterrupt(::boss_target::PIEZO_DO_PINS[1]), piezoIsr1, RISING);
  attachInterrupt(digitalPinToInterrupt(::boss_target::PIEZO_DO_PINS[2]), piezoIsr2, RISING);
  attachInterrupt(digitalPinToInterrupt(::boss_target::PIEZO_DO_PINS[3]), piezoIsr3, RISING);
  clearPiezoEdges();
  pinsConfigured_ = true;
}

void BossTargetController::reset(const char* source) {
  hpRemaining_ = config_.gameplay.hpMax;
  sequence_ = 0;
  clearActiveTarget();
  lastAcceptedHitMs_ = 0;
  lastHitTargetIndex_ = -1;
  lastWrongTargetIndex_ = -1;
  lastEvent_ = "reset";
  hpFlashUntilMs_ = 0;
  hpBlinkOn_ = false;
  deadBlinkOn_ = false;
  otaPrepared_ = false;
  hitEnabled_ = true;
  clearHpBlinkMask();
  for (uint8_t i = 0; i < ::boss_target::kMaxTargets; ++i) {
    lastTargetHitMs_[i] = 0;
    lastTargetHitOk_[i] = false;
    targetFlashUntilMs_[i] = 0;
  }
  clearPiezoEdges();
  mode_ = config_.configured ? Mode::READY : Mode::UNCONFIGURED;
  clearAllLeds();
  FastLED.show();
  emit("reset", 255, 0, source, 0);
}

void BossTargetController::start(const char* source, bool resetHp) {
  if (!config_.configured) {
    mode_ = Mode::UNCONFIGURED;
    lastEvent_ = "start_rejected_unconfigured";
    emit("start_rejected", 255, 0, source, 0);
    return;
  }
  if (resetHp || config_.gameplay.startResetsHp || hpRemaining_ <= 0) {
    hpRemaining_ = config_.gameplay.hpMax;
    clearHpBlinkMask();
    hpFlashUntilMs_ = 0;
    hpBlinkOn_ = false;
  }
  mode_ = Mode::ACTIVE;
  otaPrepared_ = false;
  hitEnabled_ = true;
  clearPiezoEdges();
  selectNewTarget(millis());
  lastEvent_ = "start";
  emit("start", static_cast<uint8_t>(activeTarget_ >= 0 ? activeTarget_ : 255), 0, source, 0);
}

void BossTargetController::simulateHit(const char* source, int targetIndex) {
  uint32_t now = millis();
  if (targetIndex < 0) targetIndex = activeTarget_;
  if (targetIndex < 0 || targetIndex >= targetCount()) return;
  if (targetIndex == activeTarget_) {
    applyDamage(static_cast<uint8_t>(targetIndex), source, 1, now);
  } else {
    recordWrongHit(static_cast<uint8_t>(targetIndex), source, 1, now);
  }
}

void BossTargetController::prepareForOta() {
  hitEnabled_ = false;
  otaPrepared_ = true;
  clearActiveTarget();
  clearAllLeds();
  FastLED.show();
}

bool BossTargetController::isSafeForOta() const {
  return mode_ == Mode::READY || mode_ == Mode::DEFEATED || mode_ == Mode::UNCONFIGURED || otaPrepared_;
}

bool BossTargetController::vulnerableNow(uint32_t) const {
  return hitEnabled_ && mode_ == Mode::ACTIVE && hpRemaining_ > 0 && activeTarget_ >= 0;
}

void BossTargetController::loop(uint32_t now) {
  if (mode_ == Mode::ACTIVE && hpRemaining_ > 0 && activeTarget_ >= 0 &&
      now - targetStartedMs_ >= config_.gameplay.targetDurationMs) {
    selectNewTarget(now);
  }

  if (vulnerableNow(now)) {
    for (uint8_t i = 0; i < targetCount(); ++i) {
      uint16_t edges = popPiezoEdges(i);
      if (edges == 0) continue;
      if (now - lastTargetHitMs_[i] < config_.gameplay.hitCooldownMs) continue;
      lastTargetHitMs_[i] = now;
      if (i == activeTarget_) {
        applyDamage(i, "piezo", edges, now);
      } else {
        recordWrongHit(i, "piezo", edges, now);
      }
    }
  } else {
    clearPiezoEdges();
  }

  renderLeds(now);
}

uint8_t BossTargetController::targetCount() const {
  return constrain(config_.target.count, static_cast<uint8_t>(1), static_cast<uint8_t>(::boss_target::kMaxTargets));
}

uint16_t BossTargetController::ringLedCount() const {
  return constrain(config_.target.ringNumLeds, static_cast<uint16_t>(1), static_cast<uint16_t>(::boss_target::MAX_RING_NUM_LEDS));
}

uint16_t BossTargetController::hpLedCount() const {
  return constrain(config_.hpBar.numLeds, static_cast<uint16_t>(1), static_cast<uint16_t>(::boss_target::MAX_HP_BAR_NUM_LEDS));
}

CRGB BossTargetController::colorFromRgb(uint32_t rgb) const {
  return CRGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

uint8_t BossTargetController::hpPhase() const {
  if (hpRemaining_ <= 0) return activePhaseCount(config_) - 1;
  const int consumed = max(0, static_cast<int>(config_.gameplay.hpMax) - hpRemaining_);
  uint8_t phase = static_cast<uint8_t>((static_cast<uint32_t>(consumed) * activePhaseCount(config_)) / config_.gameplay.hpMax);
  return constrain(phase, static_cast<uint8_t>(0), static_cast<uint8_t>(activePhaseCount(config_) - 1));
}

CRGB BossTargetController::hpColor() const {
  return hpColorForBand(hpBandForValue(hpRemaining_));
}

CRGB BossTargetController::hpColorForBand(uint8_t band) const {
  const uint8_t count = activePhaseCount(config_);
  if (band >= count) band = count - 1;
  const uint8_t paletteIndex = count - 1 - band;
  return colorFromRgb(phaseColorRgb(config_, paletteIndex));
}

CRGB BossTargetController::nextHpColorForBand(uint8_t band) const {
  if (band == 0) return hpColorForBand(0);
  return hpColorForBand(band - 1);
}

uint16_t BossTargetController::hpLitCount() const {
  return hpLitCountForValue(hpRemaining_);
}

uint8_t BossTargetController::hpBandForValue(int hp) const {
  if (hp <= 0) return 0;
  const uint8_t count = activePhaseCount(config_);
  const uint32_t band = ((static_cast<uint32_t>(hp) - 1) * count) / config_.gameplay.hpMax;
  return constrain(static_cast<uint8_t>(band), static_cast<uint8_t>(0), static_cast<uint8_t>(count - 1));
}

uint16_t BossTargetController::hpLitCountForValue(int hp) const {
  if (hp <= 0) return 0;
  const uint8_t count = activePhaseCount(config_);
  const uint8_t band = hpBandForValue(hp);
  const uint32_t lower = (static_cast<uint32_t>(band) * config_.gameplay.hpMax) / count;
  const uint32_t upper = (static_cast<uint32_t>(band + 1) * config_.gameplay.hpMax) / count;
  const uint32_t span = max<uint32_t>(1, upper - lower);
  const uint32_t inBand = constrain(static_cast<int32_t>(hp - lower), static_cast<int32_t>(0), static_cast<int32_t>(span));
  const uint32_t lit = (inBand * hpLedCount()) / span;
  return static_cast<uint16_t>(constrain(static_cast<int>(lit), 0, static_cast<int>(hpLedCount())));
}

const char* BossTargetController::modeString() const {
  switch (mode_) {
    case Mode::UNCONFIGURED: return "UNCONFIGURED";
    case Mode::READY: return "READY";
    case Mode::ACTIVE: return "ACTIVE";
    case Mode::DEFEATED: return "DEFEATED";
  }
  return "UNKNOWN";
}

const char* BossTargetController::commandState() const {
  switch (mode_) {
    case Mode::UNCONFIGURED: return "unconfigured";
    case Mode::READY: return "ready";
    case Mode::ACTIVE: return "active";
    case Mode::DEFEATED: return "dead";
  }
  return "unknown";
}

void BossTargetController::selectNewTarget(uint32_t now) {
  const uint8_t count = targetCount();
  int next = 0;
  if (count == 1) {
    next = 0;
  } else {
    do {
      next = random(0, count);
    } while (next == activeTarget_);
  }
  activeTarget_ = next;
  targetStartedMs_ = now;
  lastEvent_ = "target_selected";
}

void BossTargetController::clearActiveTarget() {
  activeTarget_ = -1;
  targetStartedMs_ = 0;
}

void BossTargetController::applyDamage(uint8_t targetIndex, const char* source, uint16_t edges, uint32_t now) {
  if (!vulnerableNow(now)) return;
  if (now - lastAcceptedHitMs_ < config_.gameplay.hitCooldownMs) return;
  lastAcceptedHitMs_ = now;
  const int oldHp = hpRemaining_;
  const uint8_t oldBand = hpBandForValue(oldHp);
  hpRemaining_ = max(0, hpRemaining_ - static_cast<int>(config_.gameplay.damagePerHit));
  const uint8_t newBand = hpBandForValue(hpRemaining_);
  if (newBand != oldBand) clearHpBlinkMask();
  if (hpRemaining_ > 0) addHpBlinkSegment(oldHp, hpRemaining_);
  hpFlashUntilMs_ = now + ::boss_target::HIT_FLASH_MS;
  sequence_++;
  lastHitTargetIndex_ = targetIndex;
  lastTargetHitMs_[targetIndex] = now;
  lastTargetHitOk_[targetIndex] = true;
  targetFlashUntilMs_[targetIndex] = now + ::boss_target::HIT_FLASH_MS;
  if (hpRemaining_ <= 0) {
    hpRemaining_ = 0;
    mode_ = Mode::DEFEATED;
    clearActiveTarget();
    lastEvent_ = "destroyed";
    emit("destroyed", targetIndex, 0, source, edges);
  } else {
    selectNewTarget(now);
    lastEvent_ = "hit";
    emit("hit", targetIndex, 0, source, edges);
  }
}

void BossTargetController::recordWrongHit(uint8_t targetIndex, const char* source, uint16_t edges, uint32_t now) {
  sequence_++;
  lastWrongTargetIndex_ = targetIndex;
  lastTargetHitMs_[targetIndex] = now;
  lastTargetHitOk_[targetIndex] = false;
  lastEvent_ = "wrong_hit";
  emit("wrong_hit", targetIndex, 0, source, edges);
}

void BossTargetController::emit(const char* name, uint8_t targetIndex, uint16_t peak, const char* source, uint16_t edges) {
  if (callback_ == nullptr) return;
  BossTargetEvent event;
  event.name = name;
  event.source = source;
  event.targetIndex = targetIndex;
  event.peak = peak;
  event.digitalEdges = edges;
  callback_(event, callbackCtx_);
}

void BossTargetController::fillRing(uint8_t index, const CRGB& color) {
  if (index >= ::boss_target::kMaxTargets) return;
  fill_solid(rings_[index], ringLedCount(), color);
  for (uint16_t i = ringLedCount(); i < ::boss_target::MAX_RING_NUM_LEDS; ++i) rings_[index][i] = CRGB::Black;
}

void BossTargetController::renderTargets(uint32_t now) {
  for (uint8_t i = 0; i < ::boss_target::kMaxTargets; ++i) fillRing(i, CRGB::Black);
  if (mode_ != Mode::ACTIVE || activeTarget_ < 0) return;
  for (uint8_t i = 0; i < targetCount(); ++i) {
    if (targetFlashUntilMs_[i] != 0 && static_cast<int32_t>(targetFlashUntilMs_[i] - now) > 0) {
      fillRing(i, colorFromRgb(config_.target.hitFlashColor));
    } else if (i == activeTarget_) {
      fillRing(i, colorFromRgb(config_.target.activeColor));
    }
  }
}

void BossTargetController::renderHpBar(uint32_t now) {
  fill_solid(hpBar_, ::boss_target::MAX_HP_BAR_NUM_LEDS, CRGB::Black);
  if (mode_ == Mode::READY || mode_ == Mode::UNCONFIGURED || otaPrepared_) return;
  if (static_cast<int32_t>(hpFlashUntilMs_ - now) > 0) {
    fill_solid(hpBar_, hpLedCount(), CRGB::White);
    return;
  }
  if (mode_ == Mode::DEFEATED) {
    if (now - lastDeadBlinkMs_ >= config_.hpBar.deadBlinkMs) {
      lastDeadBlinkMs_ = now;
      deadBlinkOn_ = !deadBlinkOn_;
    }
    if (deadBlinkOn_) fill_solid(hpBar_, hpLedCount(), colorFromRgb(::boss_target::HP_RED));
    return;
  }
  const uint16_t lit = hpLitCount();
  const uint8_t band = hpBandForValue(hpRemaining_);
  const CRGB base = hpColorForBand(band);
  const CRGB blink = nextHpColorForBand(band);
  if (now - lastHpBlinkMs_ >= ::boss_target::BLINK_MS) {
    lastHpBlinkMs_ = now;
    hpBlinkOn_ = !hpBlinkOn_;
  }
  for (uint16_t i = 0; i < hpLedCount(); ++i) {
    if (i < lit) {
      hpBar_[i] = base;
    } else if (hpBlinkMask_[i]) {
      hpBar_[i] = hpBlinkOn_ ? blink : CRGB::Black;
    }
  }
}

void BossTargetController::clearHpBlinkMask() {
  for (uint16_t i = 0; i < ::boss_target::MAX_HP_BAR_NUM_LEDS; ++i) hpBlinkMask_[i] = false;
}

void BossTargetController::addHpBlinkSegment(int oldHp, int newHp) {
  const uint16_t oldLit = hpLitCountForValue(oldHp);
  const uint16_t newLit = hpLitCountForValue(newHp);
  if (newLit < oldLit) {
    for (uint16_t i = newLit; i < oldLit && i < hpLedCount(); ++i) hpBlinkMask_[i] = true;
    return;
  }
  const uint8_t oldBand = hpBandForValue(oldHp);
  const uint8_t newBand = hpBandForValue(newHp);
  if (newBand < oldBand) {
    for (uint16_t i = newLit; i < hpLedCount(); ++i) hpBlinkMask_[i] = true;
  }
}

void BossTargetController::clearAllLeds() {
  for (uint8_t i = 0; i < ::boss_target::kMaxTargets; ++i) {
    fill_solid(rings_[i], ::boss_target::MAX_RING_NUM_LEDS, CRGB::Black);
  }
  fill_solid(hpBar_, ::boss_target::MAX_HP_BAR_NUM_LEDS, CRGB::Black);
}

void BossTargetController::renderLeds(uint32_t now) {
  if (now - lastShowMs_ < ::boss_target::LED_SHOW_PERIOD_MS) return;
  lastShowMs_ = now;
  renderTargets(now);
  renderHpBar(now);
  FastLED.show();
}

uint16_t BossTargetController::popPiezoEdges(uint8_t index) {
  if (index >= ::boss_target::kMaxTargets) return 0;
  noInterrupts();
  uint16_t pending = piezoEdgeCount_[index];
  piezoEdgeCount_[index] = 0;
  interrupts();
  return pending;
}

void BossTargetController::clearPiezoEdges() {
  noInterrupts();
  for (uint8_t i = 0; i < ::boss_target::kMaxTargets; ++i) piezoEdgeCount_[i] = 0;
  interrupts();
}

void IRAM_ATTR BossTargetController::piezoIsr0() { if (isrInstance_) isrInstance_->onPiezoIsr(0); }
void IRAM_ATTR BossTargetController::piezoIsr1() { if (isrInstance_) isrInstance_->onPiezoIsr(1); }
void IRAM_ATTR BossTargetController::piezoIsr2() { if (isrInstance_) isrInstance_->onPiezoIsr(2); }
void IRAM_ATTR BossTargetController::piezoIsr3() { if (isrInstance_) isrInstance_->onPiezoIsr(3); }

void IRAM_ATTR BossTargetController::onPiezoIsr(uint8_t index) {
  if (index >= ::boss_target::kMaxTargets) return;
  const uint32_t nowUs = micros();
  if (nowUs - lastIsrUs_[index] < config_.gameplay.digitalIsrDebounceUs) return;
  lastIsrUs_[index] = nowUs;
  if (piezoEdgeCount_[index] < UINT16_MAX) piezoEdgeCount_[index]++;
}

void BossTargetController::appendStatus(JsonObject obj) const {
  const uint32_t now = millis();
  obj["boss_id"] = config_.bossId;
  obj["target_id"] = config_.targetId;
  obj["display_name"] = config_.displayName;
  obj["name"] = config_.displayName;
  obj["device_id"] = config_.deviceId;
  obj["device_mac"] = config_.deviceMac;
  obj["configured"] = config_.configured;
  obj["config_version"] = config_.configVersion;
  obj["mode"] = modeString();
  obj["command_state"] = commandState();
  obj["life_state"] = hpRemaining_ <= 0 ? "dead" : "alive";
  obj["sequence"] = sequence_;
  obj["hp_remaining"] = hpRemaining_;
  obj["hp_max"] = config_.gameplay.hpMax;
  obj["hp_pct"] = config_.gameplay.hpMax == 0 ? 0 : (hpRemaining_ * 100) / config_.gameplay.hpMax;
  obj["hp_phase"] = hpPhase() + 1;
  obj["hp_phase_count"] = activePhaseCount(config_);
  obj["damage_per_hit"] = config_.gameplay.damagePerHit;
  obj["destroyed"] = hpRemaining_ <= 0;
  obj["hit_enabled"] = hitEnabled_;
  obj["vulnerable"] = vulnerableNow(now);
  obj["activation_active"] = vulnerableNow(now);
  obj["hit_target_active"] = vulnerableNow(now);
  obj["active_target_index"] = activeTarget_ >= 0 ? activeTarget_ : -1;
  obj["active_target_id"] = activeTarget_ >= 0 ? String("target_") + String(activeTarget_ + 1) : String("");
  obj["target_count"] = targetCount();
  obj["hardware_max_targets"] = ::boss_target::kMaxTargets;
  obj["target_duration_ms"] = config_.gameplay.targetDurationMs;
  obj["hit_cooldown_ms"] = config_.gameplay.hitCooldownMs;
  obj["digital_isr_debounce_us"] = config_.gameplay.digitalIsrDebounceUs;
  obj["last_hit_target_index"] = lastHitTargetIndex_;
  obj["last_wrong_target_index"] = lastWrongTargetIndex_;
  obj["last_event"] = lastEvent_;
  obj["ota_safe"] = isSafeForOta();

  JsonArray targets = obj.createNestedArray("targets");
  for (uint8_t i = 0; i < targetCount(); ++i) {
    JsonObject t = targets.createNestedObject();
    t["index"] = i;
    t["id"] = String("target_") + String(i + 1);
    t["active"] = i == activeTarget_ && mode_ == Mode::ACTIVE;
    t["last_hit_ms"] = lastTargetHitMs_[i];
    t["last_hit_ok"] = lastTargetHitOk_[i];
  }
}

String BossTargetController::statusSignature() const {
  String s;
  s.reserve(160);
  s += modeString();
  s += '|';
  s += String(sequence_);
  s += '|';
  s += String(hpRemaining_);
  s += '|';
  s += String(activeTarget_);
  s += '|';
  s += hitEnabled_ ? '1' : '0';
  s += '|';
  s += lastEvent_;
  return s;
}

void BossTargetController::printBootBanner() const {
  Serial.print("[boss_target] app=");
  Serial.print(::boss_target::FIRMWARE_NAME);
  Serial.print(" boss_id=");
  Serial.print(config_.bossId);
  Serial.print(" display_name=");
  Serial.print(config_.displayName);
  Serial.print(" targets=");
  Serial.print(targetCount());
  Serial.print("/max=");
  Serial.print(::boss_target::kMaxTargets);
  Serial.print(" hp=");
  Serial.println(config_.gameplay.hpMax);
}

}  // namespace boss_target
}  // namespace battlebang
