#include "boss_target_controller.h"

#include <Esp.h>

namespace battlebang {
namespace boss_target {

namespace {

uint8_t circularDistance8(uint8_t lhs, uint8_t rhs) {
  uint8_t delta = lhs > rhs ? lhs - rhs : rhs - lhs;
  return delta > 127 ? 255 - delta : delta;
}

uint8_t neonOrbitValue(uint8_t distance, uint8_t width) {
  static constexpr uint8_t kFloor = 70;
  if (distance >= width) return kFloor;
  return static_cast<uint8_t>(kFloor + ((static_cast<uint16_t>(width - distance) * (255 - kFloor)) / width));
}

}  // namespace

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
  analogReadResolution(12);
  for (uint8_t i = 0; i < ::boss_target::kMaxTargets; ++i) {
    if (next.hardware.piezoDoPins[i] < 0) continue;
    pinMode(next.hardware.piezoDoPins[i], INPUT);
    analogSetPinAttenuation(next.hardware.piezoDoPins[i], ADC_11db);
  }
  clearPiezoEdges();
  pinsConfigured_ = true;
}

void BossTargetController::reset(const char* source) {
  hpRemaining_ = config_.gameplay.hpMax;
  sequence_ = 0;
  clearActiveTarget();
  startIntroStartedMs_ = 0;
  startIntroUntilMs_ = 0;
  lastAcceptedHitMs_ = 0;
  lastHitTargetIndex_ = -1;
  lastWrongTargetIndex_ = -1;
  lastEvent_ = "reset";
  nextTargetSelectionMs_ = 0;
  deadBlinkOn_ = false;
  otaPrepared_ = false;
  hitEnabled_ = true;
  targetTransitionPending_ = false;
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
  }
  const uint32_t now = millis();
  clearActiveTarget();
  for (uint8_t i = 0; i < ::boss_target::kMaxTargets; ++i) targetFlashUntilMs_[i] = 0;
  mode_ = Mode::INTRO;
  startIntroStartedMs_ = now;
  startIntroUntilMs_ = now + ::boss_target::START_INTRO_MS;
  otaPrepared_ = false;
  hitEnabled_ = true;
  targetTransitionPending_ = false;
  nextTargetSelectionMs_ = 0;
  clearPiezoEdges();
  lastEvent_ = "start_intro";
  emit("start", 255, 0, source, 0);
}

void BossTargetController::simulateHit(const char* source, int targetIndex) {
  uint32_t now = millis();
  if (targetIndex < 0) targetIndex = activeTarget_;
  if (targetIndex < 0 || targetIndex >= targetCount()) return;
  if (targetIndex == activeTarget_) {
    applyDamage(static_cast<uint8_t>(targetIndex), source, 1, 0, now);
  } else {
    recordWrongHit(static_cast<uint8_t>(targetIndex), source, 1, 0, now);
  }
}

void BossTargetController::prepareForOta() {
  hitEnabled_ = false;
  otaPrepared_ = true;
  clearActiveTarget();
  clearAllLeds();
  FastLED.show();
}

void BossTargetController::recoverFromFailedOta(const char* source) {
  otaPrepared_ = false;
  hitEnabled_ = true;
  if (mode_ == Mode::INTRO || mode_ == Mode::ACTIVE) {
    reset(source);
    return;
  }
  const uint32_t now = millis();
  lastShowMs_ = now - ::boss_target::LED_SHOW_PERIOD_MS;
  renderLeds(now);
  FastLED.show();
  emit("ota_failed", 255, 0, source, 0);
}

bool BossTargetController::isSafeForOta() const {
  return mode_ == Mode::READY || mode_ == Mode::DEFEATED || mode_ == Mode::UNCONFIGURED || otaPrepared_;
}

bool BossTargetController::vulnerableNow(uint32_t) const {
  return hitEnabled_ && mode_ == Mode::ACTIVE && hpRemaining_ > 0 && activeTarget_ >= 0 && !targetTransitionPending_;
}

void BossTargetController::loop(uint32_t now) {
  if (mode_ == Mode::INTRO && hpRemaining_ > 0 && static_cast<int32_t>(now - startIntroUntilMs_) >= 0) {
    mode_ = Mode::ACTIVE;
    startIntroStartedMs_ = 0;
    startIntroUntilMs_ = 0;
    selectNewTarget(now);
  }

  if (mode_ == Mode::ACTIVE && hpRemaining_ > 0 && targetTransitionPending_ &&
      static_cast<int32_t>(now - nextTargetSelectionMs_) >= 0) {
    targetTransitionPending_ = false;
    nextTargetSelectionMs_ = 0;
    selectNewTarget(now);
  }

  if (mode_ == Mode::ACTIVE && hpRemaining_ > 0 && !targetTransitionPending_ && activeTarget_ >= 0 &&
      now - targetStartedMs_ >= config_.gameplay.targetDurationMs) {
    selectNewTarget(now);
  }

  if (vulnerableNow(now)) {
    pollPiezoAo(now);
    for (uint8_t i = 0; i < targetCount(); ++i) {
      uint16_t edges = popPiezoEdges(i);
      if (edges == 0) continue;
      uint16_t peak = popPiezoPeak(i);
      if (now - lastTargetHitMs_[i] < config_.gameplay.hitCooldownMs) continue;
      lastTargetHitMs_[i] = now;
      if (i == activeTarget_) {
        applyDamage(i, "piezo_ao", edges, peak, now);
      } else {
        recordWrongHit(i, "piezo_ao", edges, peak, now);
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

uint16_t BossTargetController::hpGroupCount() const {
  return constrain(static_cast<uint16_t>(hpLedCount() / ::boss_target::HP_BAR_LEDS_PER_GROUP),
                   static_cast<uint16_t>(1),
                   static_cast<uint16_t>(::boss_target::MAX_HP_BAR_GROUP_COUNT));
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

uint16_t BossTargetController::hpLitGroupCount() const {
  return hpLitGroupCountForValue(hpRemaining_);
}

uint8_t BossTargetController::hpBandForValue(int hp) const {
  if (hp <= 0) return 0;
  const uint8_t count = activePhaseCount(config_);
  const uint32_t band = ((static_cast<uint32_t>(hp) - 1) * count) / config_.gameplay.hpMax;
  return constrain(static_cast<uint8_t>(band), static_cast<uint8_t>(0), static_cast<uint8_t>(count - 1));
}

uint16_t BossTargetController::hpLitGroupCountForValue(int hp) const {
  if (hp <= 0) return 0;
  if (config_.gameplay.hpMax == 0) return 0;
  const uint32_t clampedHp = min<uint32_t>(static_cast<uint32_t>(hp), config_.gameplay.hpMax);
  const uint32_t lit = (clampedHp * hpGroupCount() + config_.gameplay.hpMax - 1) / config_.gameplay.hpMax;
  return static_cast<uint16_t>(constrain(static_cast<int>(lit), 0, static_cast<int>(hpGroupCount())));
}

const char* BossTargetController::modeString() const {
  switch (mode_) {
    case Mode::UNCONFIGURED: return "UNCONFIGURED";
    case Mode::READY: return "READY";
    case Mode::INTRO: return "INTRO";
    case Mode::ACTIVE: return "ACTIVE";
    case Mode::DEFEATED: return "DEFEATED";
  }
  return "UNKNOWN";
}

const char* BossTargetController::commandState() const {
  switch (mode_) {
    case Mode::UNCONFIGURED: return "unconfigured";
    case Mode::READY: return "ready";
    case Mode::INTRO: return "intro";
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
  targetTransitionPending_ = false;
  nextTargetSelectionMs_ = 0;
}

void BossTargetController::applyDamage(uint8_t targetIndex, const char* source, uint16_t edges, uint16_t peak, uint32_t now) {
  if (!vulnerableNow(now)) return;
  if (now - lastAcceptedHitMs_ < config_.gameplay.hitCooldownMs) return;
  lastAcceptedHitMs_ = now;
  hpRemaining_ = max(0, hpRemaining_ - static_cast<int>(config_.gameplay.damagePerHit));
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
    emit("destroyed", targetIndex, peak, source, edges);
  } else {
    targetTransitionPending_ = true;
    nextTargetSelectionMs_ = now + ::boss_target::HIT_FLASH_MS;
    lastEvent_ = "hit";
    emit("hit", targetIndex, peak, source, edges);
  }
}

void BossTargetController::recordWrongHit(uint8_t targetIndex, const char* source, uint16_t edges, uint16_t peak, uint32_t now) {
  sequence_++;
  lastWrongTargetIndex_ = targetIndex;
  lastTargetHitMs_[targetIndex] = now;
  lastTargetHitOk_[targetIndex] = false;
  lastEvent_ = "wrong_hit";
  emit("wrong_hit", targetIndex, peak, source, edges);
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

void BossTargetController::setHpBarGroup(uint16_t group0Based, const CRGB& color) {
  const uint16_t groups = hpGroupCount();
  if (group0Based >= groups) return;

  // Three-row serpentine HP bar layout:
  // group 1  -> LEDs 1, 200, 201 on the 300-LED boss bar
  // group 2  -> LEDs 2, 199, 202
  // ...
  // group 100 -> LEDs 100, 101, 300
  const uint16_t row1Index = group0Based;
  const uint16_t row2Index = 2 * groups - 1 - group0Based;
  const uint16_t row3Index = 2 * groups + group0Based;

  hpBar_[row1Index] = color;
  hpBar_[row2Index] = color;
  hpBar_[row3Index] = color;
}

void BossTargetController::setHpBarAll(const CRGB& color) {
  for (uint16_t group = 0; group < hpGroupCount(); ++group) {
    setHpBarGroup(group, color);
  }
}

void BossTargetController::renderStartIntro(uint32_t now) {
  fill_solid(hpBar_, ::boss_target::MAX_HP_BAR_NUM_LEDS, CRGB::Black);
  const uint32_t elapsed = now - startIntroStartedMs_;
  const uint8_t baseHue = static_cast<uint8_t>((elapsed / ::boss_target::START_INTRO_HUE_STEP_MS) & 0xFF);

  const uint16_t ringCount = ringLedCount();
  for (uint8_t ring = 0; ring < ::boss_target::kMaxTargets; ++ring) {
    fill_solid(rings_[ring], ::boss_target::MAX_RING_NUM_LEDS, CRGB::Black);
    if (ring >= targetCount()) continue;
    const uint8_t orbit = static_cast<uint8_t>(baseHue * 3 + ring * 48);
    for (uint16_t led = 0; led < ringCount; ++led) {
      const uint8_t position = static_cast<uint8_t>((led * 255U) / ringCount);
      const uint8_t distance = circularDistance8(position, orbit);
      const uint8_t hue = static_cast<uint8_t>(baseHue * 2 + ring * 32 + position);
      rings_[ring][led] = CHSV(hue, 255, neonOrbitValue(distance, 42));
    }
  }

  const uint16_t groups = hpGroupCount();
  const uint8_t hpOrbitA = static_cast<uint8_t>(baseHue * 4);
  const uint8_t hpOrbitB = static_cast<uint8_t>(128 + baseHue * 4);
  for (uint16_t group = 0; group < groups; ++group) {
    const uint8_t position = static_cast<uint8_t>((group * 255U) / groups);
    const uint8_t distanceA = circularDistance8(position, hpOrbitA);
    const uint8_t distanceB = circularDistance8(position, hpOrbitB);
    const uint8_t distance = min(distanceA, distanceB);
    const uint8_t hue = static_cast<uint8_t>(baseHue * 2 + position);
    setHpBarGroup(group, CHSV(hue, 255, neonOrbitValue(distance, 55)));
  }
}

void BossTargetController::renderTargets(uint32_t now) {
  for (uint8_t i = 0; i < ::boss_target::kMaxTargets; ++i) fillRing(i, CRGB::Black);
  if (mode_ != Mode::ACTIVE) return;
  for (uint8_t i = 0; i < targetCount(); ++i) {
    if (targetFlashUntilMs_[i] != 0 && static_cast<int32_t>(targetFlashUntilMs_[i] - now) > 0) {
      const uint32_t remaining = targetFlashUntilMs_[i] - now;
      const uint32_t elapsed = ::boss_target::HIT_FLASH_MS - min<uint32_t>(remaining, ::boss_target::HIT_FLASH_MS);
      const bool flashOn = ((elapsed / ::boss_target::HIT_FLASH_BLINK_MS) % 2) == 0;
      if (flashOn) fillRing(i, colorFromRgb(config_.target.hitFlashColor));
    } else if (!targetTransitionPending_ && i == activeTarget_) {
      fillRing(i, colorFromRgb(config_.target.activeColor));
    }
  }
}

void BossTargetController::renderHpBar(uint32_t now) {
  fill_solid(hpBar_, ::boss_target::MAX_HP_BAR_NUM_LEDS, CRGB::Black);
  if (mode_ == Mode::INTRO || mode_ == Mode::READY || mode_ == Mode::UNCONFIGURED || otaPrepared_) return;
  if (mode_ == Mode::DEFEATED) {
    if (now - lastDeadBlinkMs_ >= config_.hpBar.deadBlinkMs) {
      lastDeadBlinkMs_ = now;
      deadBlinkOn_ = !deadBlinkOn_;
    }
    if (deadBlinkOn_) setHpBarAll(colorFromRgb(::boss_target::HP_RED));
    return;
  }
  const uint16_t lit = hpLitGroupCount();
  const uint8_t band = hpBandForValue(hpRemaining_);
  const CRGB base = hpColorForBand(band);
  for (uint16_t group = 0; group < hpGroupCount(); ++group) {
    if (group < lit) setHpBarGroup(group, base);
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
  if (mode_ == Mode::INTRO) {
    renderStartIntro(now);
    FastLED.show();
    return;
  }
  renderTargets(now);
  renderHpBar(now);
  FastLED.show();
}

void BossTargetController::pollPiezoAo(uint32_t now) {
  if (now - lastPiezoSampleMs_ < ::boss_target::PIEZO_AO_SAMPLE_PERIOD_MS) return;
  lastPiezoSampleMs_ = now;
  for (uint8_t i = 0; i < targetCount(); ++i) {
    const int8_t pin = config_.hardware.piezoDoPins[i];
    if (pin < 0) continue;
    const uint16_t raw = static_cast<uint16_t>(analogRead(pin));
    if (raw <= ::boss_target::PIEZO_AO_RELEASE) {
      piezoArmed_[i] = true;
    }
    if (!piezoArmed_[i] || raw < ::boss_target::PIEZO_AO_THRESHOLD) continue;
    piezoArmed_[i] = false;
    if (piezoEdgeCount_[i] < UINT16_MAX) piezoEdgeCount_[i]++;
    if (raw > piezoPeak_[i]) piezoPeak_[i] = raw;
  }
}

uint16_t BossTargetController::popPiezoEdges(uint8_t index) {
  if (index >= ::boss_target::kMaxTargets) return 0;
  uint16_t pending = piezoEdgeCount_[index];
  piezoEdgeCount_[index] = 0;
  return pending;
}

uint16_t BossTargetController::popPiezoPeak(uint8_t index) {
  if (index >= ::boss_target::kMaxTargets) return 0;
  uint16_t peak = piezoPeak_[index];
  piezoPeak_[index] = 0;
  return peak;
}

void BossTargetController::clearPiezoEdges() {
  for (uint8_t i = 0; i < ::boss_target::kMaxTargets; ++i) {
    piezoEdgeCount_[i] = 0;
    piezoPeak_[i] = 0;
    piezoArmed_[i] = true;
  }
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
  obj["start_intro_active"] = mode_ == Mode::INTRO;
  obj["target_transition_pending"] = targetTransitionPending_;
  const bool targetActiveForHits = vulnerableNow(now);
  obj["active_target_index"] = targetActiveForHits ? activeTarget_ : -1;
  obj["active_target_id"] = targetActiveForHits ? String("target_") + String(activeTarget_ + 1) : String("");
  obj["target_count"] = targetCount();
  obj["hardware_max_targets"] = ::boss_target::kMaxTargets;
  obj["target_duration_ms"] = config_.gameplay.targetDurationMs;
  obj["hit_cooldown_ms"] = config_.gameplay.hitCooldownMs;
  obj["digital_isr_debounce_us"] = config_.gameplay.digitalIsrDebounceUs;
  obj["piezo_ao_threshold"] = ::boss_target::PIEZO_AO_THRESHOLD;
  obj["piezo_ao_release"] = ::boss_target::PIEZO_AO_RELEASE;
  obj["last_hit_target_index"] = lastHitTargetIndex_;
  obj["last_wrong_target_index"] = lastWrongTargetIndex_;
  obj["last_event"] = lastEvent_;
  obj["ota_safe"] = isSafeForOta();

  JsonArray targets = obj.createNestedArray("targets");
  for (uint8_t i = 0; i < targetCount(); ++i) {
    JsonObject t = targets.createNestedObject();
    t["index"] = i;
    t["id"] = String("target_") + String(i + 1);
    t["active"] = targetActiveForHits && i == activeTarget_;
    t["hit_flash_active"] = targetTransitionPending_ && i == activeTarget_;
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
  s += targetTransitionPending_ ? '1' : '0';
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
