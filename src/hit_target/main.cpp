#include <Arduino.h>
#include <FastLED.h>

#include "hit_target/build_config.h"

using namespace hit_target;

namespace {

CRGB leds[NUM_LEDS];
CRGB hpLayer[NUM_LEDS];
CRGB damageLayer[NUM_LEDS];
CRGB orbitLayer[NUM_LEDS];

struct DeviceIdentity {
  char targetId[32] = {0};
  char mac[18] = {0};

  void begin() {
    const uint64_t efuseMac = ESP.getEfuseMac();
    // ESP.getEfuseMac() stores the canonical ESP MAC with the first printed
    // octet in the least-significant byte. Keep this order aligned with esptool's
    // "MAC: AA:BB:CC:DD:EE:FF" output so target_id matches the board label/logs.
    const uint8_t macBytes[6] = {
        static_cast<uint8_t>(efuseMac & 0xFF),
        static_cast<uint8_t>((efuseMac >> 8) & 0xFF),
        static_cast<uint8_t>((efuseMac >> 16) & 0xFF),
        static_cast<uint8_t>((efuseMac >> 24) & 0xFF),
        static_cast<uint8_t>((efuseMac >> 32) & 0xFF),
        static_cast<uint8_t>((efuseMac >> 40) & 0xFF),
    };

    snprintf(mac,
             sizeof(mac),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             macBytes[0],
             macBytes[1],
             macBytes[2],
             macBytes[3],
             macBytes[4],
             macBytes[5]);
    snprintf(targetId,
             sizeof(targetId),
             "%s_%02X%02X%02X%02X%02X%02X",
             TARGET_ID_PREFIX,
             macBytes[0],
             macBytes[1],
             macBytes[2],
             macBytes[3],
             macBytes[4],
             macBytes[5]);
  }
};

struct TargetState {
  uint32_t sequence = 0;
  int hpRemaining = MAX_HITS;
  bool damaged = false;
  bool destroyed = false;

  void reset() {
    hpRemaining = MAX_HITS;
    damaged = false;
    destroyed = false;
  }
};

struct CaptureState {
  bool active = false;
  uint32_t startedMs = 0;
  uint16_t peak = 0;
  uint16_t digitalEdges = 0;
  bool fromDigital = false;

  void reset() {
    active = false;
    startedMs = 0;
    peak = 0;
    digitalEdges = 0;
    fromDigital = false;
  }
};

struct SensorState {
  volatile uint16_t digitalEdgeCount = 0;
  volatile uint32_t lastIsrUs = 0;
  bool armed = true;
  uint32_t quietStartedMs = 0;
  uint32_t lastRearmCheckMs = 0;

  void resetRuntime() {
    armed = true;
    quietStartedMs = 0;
    lastRearmCheckMs = 0;
  }
};

struct ResetButtonState {
  bool pressed = false;
  bool consumed = false;
  uint32_t pressedSinceMs = 0;

  void release() {
    pressed = false;
    consumed = false;
    pressedSinceMs = 0;
  }
};

struct EffectTimers {
  uint32_t lockoutUntilMs = 0;
  uint32_t hitFlashUntilMs = 0;
  uint32_t hpPulseUntilMs = 0;
  uint32_t defeatStartedMs = 0;
  uint32_t defeatUntilMs = 0;
  uint32_t lastShowMs = 0;
  uint32_t lastStatusMs = 0;

  void resetEffects() {
    lockoutUntilMs = 0;
    hitFlashUntilMs = 0;
    hpPulseUntilMs = 0;
    defeatStartedMs = 0;
    defeatUntilMs = 0;
  }
};

int phaseIndexForHp(int hp);
int phaseHitsRemaining(int hp);
int phaseLitCount(int hp);

struct DamageChipState {
  uint32_t startedMs = 0;
  uint32_t visibleUntilMs = 0;
  int firstLed = 0;
  int endLed = 0;
  bool phaseTransition = false;
  int previousPhaseIndex = 0;

  void reset() {
    startedMs = 0;
    visibleUntilMs = 0;
    firstLed = 0;
    endLed = 0;
    phaseTransition = false;
    previousPhaseIndex = 0;
  }

  void capture(int previousHp, int currentHp, uint32_t now) {
    previousPhaseIndex = phaseIndexForHp(previousHp);
    int currentPhaseIndex = phaseIndexForHp(currentHp);
    phaseTransition = currentHp > 0 && currentPhaseIndex != previousPhaseIndex;

    if (phaseTransition) {
      firstLed = 0;
      endLed = phaseLitCount(previousHp);
    } else {
      firstLed = phaseLitCount(currentHp);
      endLed = phaseLitCount(previousHp);
    }

    if (endLed <= firstLed && endLed > 0) {
      firstLed = endLed - 1;
    }
    startedMs = now;
    visibleUntilMs = now + DAMAGE_CHIP_MS;
  }

  void delayUntil(uint32_t startMs) {
    uint32_t duration = visibleUntilMs - startedMs;
    startedMs = startMs;
    visibleUntilMs = startMs + duration;
  }

  bool visible(uint32_t now) const {
    return endLed > firstLed && (int32_t)(visibleUntilMs - now) > 0;
  }

  int length() const {
    return max(0, endLed - firstLed);
  }

  int visibleCount(uint32_t now) const {
    if (!visible(now)) return 0;

    int total = length();
    if (total <= 0) return 0;

    uint32_t elapsed = now > startedMs ? now - startedMs : 0;
    if (elapsed > DAMAGE_CHIP_MS) elapsed = DAMAGE_CHIP_MS;

    uint32_t progress = elapsed * static_cast<uint32_t>(total);
    int expired = static_cast<int>(progress / DAMAGE_CHIP_MS);
    return constrain(total - expired, 0, total);
  }

  int expiredCount(uint32_t now) const {
    return length() - visibleCount(now);
  }
};

DeviceIdentity identity;
TargetState target;
CaptureState capture;
SensorState sensor;
ResetButtonState resetButton;
EffectTimers timers;
DamageChipState damageChip;

constexpr uint32_t STATUS_PERIOD_MS = 1000;

CRGB addBlend(const CRGB& base, const CRGB& overlay);
CRGB scaleColor(const CRGB& color, uint8_t scale) {
  return CRGB(scale8(color.r, scale), scale8(color.g, scale), scale8(color.b, scale));
}

CRGB phaseColor(int phaseIndex) {
  phaseIndex = constrain(phaseIndex, 0, HP_PHASE_COUNT - 1);
  if (phaseIndex == 0) return CRGB(0, 150, 0);
  if (phaseIndex == HP_PHASE_COUNT - 1) return CRGB(190, 0, 0);
  if (HP_PHASE_COUNT == 3) return CRGB(190, 130, 0);

  uint8_t hue = static_cast<uint8_t>(map(phaseIndex, 0, HP_PHASE_COUNT - 1, HUE_GREEN, HUE_RED));
  return CHSV(hue, 255, 150);
}
CRGB applyHpHitPulse(const CRGB& color, uint32_t now) {
  if ((int32_t)(timers.hitFlashUntilMs - now) > 0) return color;
  if ((int32_t)(timers.hpPulseUntilMs - now) <= 0) return color;

  uint32_t remaining = timers.hpPulseUntilMs - now;
  if (remaining > HP_HIT_PULSE_MS) remaining = HP_HIT_PULSE_MS;
  uint8_t boost = static_cast<uint8_t>((remaining * 90) / HP_HIT_PULSE_MS);
  return addBlend(color, CRGB(boost, boost, boost / 3));
}

void IRAM_ATTR piezoDoIsr() {
  uint32_t nowUs = micros();
  if (nowUs - sensor.lastIsrUs < ISR_DEBOUNCE_US) return;
  sensor.lastIsrUs = nowUs;
  if (sensor.digitalEdgeCount < UINT16_MAX) {
    sensor.digitalEdgeCount++;
  }
}

uint16_t popPiezoDoEdges() {
  noInterrupts();
  uint16_t pending = sensor.digitalEdgeCount;
  sensor.digitalEdgeCount = 0;
  interrupts();
  return pending;
}

void clearPiezoDoFlag() {
  noInterrupts();
  sensor.digitalEdgeCount = 0;
  interrupts();
}

uint16_t readPiezoAnalog() {
  if (!HAS_PIEZO_AO) return 0;
  int value = analogRead(PIEZO_AO_PIN);
  if (value < 0) return 0;
  if (value > 4095) return 4095;
  return static_cast<uint16_t>(value);
}

CRGB addBlend(const CRGB& base, const CRGB& overlay) {
  return CRGB(qadd8(base.r, overlay.r), qadd8(base.g, overlay.g), qadd8(base.b, overlay.b));
}

int phaseIndexForHp(int hp) {
  hp = constrain(hp, 0, MAX_HITS);
  if (hp <= 0) return HP_PHASE_COUNT - 1;

  int hitsConsumed = MAX_HITS - hp;
  int phaseIndex = hitsConsumed / HITS_PER_PHASE;
  return constrain(phaseIndex, 0, HP_PHASE_COUNT - 1);
}

int phaseHitsRemaining(int hp) {
  hp = constrain(hp, 0, MAX_HITS);
  if (hp <= 0) return 0;

  int hitsConsumed = MAX_HITS - hp;
  int consumedInPhase = hitsConsumed % HITS_PER_PHASE;
  return HITS_PER_PHASE - consumedInPhase;
}

int phaseLitCount(int hp) {
  int hitsRemaining = phaseHitsRemaining(hp);
  return static_cast<int>(((long)hitsRemaining * NUM_LEDS + HITS_PER_PHASE - 1) / HITS_PER_PHASE);
}

void clearLayer(CRGB* layer) {
  fill_solid(layer, NUM_LEDS, CRGB::Black);
}

bool isLockedOut(uint32_t now) {
  return (int32_t)(timers.lockoutUntilMs - now) > 0;
}

bool resetButtonPressed() {
  return HAS_RESET_BUTTON && digitalRead(RESET_BUTTON_PIN) == LOW;
}

void pollResetButton(uint32_t now);

bool phaseRevealPending(uint32_t now) {
  return damageChip.phaseTransition && damageChip.visible(now);
}

void renderPhaseBackfill(int phaseIndex, int lit) {
  if (phaseIndex >= HP_PHASE_COUNT - 1) return;
  if (lit >= NUM_LEDS) return;

  int gap = constrain(PHASE_BACKFILL_GAP_LEDS, 0, NUM_LEDS / 2);
  int start = constrain(lit + gap, 0, NUM_LEDS);
  int end = constrain(NUM_LEDS - gap, 0, NUM_LEDS);
  if (start >= end) return;

  CRGB backfill = scaleColor(phaseColor(phaseIndex + 1), PHASE_BACKFILL_SCALE);
  for (int i = start; i < end; i++) {
    hpLayer[i] = backfill;
  }
}

void renderPhaseTransitionReveal(uint32_t now) {
  if (!damageChip.phaseTransition) return;

  int expired = damageChip.expiredCount(now);
  if (expired <= 0) return;

  int start = constrain(damageChip.endLed - expired, damageChip.firstLed, damageChip.endLed);
  int end = damageChip.endLed;
  CRGB nextColor = phaseColor(damageChip.previousPhaseIndex + 1);
  for (int i = start; i < end; i++) {
    if (i < 0 || i >= NUM_LEDS) continue;
    hpLayer[i] = nextColor;
  }
}

void renderHpLayer(uint32_t now) {
  clearLayer(hpLayer);
  if (phaseRevealPending(now)) {
    renderPhaseBackfill(damageChip.previousPhaseIndex, damageChip.endLed);
    renderPhaseTransitionReveal(now);
    return;
  }

  int lit = phaseLitCount(target.hpRemaining);
  int phaseIndex = phaseIndexForHp(target.hpRemaining);
  renderPhaseBackfill(phaseIndex, lit);
  if (lit <= 0) return;

  CRGB color = phaseColor(phaseIndex);
  color = applyHpHitPulse(color, now);

  for (int i = 0; i < lit; i++) {
    hpLayer[i] = color;
  }
}

void renderDamageLayer(uint32_t now) {
  clearLayer(damageLayer);
  if (!damageChip.visible(now)) return;

  int visibleCount = damageChip.visibleCount(now);
  if (visibleCount <= 0) return;

  uint8_t edgeFade = 255;
  int total = damageChip.length();
  uint32_t elapsed = now > damageChip.startedMs ? now - damageChip.startedMs : 0;
  if (elapsed > DAMAGE_CHIP_MS) elapsed = DAMAGE_CHIP_MS;
  uint32_t progress = elapsed * static_cast<uint32_t>(total);
  uint32_t edgePhase = progress % DAMAGE_CHIP_MS;
  if (edgePhase > 0) {
    edgeFade = 255 - static_cast<uint8_t>((edgePhase * 255) / DAMAGE_CHIP_MS);
  }

  for (int offset = 0; offset < visibleCount; offset++) {
    int i = damageChip.firstLed + offset;
    if (i < 0 || i >= NUM_LEDS) continue;

    uint8_t intensity = 230;
    if (offset == visibleCount - 1) {
      intensity = scale8(intensity, edgeFade);
    }
    damageLayer[i] = CRGB(intensity, scale8(intensity, 70), 0);
  }
}

void renderOrbitLayer(uint32_t now) {
  clearLayer(orbitLayer);
  if (target.hpRemaining <= 0) return;

  uint32_t step = ORBIT_STEP_MS < 1 ? 1 : ORBIT_STEP_MS;
  int head = (now / step) % NUM_LEDS;
  int tail = constrain(ORBIT_TAIL_LEDS, 0, NUM_LEDS - 1);

  orbitLayer[head] = CRGB::White;
  for (int offset = 1; offset <= tail; offset++) {
    int index = head - offset;
    while (index < 0) index += NUM_LEDS;
    uint8_t level = static_cast<uint8_t>(180 / (offset + 1));
    orbitLayer[index] = CRGB(level, level, level);
  }
}

void renderDefeatRainbow(uint32_t now) {
  uint32_t elapsed = now > timers.defeatStartedMs ? now - timers.defeatStartedMs : 0;
  if (elapsed > DEFEAT_RAINBOW_MS) elapsed = DEFEAT_RAINBOW_MS;

  uint32_t remaining = DEFEAT_RAINBOW_MS - elapsed;
  uint32_t fadeWindow = min<uint32_t>(260, DEFEAT_RAINBOW_MS);
  uint8_t fade = 255;
  if (remaining < fadeWindow) {
    fade = static_cast<uint8_t>((remaining * 255UL) / fadeWindow);
  }

  uint8_t phase = static_cast<uint8_t>((elapsed * 255UL * DEFEAT_RAINBOW_SPINS) / DEFEAT_RAINBOW_MS);
  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t hue = phase + static_cast<uint8_t>((i * 255UL) / NUM_LEDS);
    leds[i] = CHSV(hue, 255, scale8(230, fade));
  }
}

void renderLeds(uint32_t now) {
  if (now - timers.lastShowMs < LED_SHOW_PERIOD_MS) return;
  timers.lastShowMs = now;

  if ((int32_t)(timers.hitFlashUntilMs - now) > 0) {
    fill_solid(leds, NUM_LEDS, CRGB::White);
    FastLED.show();
    return;
  }

  if (target.hpRemaining <= 0) {
    if (damageChip.visible(now) && (int32_t)(timers.defeatStartedMs - now) > 0) {
      renderDamageLayer(now);
      for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = damageLayer[i];
      }
    } else if ((int32_t)(timers.defeatStartedMs - now) > 0) {
      fill_solid(leds, NUM_LEDS, CRGB::Black);
    } else if ((int32_t)(timers.defeatUntilMs - now) > 0) {
      renderDefeatRainbow(now);
    } else {
      fill_solid(leds, NUM_LEDS, CRGB::Black);
    }
    FastLED.show();
    return;
  }

  renderHpLayer(now);
  renderDamageLayer(now);
  renderOrbitLayer(now);
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = addBlend(addBlend(hpLayer[i], damageLayer[i]), orbitLayer[i]);
  }
  FastLED.show();
}

void publishStatus(const char* eventName,
                   uint16_t peak = 0,
                   const char* source = "state",
                   uint16_t digitalEdges = 0) {
  int hpPhase = target.hpRemaining > 0 ? phaseIndexForHp(target.hpRemaining) + 1 : HP_PHASE_COUNT;
  int phaseHits = phaseHitsRemaining(target.hpRemaining);
  Serial.printf(
      "{\"event\":\"%s\",\"target_id\":\"%s\",\"sequence\":%lu,"
      "\"device_mac\":\"%s\",\"source\":\"%s\",\"peak\":%u,\"hp_remaining\":%d,\"max_hits\":%d,"
      "\"hp_phase\":%d,\"hp_phase_count\":%d,\"hits_per_phase\":%d,\"phase_hits_remaining\":%d,"
      "\"phase_transition\":%s,\"phase_backfill_gap_leds\":%d,\"phase_backfill_scale\":%u,"
      "\"digital_edges\":%u,\"digital_hit_min_edges\":%u,\"digital_isr_debounce_us\":%lu,"
      "\"threshold\":%u,\"cooldown_ms\":%lu,\"hit_flash_ms\":%lu,"
      "\"damage_chip_ms\":%lu,\"hp_hit_pulse_ms\":%lu,\"defeat_blackout_ms\":%lu,"
      "\"defeat_rainbow_ms\":%lu,"
      "\"defeat_rainbow_spins\":%u,\"rearm_stable_ms\":%lu}\n",
      eventName,
      identity.targetId,
      static_cast<unsigned long>(target.sequence),
      identity.mac,
      source,
      peak,
      target.hpRemaining,
      MAX_HITS,
      hpPhase,
      HP_PHASE_COUNT,
      HITS_PER_PHASE,
      phaseHits,
      damageChip.phaseTransition ? "true" : "false",
      PHASE_BACKFILL_GAP_LEDS,
      PHASE_BACKFILL_SCALE,
      digitalEdges,
      DIGITAL_HIT_MIN_EDGES,
      static_cast<unsigned long>(DIGITAL_ISR_DEBOUNCE_US),
      HIT_THRESHOLD,
      static_cast<unsigned long>(HIT_COOLDOWN_MS),
      static_cast<unsigned long>(HIT_FLASH_MS),
      static_cast<unsigned long>(DAMAGE_CHIP_MS),
      static_cast<unsigned long>(HP_HIT_PULSE_MS),
      static_cast<unsigned long>(DEFEAT_BLACKOUT_MS),
      static_cast<unsigned long>(DEFEAT_RAINBOW_MS),
      DEFEAT_RAINBOW_SPINS,
      static_cast<unsigned long>(HIT_REARM_STABLE_MS));
}

void resetTarget(const char* source = "state") {
  target.reset();
  capture.reset();
  sensor.resetRuntime();
  timers.resetEffects();
  damageChip.reset();
  clearPiezoDoFlag();
  publishStatus("reset", 0, source);
}

void pollResetButton(uint32_t now) {
  if (!HAS_RESET_BUTTON) return;

  if (!resetButtonPressed()) {
    resetButton.release();
    return;
  }

  if (!resetButton.pressed) {
    resetButton.pressed = true;
    resetButton.pressedSinceMs = now;
    return;
  }

  if (resetButton.consumed) return;
  if (now - resetButton.pressedSinceMs < RESET_BUTTON_HOLD_MS) return;

  resetButton.consumed = true;
  resetTarget("button");
}

void registerHit(uint32_t now, uint16_t peak, const char* source, uint16_t digitalEdges = 0) {
  if (target.hpRemaining <= 0) return;
  if (isLockedOut(now)) return;

  target.damaged = true;
  target.sequence++;
  int previousHp = target.hpRemaining;
  target.hpRemaining--;
  damageChip.capture(previousHp, target.hpRemaining, now);
  if (peak == 0 && !HAS_PIEZO_AO) peak = HIT_THRESHOLD;

  timers.hitFlashUntilMs = now + HIT_FLASH_MS;
  timers.hpPulseUntilMs = timers.hitFlashUntilMs + HP_HIT_PULSE_MS;
  timers.lockoutUntilMs = now + HIT_COOLDOWN_MS;
  sensor.armed = false;
  sensor.quietStartedMs = 0;
  sensor.lastRearmCheckMs = now;
  clearPiezoDoFlag();

  if (target.hpRemaining <= 0) {
    target.hpRemaining = 0;
    target.destroyed = true;
    damageChip.delayUntil(timers.hitFlashUntilMs);
    timers.defeatStartedMs = damageChip.visibleUntilMs + DEFEAT_BLACKOUT_MS;
    timers.defeatUntilMs = timers.defeatStartedMs + DEFEAT_RAINBOW_MS;
    publishStatus("destroyed", peak, source, digitalEdges);
  } else {
    publishStatus("hit", peak, source, digitalEdges);
  }
}

void startCapture(uint32_t now, bool fromDigital, uint16_t initialPeak, uint16_t initialDigitalEdges) {
  capture.active = true;
  capture.startedMs = now;
  capture.peak = initialPeak;
  capture.digitalEdges = initialDigitalEdges;
  capture.fromDigital = fromDigital;
}

void finishCaptureIfDue(uint32_t now) {
  if (!capture.active) return;

  uint16_t value = readPiezoAnalog();
  if (value > capture.peak) capture.peak = value;

  if (now - capture.startedMs < CAPTURE_WINDOW_MS) return;

  uint16_t digitalEdges = capture.digitalEdges;
  bool hit = capture.fromDigital && digitalEdges >= DIGITAL_HIT_MIN_EDGES;
  if (HAS_PIEZO_AO && capture.peak >= HIT_THRESHOLD) hit = true;

  uint16_t peak = capture.peak;
  capture.reset();

  if (hit) registerHit(now, peak, "piezo", digitalEdges);
}

bool piezoQuiet() {
  if (HAS_PIEZO_AO) return readPiezoAnalog() < HIT_REARM_THRESHOLD;
  if (HAS_PIEZO_DO) return digitalRead(PIEZO_DO_PIN) == LOW;
  return true;
}

void updateSensorRearm(uint32_t now) {
  if (sensor.armed) return;
  if (isLockedOut(now)) return;
  if (now - sensor.lastRearmCheckMs < HIT_REARM_CHECK_MS) return;
  sensor.lastRearmCheckMs = now;

  if (!piezoQuiet()) {
    sensor.quietStartedMs = 0;
    clearPiezoDoFlag();
    return;
  }

  if (sensor.quietStartedMs == 0) {
    sensor.quietStartedMs = now;
    clearPiezoDoFlag();
    return;
  }

  if (now - sensor.quietStartedMs >= HIT_REARM_STABLE_MS) {
    sensor.armed = true;
    sensor.quietStartedMs = 0;
    clearPiezoDoFlag();
  }
}

void pollPiezo(uint32_t now) {
  uint16_t digitalEdges = popPiezoDoEdges();
  if (capture.active && digitalEdges > 0) {
    uint32_t totalEdges = static_cast<uint32_t>(capture.digitalEdges) + digitalEdges;
    capture.digitalEdges = totalEdges > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(totalEdges);
  }

  finishCaptureIfDue(now);
  updateSensorRearm(now);

  uint16_t analogValue = readPiezoAnalog();
  bool digitalTriggered = digitalEdges > 0;
  bool analogTriggered = HAS_PIEZO_AO && analogValue >= HIT_THRESHOLD;

  if (target.destroyed || capture.active) return;
  if (!sensor.armed) return;
  if (isLockedOut(now)) return;
  if (!digitalTriggered && !analogTriggered) return;

  startCapture(now, digitalTriggered, analogValue, digitalEdges);
}

void handleSerialCommand(char c) {
  if (c == '\r' || c == '\n' || c == ' ' || c == '\t') return;
  if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');

  if (c == 'r' || c == '2') {
    resetTarget("serial");
    return;
  }
  if (c == 'h') {
    registerHit(millis(), readPiezoAnalog(), "serial");
    return;
  }
  if (c == 's') {
    publishStatus("status", readPiezoAnalog(), "serial");
    return;
  }
  Serial.println("{\"event\":\"help\",\"commands\":\"r reset, h simulate hit, s status\"}");
}

void pollSerial() {
  while (Serial.available() > 0) {
    handleSerialCommand(static_cast<char>(Serial.read()));
  }
}

void publishPeriodicStatus(uint32_t now) {
  if (now - timers.lastStatusMs < STATUS_PERIOD_MS) return;
  timers.lastStatusMs = now;
  int hpPhase = target.hpRemaining > 0 ? phaseIndexForHp(target.hpRemaining) + 1 : HP_PHASE_COUNT;
  Serial.printf(
      "{\"event\":\"heartbeat\",\"target_id\":\"%s\",\"hp_remaining\":%d,"
      "\"max_hits\":%d,\"hp_phase\":%d,\"hp_phase_count\":%d,\"hits_per_phase\":%d,"
      "\"phase_hits_remaining\":%d,\"digital_hit_min_edges\":%u,"
      "\"digital_isr_debounce_us\":%lu,\"phase_backfill_gap_leds\":%d,\"phase_backfill_scale\":%u,"
      "\"defeat_blackout_ms\":%lu,"
      "\"defeat_rainbow_ms\":%lu,\"defeat_rainbow_spins\":%u,"
      "\"device_mac\":\"%s\",\"armed\":%s,\"capture\":%s,"
      "\"damaged\":%s,\"analog\":%u}\n",
      identity.targetId,
      target.hpRemaining,
      MAX_HITS,
      hpPhase,
      HP_PHASE_COUNT,
      HITS_PER_PHASE,
      phaseHitsRemaining(target.hpRemaining),
      DIGITAL_HIT_MIN_EDGES,
      static_cast<unsigned long>(DIGITAL_ISR_DEBOUNCE_US),
      PHASE_BACKFILL_GAP_LEDS,
      PHASE_BACKFILL_SCALE,
      static_cast<unsigned long>(DEFEAT_BLACKOUT_MS),
      static_cast<unsigned long>(DEFEAT_RAINBOW_MS),
      DEFEAT_RAINBOW_SPINS,
      identity.mac,
      sensor.armed ? "true" : "false",
      capture.active ? "true" : "false",
      target.damaged ? "true" : "false",
      readPiezoAnalog());
}

void printBootBanner() {
  Serial.printf(
      "[%s] target_id=%s device_mac=%s max_hits=%d hp_phase_count=%d hits_per_phase=%d threshold=%u rearm_threshold=%u cooldown_ms=%lu hit_flash_ms=%lu damage_chip_ms=%lu phase_backfill_gap_leds=%d phase_backfill_scale=%u hp_hit_pulse_ms=%lu defeat_blackout_ms=%lu defeat_rainbow_ms=%lu defeat_rainbow_spins=%u rearm_stable_ms=%lu digital_hit_min_edges=%u digital_isr_debounce_us=%lu orbit_step_ms=%lu\n",
      FIRMWARE_NAME,
      identity.targetId,
      identity.mac,
      MAX_HITS,
      HP_PHASE_COUNT,
      HITS_PER_PHASE,
      HIT_THRESHOLD,
      HIT_REARM_THRESHOLD,
      static_cast<unsigned long>(HIT_COOLDOWN_MS),
      static_cast<unsigned long>(HIT_FLASH_MS),
      static_cast<unsigned long>(DAMAGE_CHIP_MS),
      PHASE_BACKFILL_GAP_LEDS,
      PHASE_BACKFILL_SCALE,
      static_cast<unsigned long>(HP_HIT_PULSE_MS),
      static_cast<unsigned long>(DEFEAT_BLACKOUT_MS),
      static_cast<unsigned long>(DEFEAT_RAINBOW_MS),
      DEFEAT_RAINBOW_SPINS,
      static_cast<unsigned long>(HIT_REARM_STABLE_MS),
      DIGITAL_HIT_MIN_EDGES,
      static_cast<unsigned long>(DIGITAL_ISR_DEBOUNCE_US),
      static_cast<unsigned long>(ORBIT_STEP_MS));
  Serial.printf("[PIN] LED=%d NUM_LEDS=%d led_type=%s color_order=%s brightness=%u max_ma=%u piezo_do=%d piezo_ao=%d reset_button=%d reset_hold_ms=%lu\n",
                LED_PIN,
                NUM_LEDS,
                LED_TYPE_NAME,
                COLOR_ORDER_NAME,
                LED_BRIGHTNESS,
                LED_MAX_MA,
                PIEZO_DO_PIN,
                PIEZO_AO_PIN,
                RESET_BUTTON_PIN,
                static_cast<unsigned long>(RESET_BUTTON_HOLD_MS));
  Serial.println("[CMD] r/2=reset, h=simulate hit, s=status, BOOT hold=reset");
}

}  // namespace

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(200);

  random16_add_entropy(static_cast<uint16_t>(esp_random()));
  identity.begin();

  FastLED.addLeds<BATTLEBANG_HIT_TARGET_LED_TYPE, LED_PIN, BATTLEBANG_HIT_TARGET_COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(LED_BRIGHTNESS);
  FastLED.setMaxPowerInVoltsAndMilliamps(LED_MAX_VOLTS, LED_MAX_MA);
  FastLED.clear(true);

  if (HAS_PIEZO_AO) {
    analogReadResolution(12);
    analogSetPinAttenuation(PIEZO_AO_PIN, ADC_11db);
  }

  if (HAS_PIEZO_DO) {
    pinMode(PIEZO_DO_PIN, INPUT_PULLDOWN);
    attachInterrupt(digitalPinToInterrupt(PIEZO_DO_PIN), piezoDoIsr, RISING);
  }

  if (HAS_RESET_BUTTON) {
    pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  }

  printBootBanner();
  resetTarget("boot");
  renderLeds(millis());
}

void loop() {
  uint32_t now = millis();
  pollSerial();
  pollResetButton(now);
  pollPiezo(now);
  renderLeds(now);
  publishPeriodicStatus(now);
  delay(1);
}
