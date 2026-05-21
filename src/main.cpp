#include <Arduino.h>
#include "BluetoothSerial.h"

#include "go2/command_center/command_center_mqtt.h"
#include "go2/config.h"
#include "go2/fire/fire_control.h"
#include "go2/game/game_state.h"
#include "go2/hit/hit_sensor.h"
#include "go2/led/led_ring.h"

using namespace go2;

BluetoothSerial SerialBT;
HardwareSerial& JetsonSerial = Serial2;

LedRing ledRing;
FireControl fireControl;
GameState gameState;
HitSensor hitSensor;
CommandCenterMqtt commandCenter;

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
  gameState.reset(ledRing);
  hitSensor.resetFlags();
  fireControl.reset();
  commandCenter.clearPending();
  Serial.println("[RESET] OK");
  if (SerialBT.hasClient()) SerialBT.println("[RESET] OK");
}

static void applyPendingFallback(const PendingAuthorityHit& pending, const char* reason) {
  Serial.printf("[CC] authority fallback %s seq=%lu target=%d peak=%u\n",
                reason,
                (unsigned long)pending.sequence,
                pending.targetId,
                pending.peak);
  gameState.applyDamage(peakToDamage(pending.peak), ledRing);
}

static void drainTimedOutAuthorityFallback(uint32_t now) {
  PendingAuthorityHit pending;
  if (commandCenter.popTimedOutFallback(now, pending)) {
    applyPendingFallback(pending, "timeout");
  }
}

static void handleTargetHit(int targetId, uint16_t peak) {
  if (gameState.isDead()) return;
  if (peak <= HIT_THRESHOLD) return;

  uint32_t now = millis();
  drainTimedOutAuthorityFallback(now);

  PendingAuthorityHit pending;
  if (commandCenter.popSupersededFallback(pending)) {
    applyPendingFallback(pending, "superseded");
  }

  uint32_t sequence = ++hitSequence;
  if (commandCenter.publishHitCandidate(targetId, peak, sequence, now)) {
    commandCenter.startPending(targetId, peak, sequence, now);
    return;
  }

  gameState.applyDamage(peakToDamage(peak), ledRing);
}

static void handleImmediateChar(char c) {
  c = normalizeCommandChar(c);
  if (c == CMD_LOCAL_FIRE) {
    if (fireControl.start(gameState.isDead(), millis()) && SerialBT.hasClient()) SerialBT.println("[FIRE] start");
    return;
  }
}

static void handleJetsonChar(char c) {
  c = normalizeCommandChar(c);
  if (c == CMD_JETSON_FIRE) {
    fireControl.start(gameState.isDead(), millis());
    return;
  }
  if (c == CMD_JETSON_RESET_HP) {
    resetAll();
    return;
  }
}

static void pollCommands() {
  while (JetsonSerial.available() > 0) {
    char c = (char)JetsonSerial.read();
    if (isIgnoredCommandChar(c)) continue;
    handleJetsonChar(c);
  }

  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (isIgnoredCommandChar(c)) continue;
    handleImmediateChar(c);
  }

  while (SerialBT.available() > 0) {
    char c = (char)SerialBT.read();
    if (isIgnoredCommandChar(c)) continue;
    handleImmediateChar(c);
  }
}

static void systemTickLite() {
  uint32_t now = millis();
  pollCommands();
  fireControl.update(now);
  ledRing.tick(now, gameState.hp(), gameState.isDead());
}

static void onRingDisplayUpdate(const RingDisplayUpdate& update) {
  uint32_t now = millis();
  ledRing.setRemoteDisplay(update.fillRatio, update.mode, update.down, update.ttlMs, now);
  gameState.applyAuthorityDown(update.down, ledRing);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  JetsonSerial.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  SerialBT.begin(BT_NAME);

  ledRing.begin();
  fireControl.begin();
  hitSensor.begin();
  gameState.begin(JetsonSerial);
  commandCenter.begin(onRingDisplayUpdate);

  ledRing.markDirty();
  ledRing.tick(millis(), gameState.hp(), gameState.isDead());

  Serial.printf("[PIN] UART2 RX=%d TX=%d | LED=%d | T1_DO=%d T1_AO=%d | T2_DO=%d T2_AO=%d | SERVO=%d | RELAY1=%d RELAY2=%d\n",
                UART_RX_PIN,
                UART_TX_PIN,
                LED_PIN,
                T1_DO,
                T1_AO,
                T2_DO,
                T2_AO,
                SERVO_PIN,
                RELAY1_PIN,
                RELAY2_PIN);
  Serial.printf("USB/BT CMD: %c(fire). Damage is piezo peak based only.\n", CMD_LOCAL_FIRE);
  Serial.printf("Jetson BYTE: '%c'=fire, '%c'=reset HP/recovery\n", CMD_JETSON_FIRE, CMD_JETSON_RESET_HP);
  Serial.print("Bluetooth name: ");
  Serial.println(BT_NAME);
  Serial.printf("[CC] robot_id=%s mqtt=%s broker=%s:%u event_topic=%s ring_topic=%s\n",
                ROBOT_ID,
                commandCenter.configured() ? "enabled" : "disabled",
                MQTT_HOST,
                MQTT_PORT,
                commandCenter.eventTopic(),
                commandCenter.ringCommandTopic());
}

void loop() {
  uint32_t now = millis();

  commandCenter.tick(now, gameState.isDead(), ledRing.remoteDisplayActive());
  drainTimedOutAuthorityFallback(now);

  pollCommands();
  fireControl.update(now);
  ledRing.tick(now, gameState.hp(), gameState.isDead());

  if (now - lastHpTxMs >= HP_TX_PERIOD_MS) {
    lastHpTxMs = now;
    gameState.sendHpToJetson();
  }

  hitSensor.poll(now, systemTickLite, handleTargetHit);
  delay(1);
}
