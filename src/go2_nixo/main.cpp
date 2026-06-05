#include <Arduino.h>
#include "BluetoothSerial.h"

#include "go2_nixo/mqtt/hit_mqtt_client.h"
#include "go2_nixo/build_config.h"
#include "go2_nixo/nixo_fire_client.h"
#include "go2_nixo/sensors/piezo_sensor.h"
#include "go2_nixo/display/ring_display.h"

using namespace go2;

BluetoothSerial SerialBT;
HardwareSerial& JetsonSerial = Serial2;

RingDisplay ringDisplay;
PiezoSensor piezoSensor;
HitMqttClient hitMqtt;
NixoFireClient nixoFire;

uint32_t hitSequence = 0;

static char normalizeCommandChar(char c) {
  if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
  return c;
}

static bool isIgnoredCommandChar(char c) {
  return c == '\r' || c == '\n' || c == ' ' || c == '\t';
}

static void resetAll() {
  piezoSensor.resetFlags();
  hitMqtt.clearOfflineQueue();
  nixoFire.stopFire("reset");
  ringDisplay.clearRemoteDisplay();
  ringDisplay.markDirty();
  Serial.println("[RESET] hit/display state cleared");
  if (SerialBT.hasClient()) SerialBT.println("[RESET] hit/display state cleared");
}

static void handleTargetHit(int targetId, bool hit) {
  if (!hit) return;

  uint32_t now = millis();
  uint32_t sequence = ++hitSequence;
  if (hitMqtt.offlineQueueCount() > 0) {
    hitMqtt.queueHitCandidate(targetId, hit, sequence, now);
    return;
  }
  if (!hitMqtt.publishHitCandidate(targetId, hit, sequence, now)) {
    hitMqtt.queueHitCandidate(targetId, hit, sequence, now);
  }
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
    piezoSensor.resetFlags();
    hitMqtt.clearOfflineQueue();
    nixoFire.stopFire("mqtt-hit-reset");
    ringDisplay.clearRemoteDisplay();
    Serial.println("[RESET] MQTT hit/display state reset");
    if (SerialBT.hasClient()) SerialBT.println("[RESET] MQTT hit/display state reset");
  }
  ringDisplay.setRemoteDisplay(update.fillRatio, update.mode, update.down, update.ttlMs, now);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  JetsonSerial.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  SerialBT.begin(BT_NAME);

  ringDisplay.begin();
  piezoSensor.begin();
  hitMqtt.begin(onRingDisplayUpdate);
  nixoFire.begin();

  ringDisplay.markDirty();
  ringDisplay.tick(millis());

  Serial.printf("[PIN] UART2 RX=%d TX=%d | LED=%d | PIEZO_DO=%d\n",
                UART_RX_PIN,
                UART_TX_PIN,
                LED_PIN,
                PIEZO_DO_PIN);
  Serial.printf("USB/BT/Jetson CMD: '%c'=reset hit/display state, '1'/'f'=Nixo fire.\n",
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
  piezoSensor.poll(now, handleTargetHit);
  delay(1);
}
