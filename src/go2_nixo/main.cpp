#include <Arduino.h>
#include "BluetoothSerial.h"

#include "go2_nixo/mqtt/hit_mqtt_client.h"
#include "go2_nixo/build_config.h"
#include "go2_nixo/nixo/nixo_fire_client.h"
#include "go2_nixo/ring_led/ring_display.h"

using namespace go2;

// Integrated Go2 hit/LED + Nixo fallback firmware:
// - This ESP samples piezo AO (ADC) and publishes threshold crossings as
//   hit_candidate events. Command Center owns final accept/reject, scoring,
//   down state, and LED ring display commands.
// - Nixo fire is handled on the same ESP through MQTT relay commands.
// - Piezo D0 is not used for hit judgment; it is read only for debug logs.

BluetoothSerial SerialBT;
HardwareSerial& JetsonSerial = Serial2;

RingDisplay ringDisplay;
HitMqttClient hitMqtt;
NixoFireClient nixoFire;

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
  analogSetPinAttenuation(PIEZO_AO_PIN, ADC_11db);

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

static void resetAll() {
  resetAnalogPiezoState();
  hitMqtt.clearOfflineQueue();
  nixoFire.stopFire("reset");
  ringDisplay.clearRemoteDisplay();
  ringDisplay.markDirty();
  Serial.println("[RESET] ADC hit/display state cleared");
  if (SerialBT.hasClient()) SerialBT.println("[RESET] ADC hit/display state cleared");
}

static void handleCommandChar(char c) {
  c = normalizeCommandChar(c);
  if (c == CMD_RESET_HIT_DISPLAY) {
    resetAll();
    return;
  }
  if (c == '1' || c == 'f') {
    bool started = nixoFire.startFire(NIXO_FIRE_DEFAULT_DURATION_MS, "serial");
    Serial.printf("[CMD] fire %s source=serial\n", started ? "started" : "ignored");
    if (SerialBT.hasClient()) SerialBT.printf("[CMD] fire %s source=serial\n", started ? "started" : "ignored");
  }
}

static void pollCommands() {
  while (JetsonSerial.available() > 0) {
    char c = (char)JetsonSerial.read();
    if (isIgnoredCommandChar(c)) continue;
    handleCommandChar(c);
  }

  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (isIgnoredCommandChar(c)) continue;
    handleCommandChar(c);
  }

  while (SerialBT.available() > 0) {
    char c = (char)SerialBT.read();
    if (isIgnoredCommandChar(c)) continue;
    handleCommandChar(c);
  }
}

static void onRingDisplayUpdate(const RingDisplayUpdate& update) {
  uint32_t now = millis();
  if (update.resetHitState) {
    resetAnalogPiezoState();
    hitMqtt.clearOfflineQueue();
    nixoFire.stopFire("mqtt-hit-reset");
    ringDisplay.clearRemoteDisplay();
    Serial.println("[RESET] MQTT ADC hit/display state reset");
    if (SerialBT.hasClient()) SerialBT.println("[RESET] MQTT ADC hit/display state reset");
  }
  nixoFire.setFireInhibited(update.down || update.mode == "down" || update.mode == "disabled");
  ringDisplay.setRemoteDisplay(update.fillRatio, update.mode, update.down, update.ttlMs, now);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  JetsonSerial.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  SerialBT.begin(BT_NAME);

  ringDisplay.begin();
  beginAnalogPiezo();
  hitMqtt.begin(onRingDisplayUpdate);
  nixoFire.begin();

  ringDisplay.markDirty();
  ringDisplay.tick(millis());

  Serial.printf("[PIN] UART2 RX=%d TX=%d | LED=%d | PIEZO_AO=%d | PIEZO_DO_DEBUG=%d\n",
                UART_RX_PIN,
                UART_TX_PIN,
                LED_PIN,
                PIEZO_AO_PIN,
                PIEZO_DO_PIN);
  Serial.printf("[ADC] threshold=%d rearm_raw=%d capture_window_ms=%lu cooldown_ms=%lu rearm_stable_ms=%lu\n",
                PIEZO_AO_THRESHOLD_RAW,
                PIEZO_AO_REARM_RAW,
                (unsigned long)PIEZO_AO_CAPTURE_WINDOW_MS,
                (unsigned long)HIT_COOLDOWN_MS,
                (unsigned long)HIT_REARM_STABLE_MS);
  Serial.printf("USB/BT/Jetson CMD: '%c'=reset ADC hit/display state, '1'/'f'=Nixo fire.\n",
                CMD_RESET_HIT_DISPLAY);
  Serial.print("Bluetooth name: ");
  Serial.println(BT_NAME);
  Serial.printf("[CC] robot_id=%s mqtt=%s broker=%s:%u event_topic=%s ring_topic=%s\n",
                ROBOT_ID,
                hitMqtt.configured() ? "enabled" : "disabled",
                MQTT_HOST,
                MQTT_PORT,
                hitMqtt.eventTopic(),
                hitMqtt.ringCommandTopic());
  Serial.printf("[NIXO] mqtt=%s nixo_id=%s command_topic=%s relay1=%d relay2=%d\n",
                nixoFire.configured() ? "enabled" : "disabled",
                NIXO_ID_VALUE,
                nixoFire.commandTopic(),
                NIXO_RELAY1_PIN_VALUE,
                NIXO_RELAY2_PIN_VALUE);
}

void loop() {
  uint32_t now = millis();

  hitMqtt.tick(now, ringDisplay.remoteDisplayActive());
  nixoFire.tick(now);
  pollCommands();
  ringDisplay.tick(now);
  pollAnalogPiezo(now);
  delay(1);
}
