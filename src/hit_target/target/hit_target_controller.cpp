#include "hit_target_controller.h"

#include <esp_system.h>

#include "hit_target/app/firmware_info.h"
#include "hit_target/build_config.h"

namespace battlebang {
namespace hit_target {

HitTargetController* HitTargetController::isrInstance_ = nullptr;

namespace {
String statusString(JsonObjectConst status, const char* key) {
  const char* value = status[key] | "";
  String out(value);
  out.trim();
  return out;
}

String normalizedStatusString(JsonObjectConst status, const char* key) {
  String out = statusString(status, key);
  out.toUpperCase();
  return out;
}

bool statusBoolFlag(JsonObjectConst status, const char* key, bool& out) {
  JsonVariantConst value = status[key];
  if (value.isNull()) return false;
  out = value.as<bool>();
  return true;
}

bool activeTurretStatus(JsonObjectConst status) {
  bool explicitActive = false;
  if (statusBoolFlag(status, "activation_active", explicitActive) ||
      statusBoolFlag(status, "hit_target_active", explicitActive) ||
      statusBoolFlag(status, "vulnerable", explicitActive) ||
      statusBoolFlag(status, "active", explicitActive)) {
    return explicitActive;
  }

  const bool ready = status["ready_for_next_command"] | false;
  if (ready) return false;

  const String mode = normalizedStatusString(status, "mode");
  if (mode == "WAIT_COMMAND" || mode == "IDLE" || mode == "DEAD" || mode == "UNCONFIGURED") return false;
  if (mode == "PATTERN" || mode == "FIRING") return true;

  const String fireState = normalizedStatusString(status, "fire_state");
  if (fireState == "FIRING" || fireState == "ARMING" || fireState == "ACTIVE") return true;

  const String patternState = normalizedStatusString(status, "pattern_state");
  if (patternState.length() > 0 && patternState != "IDLE" && patternState != "DONE" && patternState != "COMPLETE" &&
      patternState != "FAILED") {
    return true;
  }
  return false;
}

bool activeGenericDeviceStatus(JsonObjectConst status) {
  bool flag = false;
  if (statusBoolFlag(status, "activation_active", flag) ||
      statusBoolFlag(status, "hit_target_active", flag) ||
      statusBoolFlag(status, "vulnerable", flag) ||
      statusBoolFlag(status, "active", flag)) {
    return flag;
  }

  const String lifeState = normalizedStatusString(status, "life_state");
  if (lifeState == "DEAD" || lifeState == "DOWN" || lifeState == "DISABLED") return false;

  const bool ready = status["ready_for_next_command"] | false;
  if (ready) return false;

  const String mode = normalizedStatusString(status, "mode");
  if (mode == "WAIT_COMMAND" || mode == "IDLE" || mode == "DEAD" || mode == "UNCONFIGURED" ||
      mode == "DISABLED") {
    return false;
  }
  if (mode == "ACTIVE" || mode == "ENGAGED" || mode == "BUSY" || mode == "MOVING" ||
      mode == "TARGET" || mode == "PATTERN" || mode == "FIRING" || mode == "HOME") {
    return true;
  }

  const String state = normalizedStatusString(status, "state");
  if (state == "ACTIVE" || state == "ENGAGED" || state == "BUSY" || state == "MOVING") return true;
  if (state == "IDLE" || state == "READY" || state == "DEAD" || state == "DISABLED") return false;

  const String commandState = normalizedStatusString(status, "command_state");
  if (commandState == "ACTIVE" || commandState == "RUNNING" || commandState == "IN_PROGRESS" ||
      commandState == "BUSY") {
    return true;
  }

  return false;
}

bool activeLinkedDeviceStatus(const String& kind, JsonObjectConst status) {
  String normalizedKind = kind;
  normalizedKind.trim();
  normalizedKind.toLowerCase();
  if (normalizedKind == "turret") return activeTurretStatus(status);
  return activeGenericDeviceStatus(status);
}
}  // namespace

void HitTargetController::begin(const RuntimeConfig& config, EventCallback callback, void* ctx) {
  callback_ = callback;
  callbackCtx_ = ctx;
  config_ = config;
  random16_add_entropy(static_cast<uint16_t>(esp_random()));

  FastLED.addLeds<BATTLEBANG_HIT_TARGET_LED_TYPE, ::hit_target::LED_PIN, BATTLEBANG_HIT_TARGET_COLOR_ORDER>(leds_, ::hit_target::NUM_LEDS);
  FastLED.setBrightness(config_.led.brightness);
  FastLED.setMaxPowerInVoltsAndMilliamps(::hit_target::LED_MAX_VOLTS, config_.led.maxMa);
  FastLED.clear(true);

  RuntimeConfig empty = config_;
  empty.sensor.piezoDoPin = -1;
  empty.sensor.piezoAoPin = -1;
  empty.reset.buttonPin = -1;
  configurePins(empty, config_);
  clearRuntimeState();
  pinsConfigured_ = true;
}

void HitTargetController::applyConfig(const RuntimeConfig& config, bool resetState, const char* source) {
  const RuntimeConfig previous = config_;
  config_ = config;
  frameRendered_ = false;
  FastLED.setBrightness(config_.led.brightness);
  FastLED.setMaxPowerInVoltsAndMilliamps(::hit_target::LED_MAX_VOLTS, config_.led.maxMa);
  if (sensorPinsChanged(previous, config_)) {
    configurePins(previous, config_);
    clearPiezoDoFlag();
  }
  if (previous.activation.mode != config_.activation.mode ||
      previous.activation.linkedDeviceKind != config_.activation.linkedDeviceKind ||
      previous.activation.linkedDeviceId != config_.activation.linkedDeviceId) {
    linkedDevice_ = LinkedDeviceState{};
  }
  if (resetState) {
    reset(source);
  }
}

void HitTargetController::configurePins(const RuntimeConfig& previous, const RuntimeConfig& next) {
  if (previous.sensor.piezoDoPin >= 0 && previous.sensor.piezoDoPin != next.sensor.piezoDoPin) {
    detachInterrupt(digitalPinToInterrupt(previous.sensor.piezoDoPin));
  }
  isrInstance_ = this;
  if (next.sensor.piezoAoPin >= 0 && previous.sensor.piezoAoPin != next.sensor.piezoAoPin) {
    analogReadResolution(12);
    analogSetPinAttenuation(next.sensor.piezoAoPin, ADC_11db);
  }
  if (next.sensor.piezoDoPin >= 0 && previous.sensor.piezoDoPin != next.sensor.piezoDoPin) {
    pinMode(next.sensor.piezoDoPin, INPUT_PULLDOWN);
    attachInterrupt(digitalPinToInterrupt(next.sensor.piezoDoPin), piezoDoIsrStatic, RISING);
  }
  if (next.reset.buttonPin >= 0 && previous.reset.buttonPin != next.reset.buttonPin) {
    pinMode(next.reset.buttonPin, INPUT_PULLUP);
  }
}

void HitTargetController::clearRuntimeState() {
  target_.sequence = 0;
  target_.hpRemaining = maxHits();
  target_.damaged = false;
  target_.destroyed = false;
  frameRendered_ = false;
  lastFrameSignature_ = 0;
  capture_ = CaptureState{};
  resetSensorRuntime();
  resetButton_ = ResetButtonState{};
  resetEffects();
  resetDamageChip();
  wasVulnerable_ = false;
  clearPiezoDoFlag();
}

void HitTargetController::resetSensorRuntime() {
  sensor_.armed = true;
  sensor_.quietStartedMs = 0;
  sensor_.lastRearmCheckMs = 0;
}

void HitTargetController::resetEffects() {
  timers_.lockoutUntilMs = 0;
  timers_.defeatStartedMs = 0;
  timers_.defeatUntilMs = 0;
  frameRendered_ = false;
  lastFrameSignature_ = 0;
}

void HitTargetController::resetDamageChip() {
  damageChip_ = DamageChipState{};
}

void HitTargetController::releaseResetButton() {
  resetButton_ = ResetButtonState{};
}

void HitTargetController::reset(const char* source) {
  clearRuntimeState();
  emit("reset", 0, source);
}

void HitTargetController::simulateHit(const char* source) {
  const uint32_t now = millis();
  if (!vulnerableNow(now)) return;
  registerHit(now, readPiezoAnalog(), source);
}

void HitTargetController::loop(uint32_t now) {
  pollResetButton(now);
  const bool vulnerable = vulnerableNow(now);
  if (vulnerable) {
    wasVulnerable_ = true;
    pollPiezo(now);
  } else {
    if (wasVulnerable_ || capture_.active || !sensor_.armed) {
      capture_ = CaptureState{};
      resetSensorRuntime();
      clearPiezoDoFlag();
    }
    wasVulnerable_ = false;
  }
  renderLeds(now);
}

void HitTargetController::prepareForOta() {
  hitEnabled_ = false;
  resetEffects();
  capture_ = CaptureState{};
  resetSensorRuntime();
  fill_solid(leds_, ::hit_target::NUM_LEDS, CRGB::Black);
  FastLED.show();
  frameRendered_ = true;
  lastFrameSignature_ = renderFrameSignature(millis());
}

bool HitTargetController::isSafeForOta() const {
  return !capture_.active;
}

bool HitTargetController::linkedDeviceStatusFresh(uint32_t now) const {
  if (linkedDevice_.lastStatusMs == 0) return false;
  return now - linkedDevice_.lastStatusMs <= config_.activation.staleMs;
}

bool HitTargetController::vulnerableNow(uint32_t now) const {
  if (!hitEnabled_ || target_.destroyed || target_.hpRemaining <= 0) return false;
  if (config_.activation.mode != "linked_device") return true;
  return linkedDeviceStatusFresh(now) && linkedDevice_.active;
}

bool HitTargetController::applyLinkedDeviceStatus(JsonObjectConst status, uint32_t now) {
  if (config_.activation.mode != "linked_device") return false;

  String linkedDeviceKind = config_.activation.linkedDeviceKind;
  linkedDeviceKind.trim();
  linkedDeviceKind.toLowerCase();

  String incomingDeviceId;
  if (linkedDeviceKind == "turret") {
    incomingDeviceId = statusString(status, "turret_id");
    if (incomingDeviceId.length() == 0) incomingDeviceId = statusString(status, "device_id");
  } else {
    incomingDeviceId = statusString(status, "device_id");
  }
  if (incomingDeviceId.length() == 0) incomingDeviceId = statusString(status, "id");
  if (incomingDeviceId.length() > 0 && incomingDeviceId != config_.activation.linkedDeviceId) {
    Serial.print("[hit_target][activation] ignoring status for linked_device_id=");
    Serial.println(incomingDeviceId);
    return false;
  }

  LinkedDeviceState previous = linkedDevice_;
  linkedDevice_.lastStatusMs = now;
  linkedDevice_.mode = statusString(status, "mode");
  linkedDevice_.commandState = statusString(status, "command_state");
  linkedDevice_.fireState = statusString(status, "fire_state");
  linkedDevice_.patternState = statusString(status, "pattern_state");
  linkedDevice_.activeCommandId = statusString(status, "active_command_id");
  linkedDevice_.readyForNextCommand = status["ready_for_next_command"] | false;
  linkedDevice_.active = activeLinkedDeviceStatus(config_.activation.linkedDeviceKind, status);

  return previous.active != linkedDevice_.active ||
         previous.readyForNextCommand != linkedDevice_.readyForNextCommand ||
         previous.mode != linkedDevice_.mode ||
         previous.commandState != linkedDevice_.commandState ||
         previous.fireState != linkedDevice_.fireState ||
         previous.patternState != linkedDevice_.patternState ||
         previous.activeCommandId != linkedDevice_.activeCommandId ||
         previous.lastStatusMs == 0;
}

int HitTargetController::ledCount() const {
  return constrain(static_cast<int>(config_.led.numLeds), 1, ::hit_target::NUM_LEDS);
}

int HitTargetController::maxHits() const {
  return totalHits(config_);
}

int HitTargetController::phaseIndexForHp(int hp) const {
  hp = constrain(hp, 0, maxHits());
  if (hp <= 0) return activePhaseCount(config_) - 1;
  int hitsConsumed = maxHits() - hp;
  int phaseIndex = hitsConsumed / config_.hp.hitsPerPhase;
  return constrain(phaseIndex, 0, static_cast<int>(activePhaseCount(config_) - 1));
}

int HitTargetController::phaseHitsRemaining(int hp) const {
  hp = constrain(hp, 0, maxHits());
  if (hp <= 0) return 0;
  int hitsConsumed = maxHits() - hp;
  int consumedInPhase = hitsConsumed % config_.hp.hitsPerPhase;
  return config_.hp.hitsPerPhase - consumedInPhase;
}

int HitTargetController::phaseLitCount(int hp) const {
  int hitsRemaining = phaseHitsRemaining(hp);
  return static_cast<int>(((long)hitsRemaining * ledCount() + config_.hp.hitsPerPhase - 1) / config_.hp.hitsPerPhase);
}

CRGB HitTargetController::phaseColor(int phaseIndex) const {
  uint32_t rgb = phaseColorRgb(config_, static_cast<uint8_t>(phaseIndex));
  return CRGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

CRGB HitTargetController::addBlend(const CRGB& base, const CRGB& overlay) const {
  return CRGB(qadd8(base.r, overlay.r), qadd8(base.g, overlay.g), qadd8(base.b, overlay.b));
}

CRGB HitTargetController::scaleColor(const CRGB& color, uint8_t scale) const {
  return CRGB(scale8(color.r, scale), scale8(color.g, scale), scale8(color.b, scale));
}

bool HitTargetController::isLockedOut(uint32_t now) const {
  return (int32_t)(timers_.lockoutUntilMs - now) > 0;
}

bool HitTargetController::resetButtonPressed() const {
  return config_.reset.buttonPin >= 0 && digitalRead(config_.reset.buttonPin) == LOW;
}

bool HitTargetController::phaseRevealPending(uint32_t now) const {
  return damageChip_.phaseTransition && damageVisible(now);
}

bool HitTargetController::damageVisible(uint32_t now) const {
  return damageChip_.endLed > damageChip_.firstLed && (int32_t)(damageChip_.visibleUntilMs - now) > 0;
}

bool HitTargetController::renderFrameAnimated(uint32_t now) const {
  if (damageVisible(now) || phaseRevealPending(now)) return true;
  if (target_.hpRemaining <= 0 && (int32_t)(timers_.defeatStartedMs - now) <= 0 &&
      (int32_t)(timers_.defeatUntilMs - now) > 0) {
    return true;
  }
  return false;
}

uint32_t HitTargetController::renderFrameSignature(uint32_t now) const {
  uint32_t sig = static_cast<uint32_t>(constrain(target_.hpRemaining, 0, maxHits()));
  sig |= (static_cast<uint32_t>(phaseIndexForHp(target_.hpRemaining)) & 0x0F) << 8;
  sig |= (static_cast<uint32_t>(phaseLitCount(target_.hpRemaining)) & 0xFF) << 12;
  if (target_.destroyed) sig |= 1UL << 20;
  if (vulnerableNow(now)) sig |= 1UL << 21;
  if (hitEnabled_) sig |= 1UL << 22;
  if (damageVisible(now)) sig |= 1UL << 23;
  if (phaseRevealPending(now)) sig |= 1UL << 24;
  if (target_.hpRemaining <= 0 && (int32_t)(timers_.defeatStartedMs - now) > 0) sig |= 1UL << 25;
  if (target_.hpRemaining <= 0 && (int32_t)(timers_.defeatUntilMs - now) > 0) sig |= 1UL << 26;
  return sig;
}

int HitTargetController::damageLength() const {
  return max(0, damageChip_.endLed - damageChip_.firstLed);
}

int HitTargetController::damageVisibleCount(uint32_t now) const {
  if (!damageVisible(now)) return 0;
  int total = damageLength();
  if (total <= 0) return 0;
  uint32_t elapsed = now > damageChip_.startedMs ? now - damageChip_.startedMs : 0;
  if (elapsed > config_.visual.damageChipMs) elapsed = config_.visual.damageChipMs;
  uint32_t progress = elapsed * static_cast<uint32_t>(total);
  int expired = static_cast<int>(progress / config_.visual.damageChipMs);
  return constrain(total - expired, 0, total);
}

int HitTargetController::damageExpiredCount(uint32_t now) const {
  return damageLength() - damageVisibleCount(now);
}

void HitTargetController::captureDamageChip(int previousHp, int currentHp, uint32_t now) {
  damageChip_.previousPhaseIndex = phaseIndexForHp(previousHp);
  int currentPhaseIndex = phaseIndexForHp(currentHp);
  damageChip_.phaseTransition = currentHp > 0 && currentPhaseIndex != damageChip_.previousPhaseIndex;
  if (damageChip_.phaseTransition) {
    damageChip_.firstLed = 0;
    damageChip_.endLed = phaseLitCount(previousHp);
  } else {
    damageChip_.firstLed = phaseLitCount(currentHp);
    damageChip_.endLed = phaseLitCount(previousHp);
  }
  if (damageChip_.endLed <= damageChip_.firstLed && damageChip_.endLed > 0) {
    damageChip_.firstLed = damageChip_.endLed - 1;
  }
  damageChip_.startedMs = now;
  damageChip_.visibleUntilMs = now + config_.visual.damageChipMs;
}

void HitTargetController::clearLayer(CRGB* layer) {
  fill_solid(layer, ::hit_target::NUM_LEDS, CRGB::Black);
}

void HitTargetController::renderPhaseBackfill(int phaseIndex, int lit) {
  const int leds = ledCount();
  if (phaseIndex >= static_cast<int>(activePhaseCount(config_) - 1)) return;
  if (lit >= leds) return;
  int gap = constrain(static_cast<int>(config_.visual.phaseBackfillGapLeds), 0, leds / 2);
  int start = constrain(lit + gap, 0, leds);
  int end = constrain(leds - gap, 0, leds);
  if (start >= end) return;
  CRGB backfill = scaleColor(phaseColor(phaseIndex + 1), config_.visual.phaseBackfillScale);
  for (int i = start; i < end; i++) hpLayer_[i] = backfill;
}

void HitTargetController::renderPhaseTransitionReveal(uint32_t now) {
  if (!damageChip_.phaseTransition) return;
  int expired = damageExpiredCount(now);
  if (expired <= 0) return;
  int start = constrain(damageChip_.endLed - expired, damageChip_.firstLed, damageChip_.endLed);
  int end = damageChip_.endLed;
  CRGB nextColor = phaseColor(damageChip_.previousPhaseIndex + 1);
  for (int i = start; i < end; i++) {
    if (i < 0 || i >= ledCount()) continue;
    hpLayer_[i] = nextColor;
  }
}

void HitTargetController::renderHpLayer(uint32_t now) {
  clearLayer(hpLayer_);
  if (phaseRevealPending(now)) {
    renderPhaseBackfill(damageChip_.previousPhaseIndex, damageChip_.endLed);
    renderPhaseTransitionReveal(now);
    return;
  }
  int lit = phaseLitCount(target_.hpRemaining);
  int phaseIndex = phaseIndexForHp(target_.hpRemaining);
  renderPhaseBackfill(phaseIndex, lit);
  if (lit <= 0) return;
  CRGB color = phaseColor(phaseIndex);
  for (int i = 0; i < lit && i < ledCount(); i++) hpLayer_[i] = color;
}

void HitTargetController::renderDamageLayer(uint32_t now) {
  clearLayer(damageLayer_);
  if (!damageVisible(now)) return;
  int visibleCount = damageVisibleCount(now);
  if (visibleCount <= 0) return;
  uint8_t edgeFade = 255;
  int total = damageLength();
  uint32_t elapsed = now > damageChip_.startedMs ? now - damageChip_.startedMs : 0;
  if (elapsed > config_.visual.damageChipMs) elapsed = config_.visual.damageChipMs;
  uint32_t progress = elapsed * static_cast<uint32_t>(total);
  uint32_t edgePhase = progress % config_.visual.damageChipMs;
  if (edgePhase > 0) edgeFade = 255 - static_cast<uint8_t>((edgePhase * 255) / config_.visual.damageChipMs);
  for (int offset = 0; offset < visibleCount; offset++) {
    int i = damageChip_.firstLed + offset;
    if (i < 0 || i >= ledCount()) continue;
    uint8_t intensity = 230;
    if (offset == visibleCount - 1) intensity = scale8(intensity, edgeFade);
    damageLayer_[i] = CRGB(intensity, scale8(intensity, 70), 0);
  }
}

void HitTargetController::renderDefeatRainbow(uint32_t now) {
  const int leds = ledCount();
  uint32_t elapsed = now > timers_.defeatStartedMs ? now - timers_.defeatStartedMs : 0;
  if (elapsed > config_.visual.defeatRainbowMs) elapsed = config_.visual.defeatRainbowMs;
  uint32_t remaining = config_.visual.defeatRainbowMs - elapsed;
  uint32_t fadeWindow = min<uint32_t>(260, config_.visual.defeatRainbowMs);
  uint8_t fade = 255;
  if (remaining < fadeWindow) fade = static_cast<uint8_t>((remaining * 255UL) / fadeWindow);
  uint8_t phase = static_cast<uint8_t>((elapsed * 255UL * config_.visual.defeatRainbowSpins) / config_.visual.defeatRainbowMs);
  for (int i = 0; i < leds; i++) {
    uint8_t hue = phase + static_cast<uint8_t>((i * 255UL) / leds);
    leds_[i] = CHSV(hue, 255, scale8(230, fade));
  }
  for (int i = leds; i < ::hit_target::NUM_LEDS; ++i) leds_[i] = CRGB::Black;
}

void HitTargetController::renderLeds(uint32_t now) {
  if (now - timers_.lastShowMs < ::hit_target::LED_SHOW_PERIOD_MS) return;
  timers_.lastShowMs = now;
  const bool animatedFrame = renderFrameAnimated(now);
  const uint32_t frameSignature = renderFrameSignature(now);
  if (!animatedFrame && frameRendered_ && frameSignature == lastFrameSignature_) return;
  const int leds = ledCount();
  if (target_.hpRemaining <= 0) {
    if (damageVisible(now) && (int32_t)(timers_.defeatStartedMs - now) > 0) {
      renderDamageLayer(now);
      for (int i = 0; i < leds; i++) leds_[i] = damageLayer_[i];
      for (int i = leds; i < ::hit_target::NUM_LEDS; ++i) leds_[i] = CRGB::Black;
    } else if ((int32_t)(timers_.defeatStartedMs - now) > 0) {
      fill_solid(leds_, ::hit_target::NUM_LEDS, CRGB::Black);
    } else if ((int32_t)(timers_.defeatUntilMs - now) > 0) {
      renderDefeatRainbow(now);
    } else {
      fill_solid(leds_, ::hit_target::NUM_LEDS, CRGB::Black);
    }
    FastLED.show();
    frameRendered_ = true;
    lastFrameSignature_ = frameSignature;
    return;
  }
  if (!vulnerableNow(now)) {
    fill_solid(leds_, ::hit_target::NUM_LEDS, CRGB::Black);
    FastLED.show();
    frameRendered_ = true;
    lastFrameSignature_ = frameSignature;
    return;
  }
  renderHpLayer(now);
  renderDamageLayer(now);
  for (int i = 0; i < leds; i++) leds_[i] = addBlend(hpLayer_[i], damageLayer_[i]);
  for (int i = leds; i < ::hit_target::NUM_LEDS; ++i) leds_[i] = CRGB::Black;
  FastLED.show();
  frameRendered_ = true;
  lastFrameSignature_ = frameSignature;
}

void IRAM_ATTR HitTargetController::piezoDoIsrStatic() {
  if (isrInstance_ != nullptr) isrInstance_->onPiezoDoIsr();
}

void IRAM_ATTR HitTargetController::onPiezoDoIsr() {
  uint32_t nowUs = micros();
  if (nowUs - sensor_.lastIsrUs < config_.sensor.digitalIsrDebounceUs) return;
  sensor_.lastIsrUs = nowUs;
  if (sensor_.digitalEdgeCount < UINT16_MAX) sensor_.digitalEdgeCount++;
}

uint16_t HitTargetController::popPiezoDoEdges() {
  noInterrupts();
  uint16_t pending = sensor_.digitalEdgeCount;
  sensor_.digitalEdgeCount = 0;
  interrupts();
  return pending;
}

void HitTargetController::clearPiezoDoFlag() {
  noInterrupts();
  sensor_.digitalEdgeCount = 0;
  interrupts();
}

uint16_t HitTargetController::readPiezoAnalog() const {
  if (config_.sensor.piezoAoPin < 0) return 0;
  int value = analogRead(config_.sensor.piezoAoPin);
  if (value < 0) return 0;
  if (value > 4095) return 4095;
  return static_cast<uint16_t>(value);
}

void HitTargetController::startCapture(uint32_t now, bool fromDigital, uint16_t initialPeak, uint16_t initialDigitalEdges) {
  capture_.active = true;
  capture_.startedMs = now;
  capture_.peak = initialPeak;
  capture_.digitalEdges = initialDigitalEdges;
  capture_.fromDigital = fromDigital;
}

void HitTargetController::finishCaptureIfDue(uint32_t now) {
  if (!capture_.active) return;
  uint16_t value = readPiezoAnalog();
  if (value > capture_.peak) capture_.peak = value;
  if (now - capture_.startedMs < config_.sensor.captureWindowMs) return;
  uint16_t digitalEdges = capture_.digitalEdges;
  bool hit = capture_.fromDigital && digitalEdges >= config_.sensor.digitalHitMinEdges;
  if (config_.sensor.piezoAoPin >= 0 && capture_.peak >= config_.sensor.hitThreshold) hit = true;
  uint16_t peak = capture_.peak;
  capture_ = CaptureState{};
  if (hit) registerHit(now, peak, "piezo", digitalEdges);
}

bool HitTargetController::piezoQuiet() const {
  if (config_.sensor.piezoAoPin >= 0) return readPiezoAnalog() < config_.sensor.hitRearmThreshold;
  if (config_.sensor.piezoDoPin >= 0) return digitalRead(config_.sensor.piezoDoPin) == LOW;
  return true;
}

void HitTargetController::updateSensorRearm(uint32_t now) {
  if (sensor_.armed) return;
  if (isLockedOut(now)) return;
  if (now - sensor_.lastRearmCheckMs < config_.sensor.hitRearmCheckMs) return;
  sensor_.lastRearmCheckMs = now;
  if (!piezoQuiet()) {
    sensor_.quietStartedMs = 0;
    clearPiezoDoFlag();
    return;
  }
  if (sensor_.quietStartedMs == 0) {
    sensor_.quietStartedMs = now;
    clearPiezoDoFlag();
    return;
  }
  if (now - sensor_.quietStartedMs >= config_.sensor.hitRearmStableMs) {
    sensor_.armed = true;
    sensor_.quietStartedMs = 0;
    clearPiezoDoFlag();
  }
}

void HitTargetController::pollPiezo(uint32_t now) {
  uint16_t digitalEdges = popPiezoDoEdges();
  if (capture_.active && digitalEdges > 0) {
    uint32_t totalEdges = static_cast<uint32_t>(capture_.digitalEdges) + digitalEdges;
    capture_.digitalEdges = totalEdges > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(totalEdges);
  }
  finishCaptureIfDue(now);
  updateSensorRearm(now);
  uint16_t analogValue = readPiezoAnalog();
  bool digitalTriggered = digitalEdges > 0;
  bool analogTriggered = config_.sensor.piezoAoPin >= 0 && analogValue >= config_.sensor.hitThreshold;
  if (target_.destroyed || capture_.active) return;
  if (!sensor_.armed) return;
  if (isLockedOut(now)) return;
  if (!digitalTriggered && !analogTriggered) return;
  startCapture(now, digitalTriggered, analogValue, digitalEdges);
}

void HitTargetController::pollResetButton(uint32_t now) {
  if (config_.reset.buttonPin < 0) return;
  if (!resetButtonPressed()) {
    releaseResetButton();
    return;
  }
  if (!resetButton_.pressed) {
    resetButton_.pressed = true;
    resetButton_.pressedSinceMs = now;
    return;
  }
  if (resetButton_.consumed) return;
  if (now - resetButton_.pressedSinceMs < config_.reset.buttonHoldMs) return;
  resetButton_.consumed = true;
  reset("button");
}

void HitTargetController::registerHit(uint32_t now, uint16_t peak, const char* source, uint16_t digitalEdges) {
  if (target_.hpRemaining <= 0) return;
  if (!vulnerableNow(now)) return;
  if (isLockedOut(now)) return;
  target_.damaged = true;
  target_.sequence++;
  int previousHp = target_.hpRemaining;
  target_.hpRemaining--;
  captureDamageChip(previousHp, target_.hpRemaining, now);
  if (peak == 0 && config_.sensor.piezoAoPin < 0) peak = config_.sensor.hitThreshold;
  timers_.lockoutUntilMs = now + config_.sensor.hitCooldownMs;
  sensor_.armed = false;
  sensor_.quietStartedMs = 0;
  sensor_.lastRearmCheckMs = now;
  clearPiezoDoFlag();
  if (target_.hpRemaining <= 0) {
    target_.hpRemaining = 0;
    target_.destroyed = true;
    timers_.defeatStartedMs = damageChip_.visibleUntilMs + config_.visual.defeatBlackoutMs;
    timers_.defeatUntilMs = timers_.defeatStartedMs + config_.visual.defeatRainbowMs;
    emit("destroyed", peak, source, digitalEdges);
  } else {
    emit("hit", peak, source, digitalEdges);
  }
}

void HitTargetController::emit(const char* name, uint16_t peak, const char* source, uint16_t digitalEdges) {
  if (callback_ == nullptr) return;
  HitTargetEvent event;
  event.name = name;
  event.source = source;
  event.peak = peak;
  event.digitalEdges = digitalEdges;
  callback_(event, callbackCtx_);
}

void HitTargetController::appendStatus(JsonObject obj) const {
  const uint32_t now = millis();
  const int hpPhase = target_.hpRemaining > 0 ? phaseIndexForHp(target_.hpRemaining) + 1 : activePhaseCount(config_);
  obj["target_id"] = config_.targetId;
  obj["device_id"] = config_.deviceId;
  obj["device_mac"] = config_.deviceMac;
  obj["configured"] = config_.configured;
  obj["config_version"] = config_.configVersion;
  obj["sequence"] = target_.sequence;
  obj["hp_remaining"] = target_.hpRemaining;
  obj["max_hits"] = maxHits();
  obj["hp_phase"] = hpPhase;
  obj["hp_phase_count"] = activePhaseCount(config_);
  obj["hits_per_phase"] = config_.hp.hitsPerPhase;
  obj["phase_hits_remaining"] = phaseHitsRemaining(target_.hpRemaining);
  obj["phase_transition"] = damageChip_.phaseTransition;
  obj["phase_backfill_gap_leds"] = config_.visual.phaseBackfillGapLeds;
  obj["phase_backfill_scale"] = config_.visual.phaseBackfillScale;
  obj["digital_hit_min_edges"] = config_.sensor.digitalHitMinEdges;
  obj["digital_isr_debounce_us"] = config_.sensor.digitalIsrDebounceUs;
  obj["threshold"] = config_.sensor.hitThreshold;
  obj["cooldown_ms"] = config_.sensor.hitCooldownMs;
  obj["damage_chip_ms"] = config_.visual.damageChipMs;
  obj["defeat_blackout_ms"] = config_.visual.defeatBlackoutMs;
  obj["defeat_rainbow_ms"] = config_.visual.defeatRainbowMs;
  obj["defeat_rainbow_spins"] = config_.visual.defeatRainbowSpins;
  obj["rearm_stable_ms"] = config_.sensor.hitRearmStableMs;
  obj["armed"] = sensor_.armed;
  obj["capture"] = capture_.active;
  obj["damaged"] = target_.damaged;
  obj["destroyed"] = target_.destroyed;
  obj["hit_enabled"] = hitEnabled_;
  obj["vulnerable"] = vulnerableNow(now);
  obj["activation_mode"] = config_.activation.mode;
  obj["linked_device_kind"] = config_.activation.linkedDeviceKind;
  obj["linked_device_id"] = config_.activation.linkedDeviceId;
  obj["activation_stale_ms"] = config_.activation.staleMs;
  obj["linked_device_status_fresh"] = linkedDeviceStatusFresh(now);
  obj["linked_device_active"] = linkedDevice_.active;
  obj["linked_device_ready_for_next_command"] = linkedDevice_.readyForNextCommand;
  obj["linked_device_mode"] = linkedDevice_.mode;
  obj["linked_device_command_state"] = linkedDevice_.commandState;
  obj["linked_device_fire_state"] = linkedDevice_.fireState;
  obj["linked_device_pattern_state"] = linkedDevice_.patternState;
  obj["linked_device_active_command_id"] = linkedDevice_.activeCommandId;
  obj["linked_device_age_ms"] = linkedDevice_.lastStatusMs == 0 ? 0 : now - linkedDevice_.lastStatusMs;
  if (config_.activation.linkedDeviceKind == "turret") {
    obj["linked_turret_id"] = config_.activation.linkedDeviceId;
    obj["linked_turret_status_fresh"] = linkedDeviceStatusFresh(now);
    obj["linked_turret_active"] = linkedDevice_.active;
    obj["linked_turret_ready_for_next_command"] = linkedDevice_.readyForNextCommand;
    obj["linked_turret_mode"] = linkedDevice_.mode;
    obj["linked_turret_command_state"] = linkedDevice_.commandState;
    obj["linked_turret_fire_state"] = linkedDevice_.fireState;
    obj["linked_turret_pattern_state"] = linkedDevice_.patternState;
    obj["linked_turret_active_command_id"] = linkedDevice_.activeCommandId;
    obj["linked_turret_age_ms"] = linkedDevice_.lastStatusMs == 0 ? 0 : now - linkedDevice_.lastStatusMs;
  }
  obj["analog"] = readPiezoAnalog();
}

String HitTargetController::statusSignature() const {
  const uint32_t now = millis();
  return String(target_.sequence) + ":" + target_.hpRemaining + ":" + (target_.destroyed ? "1" : "0") + ":" +
         (hitEnabled_ ? "1" : "0") + ":" + (vulnerableNow(now) ? "1" : "0") + ":" +
         (linkedDevice_.active ? "1" : "0") + ":" + (linkedDeviceStatusFresh(now) ? "1" : "0");
}

void HitTargetController::printBootBanner() const {
  Serial.print("[");
  Serial.print(BB_HIT_TARGET_APP_NAME);
  Serial.print("] hardware=");
  Serial.print(BB_HIT_TARGET_HARDWARE);
  Serial.print(" version=");
  Serial.print(BB_HIT_TARGET_VERSION);
  Serial.print(" build=");
  Serial.print(BB_HIT_TARGET_BUILD);
  Serial.print(" target_id=");
  Serial.print(config_.targetId);
  Serial.print(" device_id=");
  Serial.print(config_.deviceId);
  Serial.print(" device_mac=");
  Serial.print(config_.deviceMac);
  Serial.print(" max_hits=");
  Serial.print(maxHits());
  Serial.print(" hp_phase_count=");
  Serial.print(activePhaseCount(config_));
  Serial.print(" hits_per_phase=");
  Serial.print(config_.hp.hitsPerPhase);
  Serial.print(" threshold=");
  Serial.print(config_.sensor.hitThreshold);
  Serial.print(" rearm_threshold=");
  Serial.print(config_.sensor.hitRearmThreshold);
  Serial.print(" cooldown_ms=");
  Serial.print(config_.sensor.hitCooldownMs);
  Serial.print(" activation_mode=");
  Serial.print(config_.activation.mode);
  Serial.print(" linked_device_kind=");
  Serial.print(config_.activation.linkedDeviceKind);
  Serial.print(" linked_device_id=");
  Serial.print(config_.activation.linkedDeviceId);
  Serial.print(" activation_stale_ms=");
  Serial.print(config_.activation.staleMs);
  Serial.print(" damage_chip_ms=");
  Serial.print(config_.visual.damageChipMs);
  Serial.print(" defeat_rainbow_ms=");
  Serial.println(config_.visual.defeatRainbowMs);
  Serial.print("[PIN] LED=");
  Serial.print(::hit_target::LED_PIN);
  Serial.print(" NUM_LEDS=");
  Serial.print(config_.led.numLeds);
  Serial.print(" capacity=");
  Serial.print(::hit_target::NUM_LEDS);
  Serial.print(" led_type=");
  Serial.print(config_.led.ledType);
  Serial.print(" color_order=");
  Serial.print(config_.led.colorOrder);
  Serial.print(" brightness=");
  Serial.print(config_.led.brightness);
  Serial.print(" max_ma=");
  Serial.print(config_.led.maxMa);
  Serial.print(" piezo_do=");
  Serial.print(config_.sensor.piezoDoPin);
  Serial.print(" piezo_ao=");
  Serial.print(config_.sensor.piezoAoPin);
  Serial.print(" reset_button=");
  Serial.print(config_.reset.buttonPin);
  Serial.print(" reset_hold_ms=");
  Serial.println(config_.reset.buttonHoldMs);
}

}  // namespace hit_target
}  // namespace battlebang
