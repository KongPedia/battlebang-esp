#include <Arduino.h>
#include "BluetoothSerial.h"

#include "go2/mqtt/hit_mqtt_client.h"
#include "go2/build_config.h"
#include "go2/fallback/offline_hit_fallback.h"
#include "go2/sensors/piezo_sensor.h"
#include "go2/display/ring_display.h"

using namespace go2;

BluetoothSerial SerialBT;
HardwareSerial& JetsonSerial = Serial2;

RingDisplay ringDisplay;
OfflineHitFallback offlineFallback;
PiezoSensor piezoSensor;
HitMqttClient hitMqtt;

uint32_t hitSequence = 0;
uint32_t lastHpTxMs = 0;
constexpr uint32_t HP_TX_PERIOD_MS = 100;

static char normalizeCommandChar(char c) {
  if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
  return c;
}

static bool isIgnoredCommandChar(char c) {
  return c == '\r' || c == '\n' || c == ' ' || c == '\t';
}

static void resetAll() {
  offlineFallback.reset(ringDisplay);
  piezoSensor.resetFlags();
  hitMqtt.clearPending();
  Serial.println("[RESET] OK");
  if (SerialBT.hasClient()) SerialBT.println("[RESET] OK");
}

static void applyPendingFallback(const PendingHitCandidate& pending, const char* reason) {
  Serial.printf("[FALLBACK] authority timeout %s seq=%lu target=%d peak=%u\n",
                reason,
                (unsigned long)pending.sequence,
                pending.targetId,
                pending.peak);
  offlineFallback.applyDamage(peakToDamage(pending.peak), ringDisplay);
}

static void drainTimedOutAuthorityFallback(uint32_t now) {
  PendingHitCandidate pending;
  if (hitMqtt.popTimedOutFallback(now, pending)) {
    applyPendingFallback(pending, "timeout");
  }
}

static void handleTargetHit(int targetId, uint16_t peak) {
  if (offlineFallback.down() && !hitMqtt.connected()) return;
  if (peak <= HIT_THRESHOLD) return;

  uint32_t now = millis();
  drainTimedOutAuthorityFallback(now);

  PendingHitCandidate pending;
  if (hitMqtt.popSupersededFallback(pending)) {
    applyPendingFallback(pending, "superseded");
  }

  uint32_t sequence = ++hitSequence;
  if (hitMqtt.publishHitCandidate(targetId, peak, sequence, now)) {
    hitMqtt.startPending(targetId, peak, sequence, now);
    return;
  }

  offlineFallback.applyDamage(peakToDamage(peak), ringDisplay);
}

static void handleCommandChar(char c) {
  c = normalizeCommandChar(c);
  if (c == CMD_RESET_HP) {
    resetAll();
    return;
  }
  if (c == '1' || c == 'f') {
    Serial.println("[CMD] fire ignored; handled by Nixo firmware");
    if (SerialBT.hasClient()) SerialBT.println("[CMD] fire ignored; handled by Nixo firmware");
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

static void systemTickLite() {
  uint32_t now = millis();
  pollCommands();
  ringDisplay.tick(now, offlineFallback.hp(), offlineFallback.down());
}

static void onRingDisplayUpdate(const RingDisplayUpdate& update) {
  uint32_t now = millis();
  ringDisplay.setRemoteDisplay(update.fillRatio, update.mode, update.down, update.ttlMs, now);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  JetsonSerial.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  SerialBT.begin(BT_NAME);

  ringDisplay.begin();
  piezoSensor.begin();
  offlineFallback.begin(JetsonSerial);
  hitMqtt.begin(onRingDisplayUpdate);

  ringDisplay.markDirty();
  ringDisplay.tick(millis(), offlineFallback.hp(), offlineFallback.down());

  Serial.printf("[PIN] UART2 RX=%d TX=%d | LED=%d | T1_DO=%d T1_AO=%d | T2_DO=%d T2_AO=%d\n",
                UART_RX_PIN,
                UART_TX_PIN,
                LED_PIN,
                T1_DO,
                T1_AO,
                T2_DO,
                T2_AO);
  Serial.printf("USB/BT/Jetson CMD: '%c'=reset HP/recovery. Fire is handled by Nixo firmware.\n", CMD_RESET_HP);
  Serial.print("Bluetooth name: ");
  Serial.println(BT_NAME);
  Serial.printf("[CC] robot_id=%s mqtt=%s broker=%s:%u event_topic=%s ring_topic=%s\n",
                ROBOT_ID,
                hitMqtt.configured() ? "enabled" : "disabled",
                MQTT_HOST,
                MQTT_PORT,
                hitMqtt.eventTopic(),
                hitMqtt.ringCommandTopic());
}

void loop() {
  uint32_t now = millis();

  hitMqtt.tick(now, ringDisplay.remoteDisplayActive());
  drainTimedOutAuthorityFallback(now);

  pollCommands();
  ringDisplay.tick(now, offlineFallback.hp(), offlineFallback.down());

  if (now - lastHpTxMs >= HP_TX_PERIOD_MS) {
    lastHpTxMs = now;
    offlineFallback.sendHpToJetson();
  }

  piezoSensor.poll(now, systemTickLite, handleTargetHit);
  delay(1);
}
