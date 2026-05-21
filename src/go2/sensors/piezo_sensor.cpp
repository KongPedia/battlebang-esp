#include "go2/sensors/piezo_sensor.h"

namespace go2 {

volatile bool t1Flag = false;
volatile bool t2Flag = false;
volatile uint32_t t1LastIsrUs = 0;
volatile uint32_t t2LastIsrUs = 0;

namespace {

struct SensorGateState {
  uint32_t lastEventMs = 0;
  uint32_t quietStartedMs = 0;
  uint32_t lastRearmCheckMs = 0;
  bool armed = true;
};

SensorGateState t1Gate;
SensorGateState t2Gate;

uint16_t samplePiezoPeak(int analogPin, SystemTickFn systemTick, uint16_t initialPeak = 0) {
  uint16_t peak = initialPeak;
  if (POST_DELAY_MS) delay(POST_DELAY_MS);

  uint32_t startUs = micros();
  for (int i = 0; i < CAPTURE_SAMPLES; i++) {
    uint32_t targetUs = startUs + (uint32_t)i * SAMPLE_INTERVAL_US;
    while ((int32_t)(micros() - targetUs) < 0) {
      if (systemTick != nullptr) systemTick();
      yield();
    }
    uint16_t value = analogRead(analogPin);
    if (value > peak) peak = value;
    if (systemTick != nullptr) systemTick();
  }
  return peak;
}

void clearFlag(volatile bool& flag) {
  noInterrupts();
  flag = false;
  interrupts();
}

bool popFlag(volatile bool& flag) {
  noInterrupts();
  bool pending = flag;
  flag = false;
  interrupts();
  return pending;
}

void rearmWhenQuiet(volatile bool& flag, SensorGateState& gate, uint32_t now, int digitalPin, int analogPin) {
  if (gate.armed) return;
  if (now - gate.lastEventMs < HIT_COOLDOWN_MS) return;
  if (now - gate.lastRearmCheckMs < HIT_REARM_CHECK_MS) return;
  gate.lastRearmCheckMs = now;

  // Treat the analog piezo input as the source of truth for re-arming. The
  // digital comparator output can be absent, disconnected, or calibrated
  // differently per sensor board. If we require DO=LOW here, a bad/stuck DO
  // path can make both MQTT hit publishing and local fallback look dead.
  (void)digitalPin;
  bool quiet = analogRead(analogPin) < HIT_REARM_THRESHOLD;
  if (!quiet) {
    gate.quietStartedMs = 0;
    clearFlag(flag);
    return;
  }

  if (gate.quietStartedMs == 0) {
    gate.quietStartedMs = now;
    clearFlag(flag);
    return;
  }

  if (now - gate.quietStartedMs >= HIT_REARM_STABLE_MS) {
    gate.armed = true;
    gate.quietStartedMs = 0;
    clearFlag(flag);
  }
}

void handleSensor(volatile bool& flag, SensorGateState& gate, uint32_t now, int targetId, int digitalPin,
                  int analogPin, SystemTickFn systemTick, HitCallback onHit) {
  rearmWhenQuiet(flag, gate, now, digitalPin, analogPin);

  bool digitalTriggered = popFlag(flag);
  uint16_t triggerPeak = analogRead(analogPin);
  bool analogTriggered = triggerPeak > HIT_THRESHOLD;
  if (!digitalTriggered && !analogTriggered) return;
  if (!gate.armed) return;
  if (now - gate.lastEventMs < HIT_COOLDOWN_MS) return;

  uint16_t peak = samplePiezoPeak(analogPin, systemTick, triggerPeak);
  if (peak <= HIT_THRESHOLD) return;

  gate.lastEventMs = now;
  gate.armed = false;
  gate.quietStartedMs = 0;
  gate.lastRearmCheckMs = now;

  if (onHit != nullptr) onHit(targetId, peak);
}

}  // namespace

void IRAM_ATTR isrT1() {
  uint32_t nowUs = micros();
  if (nowUs - t1LastIsrUs < ISR_DEBOUNCE_US) return;
  t1LastIsrUs = nowUs;
  t1Flag = true;
}

void IRAM_ATTR isrT2() {
  uint32_t nowUs = micros();
  if (nowUs - t2LastIsrUs < ISR_DEBOUNCE_US) return;
  t2LastIsrUs = nowUs;
  t2Flag = true;
}

void PiezoSensor::begin() {
  analogReadResolution(12);
  analogSetPinAttenuation(T1_AO, ADC_11db);
  analogSetPinAttenuation(T2_AO, ADC_11db);

  pinMode(T1_DO, INPUT_PULLDOWN);
  pinMode(T2_DO, INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(T1_DO), isrT1, RISING);
  attachInterrupt(digitalPinToInterrupt(T2_DO), isrT2, RISING);
}

void PiezoSensor::resetFlags() {
  clearFlag(t1Flag);
  clearFlag(t2Flag);
  t1Gate = SensorGateState{};
  t2Gate = SensorGateState{};
}

void PiezoSensor::poll(uint32_t now, SystemTickFn systemTick, HitCallback onHit) {
  handleSensor(t1Flag, t1Gate, now, 1, T1_DO, T1_AO, systemTick, onHit);
  handleSensor(t2Flag, t2Gate, now, 2, T2_DO, T2_AO, systemTick, onHit);
}

}  // namespace go2
