#include <Arduino.h>
#include "BluetoothSerial.h"

#include "go2/build_config.h"
#include "go2/display/bar_display.h"
#include "go2/display/ring_display.h"
#include "go2/mqtt/hit_mqtt_client.h"

using namespace go2;

// Go2 hit/LED ESP firmware for the 2-ESP split:
// - Nixo fire is handled by the separate nIxo ESP through MQTT. This ESP
//   mirrors the Nixo fire command topic only to drive the fire/cooldown ring LED.
// - This ESP samples piezo AO (ADC) and publishes threshold crossings as
//   hit_candidate events. Command Center owns final accept/reject, scoring,
//   down state, and legacy-named ring_display commands rendered on the HP bar.
// - Piezo D0 is not used for hit judgment; it is read only for debug logs.

BluetoothSerial SerialBT;

BarDisplay barDisplay;
RingDisplay ringDisplay;
HitMqttClient hitMqtt;

uint32_t hitSequence = 0;

struct AnalogPiezoState {
  bool armed = true;
  bool captureActive = false;
  uint32_t captureStartedMs = 0;
  int capturePeakRaw = 0;
  uint32_t lastCandidateMs = 0;
  uint32_t quietStartedMs = 0;

  uint32_t lastDebugMs = 0;
  int lastRaw = 0;
  int minRaw = 4095;
  int maxRaw = 0;
  uint32_t sumRaw = 0;
  uint32_t sampleCount = 0;
};

AnalogPiezoState analogPiezo;

static bool piezoAoEnabled() {
  return PIEZO_AO_PIN >= 0;
}

static bool piezoDoDebugEnabled() {
  return PIEZO_DO_PIN >= 0;
}

static int readPiezoAoRaw() {
  if (!piezoAoEnabled()) return -1;
  return analogRead(PIEZO_AO_PIN);
}

static int readPiezoDoLevel() {
  if (!piezoDoDebugEnabled()) return -1;
  return digitalRead(PIEZO_DO_PIN);
}

static void resetAnalogStats(int raw) {
  if (raw < 0) raw = 0;
  analogPiezo.lastRaw = raw;
  analogPiezo.minRaw = raw;
  analogPiezo.maxRaw = raw;
  analogPiezo.sumRaw = 0;
  analogPiezo.sampleCount = 0;
}

static void resetAnalogPiezoState() {
  analogPiezo.armed = true;
  analogPiezo.captureActive = false;
  analogPiezo.captureStartedMs = 0;
  analogPiezo.capturePeakRaw = 0;
  analogPiezo.lastCandidateMs = 0;
  analogPiezo.quietStartedMs = 0;
  resetAnalogStats(readPiezoAoRaw());
}

static void beginAnalogPiezo() {
  if (!piezoAoEnabled()) {
    Serial.println("[PIEZO AO] disabled: PIEZO_AO_PIN < 0");
    return;
  }

  pinMode(PIEZO_AO_PIN, INPUT);
  analogReadResolution(12);
#if defined(ADC_11db)
  analogSetPinAttenuation(PIEZO_AO_PIN, ADC_11db);
#endif

  if (piezoDoDebugEnabled()) {
    pinMode(PIEZO_DO_PIN, INPUT_PULLDOWN);
  }

  resetAnalogPiezoState();
  Serial.printf("[PIEZO AO] ADC threshold mode pin=%d threshold=%d rearm_raw=%d capture_window_ms=%lu cooldown_ms=%lu debug_period_ms=%lu initial_raw=%d do_pin=%d do=%d\n",
                PIEZO_AO_PIN,
                PIEZO_AO_THRESHOLD_RAW,
                PIEZO_AO_REARM_RAW,
                (unsigned long)PIEZO_AO_CAPTURE_WINDOW_MS,
                (unsigned long)HIT_COOLDOWN_MS,
                (unsigned long)PIEZO_AO_DEBUG_PERIOD_MS,
                analogPiezo.lastRaw,
                PIEZO_DO_PIN,
                readPiezoDoLevel());
}

static void publishAdcHitCandidate(int targetId, int peakRaw, int thresholdRaw, uint32_t eventTsMs) {
  uint32_t sequence = ++hitSequence;

  Serial.printf("[PIEZO AO] candidate seq=%lu target=%d peak=%d threshold=%d ts_ms=%lu mqtt_connected=%s queue=%u\n",
                (unsigned long)sequence,
                targetId,
                peakRaw,
                thresholdRaw,
                (unsigned long)eventTsMs,
                hitMqtt.connected() ? "true" : "false",
                hitMqtt.offlineQueueCount());

  if (SerialBT.hasClient()) {
    SerialBT.printf("[PIEZO AO] candidate seq=%lu peak=%d threshold=%d\n",
                    (unsigned long)sequence,
                    peakRaw,
                    thresholdRaw);
  }

  if (hitMqtt.offlineQueueCount() > 0) {
    hitMqtt.queueHitCandidate(targetId, true, sequence, eventTsMs, peakRaw, thresholdRaw);
    return;
  }
  if (!hitMqtt.publishHitCandidate(targetId, true, sequence, eventTsMs, false, 0, 0, peakRaw, thresholdRaw)) {
    hitMqtt.queueHitCandidate(targetId, true, sequence, eventTsMs, peakRaw, thresholdRaw);
  }
}

static void updateAnalogDebugStats(int raw) {
  analogPiezo.lastRaw = raw;
  analogPiezo.minRaw = min(analogPiezo.minRaw, raw);
  analogPiezo.maxRaw = max(analogPiezo.maxRaw, raw);
  analogPiezo.sumRaw += (uint32_t)raw;
  analogPiezo.sampleCount++;
}

static void printAnalogDebugTick(uint32_t now) {
  if (now - analogPiezo.lastDebugMs < PIEZO_AO_DEBUG_PERIOD_MS) return;
  analogPiezo.lastDebugMs = now;

  uint32_t avg = analogPiezo.sampleCount > 0 ? analogPiezo.sumRaw / analogPiezo.sampleCount : (uint32_t)analogPiezo.lastRaw;
  Serial.printf("[PIEZO AO] ms=%lu raw=%d min=%d max=%d avg=%lu threshold=%d rearm=%d armed=%s capturing=%s do_pin=%d do=%d mqtt=%s queue=%u\n",
                (unsigned long)now,
                analogPiezo.lastRaw,
                analogPiezo.minRaw,
                analogPiezo.maxRaw,
                (unsigned long)avg,
                PIEZO_AO_THRESHOLD_RAW,
                PIEZO_AO_REARM_RAW,
                analogPiezo.armed ? "true" : "false",
                analogPiezo.captureActive ? "true" : "false",
                PIEZO_DO_PIN,
                readPiezoDoLevel(),
                hitMqtt.connected() ? "connected" : "disconnected",
                hitMqtt.offlineQueueCount());

  resetAnalogStats(analogPiezo.lastRaw);
}

static void rearmAnalogPiezoWhenQuiet(uint32_t now, int raw) {
  if (analogPiezo.armed) return;
  if (analogPiezo.captureActive) return;
  if (now - analogPiezo.lastCandidateMs < HIT_COOLDOWN_MS) return;

  if (raw > PIEZO_AO_REARM_RAW) {
    analogPiezo.quietStartedMs = 0;
    return;
  }

  if (analogPiezo.quietStartedMs == 0) {
    analogPiezo.quietStartedMs = now;
    return;
  }

  if (now - analogPiezo.quietStartedMs >= HIT_REARM_STABLE_MS) {
    analogPiezo.armed = true;
    analogPiezo.quietStartedMs = 0;
    Serial.printf("[PIEZO AO] rearmed raw=%d quiet_ms=%lu\n",
                  raw,
                  (unsigned long)HIT_REARM_STABLE_MS);
  }
}

static void pollAnalogPiezo(uint32_t now) {
  if (!piezoAoEnabled()) return;

  int raw = readPiezoAoRaw();
  updateAnalogDebugStats(raw);

  if (analogPiezo.captureActive) {
    analogPiezo.capturePeakRaw = max(analogPiezo.capturePeakRaw, raw);
    if (now - analogPiezo.captureStartedMs >= PIEZO_AO_CAPTURE_WINDOW_MS) {
      uint32_t eventTsMs = analogPiezo.captureStartedMs;
      analogPiezo.captureActive = false;
      analogPiezo.lastCandidateMs = now;
      analogPiezo.quietStartedMs = 0;
      publishAdcHitCandidate(1, analogPiezo.capturePeakRaw, PIEZO_AO_THRESHOLD_RAW, eventTsMs);
    }
    printAnalogDebugTick(now);
    return;
  }

  rearmAnalogPiezoWhenQuiet(now, raw);

  if (analogPiezo.armed && raw >= PIEZO_AO_THRESHOLD_RAW) {
    analogPiezo.armed = false;
    analogPiezo.captureActive = true;
    analogPiezo.captureStartedMs = now;
    analogPiezo.capturePeakRaw = raw;
    analogPiezo.quietStartedMs = 0;
    Serial.printf("[PIEZO AO] threshold crossed raw=%d threshold=%d ms=%lu do=%d\n",
                  raw,
                  PIEZO_AO_THRESHOLD_RAW,
                  (unsigned long)now,
                  readPiezoDoLevel());
  }

  printAnalogDebugTick(now);
}

static char normalizeCommandChar(char c) {
  if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
  return c;
}

static bool isIgnoredCommandChar(char c) {
  return c == '\r' || c == '\n' || c == ' ' || c == '\t';
}

static void resetAll(const char* source) {
  resetAnalogPiezoState();
  hitMqtt.clearOfflineQueue();
  barDisplay.clearRemoteDisplay();
  barDisplay.markDirty();
  ringDisplay.clearCooldown();
  ringDisplay.markDirty();
  Serial.printf("[RESET] source=%s ADC hit/display state cleared; sequence_kept=%lu\n",
                source,
                (unsigned long)hitSequence);
  if (SerialBT.hasClient()) SerialBT.println("[RESET] ADC hit/display state cleared");
}

static void handleCommandChar(char c, const char* source) {
  c = normalizeCommandChar(c);
  if (c == CMD_RESET_HIT_DISPLAY || c == 'r') {
    resetAll(source);
    return;
  }
  if (c == '1' || c == 'f') {
    Serial.printf("[CMD] fire ignored source=%s; handled by separate nIxo ESP/MQTT\n", source);
    if (SerialBT.hasClient()) SerialBT.println("[CMD] fire ignored; handled by nIxo ESP");
    return;
  }
  Serial.printf("[CMD] ignored source=%s char='%c'\n", source, c);
}

static void pollCommands() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (isIgnoredCommandChar(c)) continue;
    handleCommandChar(c, "usb");
  }

  while (SerialBT.available() > 0) {
    char c = (char)SerialBT.read();
    if (isIgnoredCommandChar(c)) continue;
    handleCommandChar(c, "bt");
  }
}


static void onNixoFireMirror(uint32_t fireDurationMs, uint32_t cooldownMs) {
  uint32_t now = millis();
  ringDisplay.startFire(fireDurationMs, cooldownMs, now);
  Serial.printf("[NIXO RING] fire mirrored fire_duration_ms=%lu cooldown_ms=%lu active=%s\n",
                (unsigned long)fireDurationMs,
                (unsigned long)cooldownMs,
                ringDisplay.cooldownActive(now) ? "true" : "false");
}

static void onBarDisplayUpdate(const BarDisplayUpdate& update) {
  uint32_t now = millis();
  if (update.resetHitState) {
    resetAnalogPiezoState();
    hitMqtt.clearOfflineQueue();
    barDisplay.clearRemoteDisplay();
    Serial.println("[RESET] MQTT ADC hit/display state reset");
    if (SerialBT.hasClient()) SerialBT.println("[RESET] MQTT ADC hit/display state reset");
  }
  barDisplay.setRemoteDisplay(update.fillRatio, update.mode, update.down, update.ttlMs, now);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  SerialBT.begin(BT_NAME);

  barDisplay.begin();
  ringDisplay.begin();
  beginAnalogPiezo();
  hitMqtt.begin(onBarDisplayUpdate, onNixoFireMirror);

  barDisplay.markDirty();
  ringDisplay.markDirty();
  barDisplay.tick(millis());
  ringDisplay.tick(millis());

  Serial.println("[MODE] go2 ADC threshold hit candidate + server HP bar display; D0 is debug-only; Nixo fire is separate ESP");
  Serial.printf("[PIN] HP_BAR_LED=%d count=%d groups=%d leds_per_group=%d | RING_LED=%d count=%d | PIEZO_AO=%d | PIEZO_DO_DEBUG=%d | BT=%s\n",
                HP_BAR_LED_PIN,
                HP_BAR_NUM_LEDS,
                HP_BAR_GROUP_COUNT,
                HP_BAR_LEDS_PER_GROUP,
                RING_LED_PIN,
                RING_NUM_LEDS,
                PIEZO_AO_PIN,
                PIEZO_DO_PIN,
                BT_NAME);
  Serial.printf("[ADC] threshold=%d rearm_raw=%d capture_window_ms=%lu cooldown_ms=%lu rearm_stable_ms=%lu\n",
                PIEZO_AO_THRESHOLD_RAW,
                PIEZO_AO_REARM_RAW,
                (unsigned long)PIEZO_AO_CAPTURE_WINDOW_MS,
                (unsigned long)HIT_COOLDOWN_MS,
                (unsigned long)HIT_REARM_STABLE_MS);
  Serial.printf("[CMD] USB/BT: '%c' or 'r'=reset local ADC latch/display queue. '1'/'f' ignored here.\n",
                CMD_RESET_HIT_DISPLAY);
  Serial.printf("[CC] robot_id=%s mqtt=%s broker=%s:%u event_topic=%s hp_bar_topic=%s nixo_cooldown_topic=%s\n",
                ROBOT_ID,
                hitMqtt.configured() ? "enabled" : "disabled",
                MQTT_HOST,
                MQTT_PORT,
                hitMqtt.eventTopic(),
                hitMqtt.ringCommandTopic(),
                hitMqtt.nixoCommandTopic());
}

void loop() {
  uint32_t now = millis();

  hitMqtt.tick(now, barDisplay.remoteDisplayActive());
  pollCommands();
  pollAnalogPiezo(now);
  barDisplay.tick(now);
  ringDisplay.tick(now);

  delay(1);
}
