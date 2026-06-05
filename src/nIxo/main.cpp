#include <Arduino.h>
#include <cstring>
#include <FastLED.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "BluetoothSerial.h"
#include "build_config.h"

// ================== Bluetooth ==================
BluetoothSerial SerialBT;
static const char* BT_NAME = "ESP32_HP_SERVO";

// ================== UART2 (Jetson) ==================
static const int UART_RX_PIN = 16;
static const int UART_TX_PIN = 17;
static const uint32_t UART_BAUD = 115200;

// ESP -> Jetson: newline-delimited numeric HP only.
// Jetson -> ESP commands.
constexpr char CMD_JETSON_FIRE = '1';
constexpr char CMD_JETSON_RESET_HP = '2';

// USB/BT Serial Monitor commands.
constexpr char CMD_DAMAGE_T1 = 'q';
constexpr char CMD_DAMAGE_T2 = 'w';
constexpr char CMD_LOCAL_FIRE = 'f';

// HP/game tuning.
constexpr int HP_MAX = 3000;
constexpr int HP_PER_LAP = 1000;
constexpr int HP_DAMAGE_PER_HIT = 200;
bool isDead = false;

// ================== LED ==================
#define LED_PIN     4
#define NUM_LEDS    40
#define BRIGHTNESS  60
#define LED_TYPE    WS2811
#define COLOR_ORDER RGB
CRGB leds[NUM_LEDS];

static const uint8_t  LED_MAX_VOLTS = 5;
static const uint16_t LED_MAX_MA    = 900;

// ================== Targets ==================
constexpr int T1_DO = 25;
constexpr int T1_AO = 34;
constexpr int T2_DO = 26;
constexpr int T2_AO = 35;

// ================== TEST ==================
constexpr int BOOT_BTN = 0;
constexpr int BOOT_BUTTON_DAMAGE = HP_DAMAGE_PER_HIT;

// ================== RELAY ==================
constexpr int RELAY1_PIN = NIXO_RELAY1_PIN;
constexpr int RELAY2_PIN = NIXO_RELAY2_PIN;
constexpr bool RELAY2_ENABLED = RELAY2_PIN >= 0;
constexpr int RELAY_ON = NIXO_RELAY_ON_LEVEL;
constexpr int RELAY_OFF = NIXO_RELAY_OFF_LEVEL;

static void relayOff() {
  digitalWrite(RELAY1_PIN, RELAY_OFF);
  if (RELAY2_ENABLED) {
    digitalWrite(RELAY2_PIN, RELAY_OFF);
  }
}

// ================== SERVO ==================
constexpr int SERVO_PIN = 18;
constexpr int SERVO_PWM_FREQ = 50;
constexpr int SERVO_PWM_RES  = 16;
constexpr int SERVO_PWM_CHANNEL = 0;

static uint32_t usToDuty(int us) {
  us = constrain(us, 500, 2400);
  return (uint32_t)((((uint64_t)us) * ((1UL << SERVO_PWM_RES) - 1)) / 20000ULL);
}
static int angleToUs(int angle) {
  angle = constrain(angle, 0, 180);
  return map(angle, 0, 180, 500, 2400);
}
static void servoAttachPwm() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(SERVO_PIN, SERVO_PWM_FREQ, SERVO_PWM_RES);
#else
  ledcSetup(SERVO_PWM_CHANNEL, SERVO_PWM_FREQ, SERVO_PWM_RES);
  ledcAttachPin(SERVO_PIN, SERVO_PWM_CHANNEL);
#endif
}
static void servoWriteDuty(uint32_t duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(SERVO_PIN, duty);
#else
  ledcWrite(SERVO_PWM_CHANNEL, duty);
#endif
}
static void servoWriteAngle(int angle) {
  servoWriteDuty(usToDuty(angleToUs(angle)));
}

constexpr int FIRE_POS_A = 55;
constexpr int FIRE_POS_B = 145;
constexpr int SERVO_STEP = 2;
constexpr uint32_t SERVO_STEP_DT_MS = 15;

int servoCur = 125;
int servoTarget = 125;
uint32_t lastServoMs = 0;

static void setServoTarget(int angle) {
  servoTarget = constrain(angle, 0, 180);
}

static bool updateServoMove() {
  if (servoCur == servoTarget) return true;

  uint32_t now = millis();
  if (now - lastServoMs < SERVO_STEP_DT_MS) return false;
  lastServoMs = now;

  if (servoCur < servoTarget) servoCur = min(servoCur + SERVO_STEP, servoTarget);
  else                        servoCur = max(servoCur - SERVO_STEP, servoTarget);

  servoWriteAngle(servoCur);
  return (servoCur == servoTarget);
}

// ================== FIRE ==================
constexpr uint32_t RELAY_DELAY1_MS = 800;
constexpr uint32_t DEFAULT_FIRE_DURATION_MS = NIXO_FIRE_DEFAULT_DURATION_MS;
constexpr uint32_t MIN_FIRE_DURATION_MS = NIXO_FIRE_MIN_DURATION_MS;
constexpr uint32_t MAX_FIRE_DURATION_MS = NIXO_FIRE_MAX_DURATION_MS;
constexpr uint32_t FIRE_COOLDOWN_MS = 2500;   // 연타 방지

enum FireState {
  FIRE_IDLE,
  FIRE_SERVO_MOVING,
  FIRE_RELAY_WAIT1,
  FIRE_RELAY_WAIT2
};

FireState fireState = FIRE_IDLE;
uint32_t fireTimerMs = 0;
uint32_t lastFireStartMs = 0;
uint32_t activeFireDurationMs = DEFAULT_FIRE_DURATION_MS;

static bool isFiring() {
  return fireState != FIRE_IDLE;
}

static bool startFireSequence(uint32_t durationMs = DEFAULT_FIRE_DURATION_MS, const char* source = "local") {
  uint32_t now = millis();

  if (isDead) {
    Serial.printf("[FIRE] ignored source=%s reason=dead\n", source);
    if (SerialBT.hasClient()) SerialBT.printf("[FIRE] ignored source=%s reason=dead\n", source);
    return false;
  }
  if (isFiring()) {
    Serial.printf("[FIRE] ignored source=%s reason=already_firing\n", source);
    if (SerialBT.hasClient()) SerialBT.printf("[FIRE] ignored source=%s reason=already_firing\n", source);
    return false;
  }
  if (lastFireStartMs != 0 && now - lastFireStartMs < FIRE_COOLDOWN_MS) {
    Serial.printf(
      "[FIRE] ignored source=%s reason=cooldown remaining_ms=%lu\n",
      source,
      (unsigned long)(FIRE_COOLDOWN_MS - (now - lastFireStartMs))
    );
    if (SerialBT.hasClient()) {
      SerialBT.printf(
        "[FIRE] ignored source=%s reason=cooldown remaining_ms=%lu\n",
        source,
        (unsigned long)(FIRE_COOLDOWN_MS - (now - lastFireStartMs))
      );
    }
    return false;
  }

  lastFireStartMs = now;
  activeFireDurationMs = constrain(durationMs, MIN_FIRE_DURATION_MS, MAX_FIRE_DURATION_MS);

  int next = (servoCur == FIRE_POS_A) ? FIRE_POS_B : FIRE_POS_A;
  setServoTarget(next);
  fireState = FIRE_SERVO_MOVING;

  Serial.printf("[FIRE] start source=%s duration_ms=%lu\n", source, (unsigned long)activeFireDurationMs);
  if (SerialBT.hasClient()) {
    SerialBT.printf("[FIRE] start source=%s duration_ms=%lu\n", source, (unsigned long)activeFireDurationMs);
  }
  return true;
}

static void stopFireSequence(const char* source = "local") {
  relayOff();
  fireState = FIRE_IDLE;

  Serial.printf("[FIRE] stop source=%s\n", source);
  if (SerialBT.hasClient()) SerialBT.printf("[FIRE] stop source=%s\n", source);
}

static void updateFireSequence() {
  uint32_t now = millis();

  switch (fireState) {
    case FIRE_IDLE:
      return;

    case FIRE_SERVO_MOVING:
      if (updateServoMove()) {
        digitalWrite(RELAY1_PIN, RELAY_ON);
        if (RELAY2_ENABLED) {
          digitalWrite(RELAY2_PIN, RELAY_OFF);
        }
        fireState = FIRE_RELAY_WAIT1;
        fireTimerMs = now;

        Serial.printf(
          "[RELAY] CH1 ON pin=%d level=%d readback=%d\n",
          RELAY1_PIN,
          RELAY_ON,
          digitalRead(RELAY1_PIN)
        );
      }
      return;

    case FIRE_RELAY_WAIT1:
      if (!RELAY2_ENABLED) {
        if (now - fireTimerMs >= activeFireDurationMs) {
          relayOff();
          fireState = FIRE_IDLE;

          Serial.printf("[RELAY] CH1 OFF pin=%d level=%d readback=%d\n", RELAY1_PIN, RELAY_OFF, digitalRead(RELAY1_PIN));
          Serial.println("[RELAY] ALL OFF / FIRE done");
        }
        return;
      }

      if (now - fireTimerMs >= RELAY_DELAY1_MS) {
        digitalWrite(RELAY2_PIN, RELAY_ON);
        fireState = FIRE_RELAY_WAIT2;
        fireTimerMs = now;

        Serial.printf(
          "[RELAY] CH2 ON pin=%d level=%d readback=%d\n",
          RELAY2_PIN,
          RELAY_ON,
          digitalRead(RELAY2_PIN)
        );
      }
      return;

    case FIRE_RELAY_WAIT2:
      if (now - fireTimerMs >= activeFireDurationMs) {
        relayOff();
        fireState = FIRE_IDLE;

        Serial.printf(
          "[RELAY] CH1 OFF pin=%d readback=%d | CH2 OFF pin=%d readback=%d\n",
          RELAY1_PIN,
          digitalRead(RELAY1_PIN),
          RELAY2_PIN,
          digitalRead(RELAY2_PIN)
        );
        Serial.println("[RELAY] ALL OFF / FIRE done");
      }
      return;
  }
}

// ================== MQTT (Command Center / Nexus) ==================
WiFiClient nixoWifiClient;
PubSubClient nixoMqttClient(nixoWifiClient);

char nixoMqttCommandTopic[128] = {0};
uint32_t lastWifiAttemptMs = 0;
uint32_t lastMqttAttemptMs = 0;
String lastMqttRequestId;

static bool isPlaceholder(const char* value, const char* placeholder) {
  return value == nullptr || value[0] == '\0' || strcmp(value, placeholder) == 0;
}

static bool hasWifiConfig() {
  return !isPlaceholder(NIXO_WIFI_SSID, "YOUR_WIFI_SSID");
}

static bool hasMqttConfig() {
  return !isPlaceholder(NIXO_MQTT_HOST, "YOUR_MQTT_HOST");
}

static bool networkConfigReady() {
  return hasWifiConfig() && hasMqttConfig();
}

static uint32_t clampFireDuration(uint32_t durationMs) {
  return constrain(durationMs, MIN_FIRE_DURATION_MS, MAX_FIRE_DURATION_MS);
}

static void handleNixoMqttCommand(const char* payload, unsigned int length) {
  StaticJsonDocument<NIXO_MQTT_BUFFER_BYTES> doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    Serial.printf("[MQTT] invalid JSON: %s\n", error.c_str());
    return;
  }

  const int schemaVersion = doc["schema_version"] | 0;
  const char* command = doc["command"] | "";
  const char* nixoId = doc["nixo_id"] | "";
  const char* requestId = doc["request_id"] | "";
  const bool enabled = doc["enabled"] | true;

  if (schemaVersion != 1) {
    Serial.printf("[MQTT] ignored schema_version=%d\n", schemaVersion);
    return;
  }
  if (strcmp(command, "fire") != 0) {
    Serial.printf("[MQTT] ignored command=%s\n", command);
    return;
  }
  if (strcmp(nixoId, NIXO_ID) != 0) {
    Serial.printf("[MQTT] ignored nixo_id=%s expected=%s\n", nixoId, NIXO_ID);
    return;
  }

  if (requestId[0] != '\0' && lastMqttRequestId == requestId) {
    Serial.printf("[MQTT] duplicate request_id=%s ignored\n", requestId);
    return;
  }
  lastMqttRequestId = requestId;

  if (!enabled) {
    stopFireSequence("mqtt");
    Serial.printf("[MQTT] fire off request_id=%s\n", requestId);
    return;
  }

  const uint32_t durationMs = clampFireDuration(doc["duration_ms"] | DEFAULT_FIRE_DURATION_MS);
  Serial.printf("[MQTT] fire on request_id=%s duration_ms=%lu\n", requestId, (unsigned long)durationMs);
  if (!startFireSequence(durationMs, "mqtt")) {
    Serial.printf("[MQTT] fire not started request_id=%s\n", requestId);
  }
}

static void onNixoMqttMessage(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, nixoMqttCommandTopic) != 0) {
    Serial.printf("[MQTT] ignored topic=%s\n", topic);
    return;
  }
  if (length >= NIXO_MQTT_BUFFER_BYTES) {
    Serial.printf("[MQTT] payload too large length=%u limit=%u\n", length, NIXO_MQTT_BUFFER_BYTES - 1);
    return;
  }

  char payloadBuffer[NIXO_MQTT_BUFFER_BYTES];
  memcpy(payloadBuffer, payload, length);
  payloadBuffer[length] = '\0';
  handleNixoMqttCommand(payloadBuffer, length);
}

static void setupNixoMqtt() {
  snprintf(nixoMqttCommandTopic, sizeof(nixoMqttCommandTopic), "%s/%s/command", NIXO_MQTT_TOPIC_PREFIX, NIXO_ID);
  nixoMqttClient.setServer(NIXO_MQTT_HOST, NIXO_MQTT_PORT);
  nixoMqttClient.setCallback(onNixoMqttMessage);
  nixoMqttClient.setBufferSize(NIXO_MQTT_BUFFER_BYTES);

  Serial.printf("[NIXO] id=%s topic=%s duration_ms=%lu..%lu default=%lu\n",
                NIXO_ID,
                nixoMqttCommandTopic,
                (unsigned long)MIN_FIRE_DURATION_MS,
                (unsigned long)MAX_FIRE_DURATION_MS,
                (unsigned long)DEFAULT_FIRE_DURATION_MS);

  if (!networkConfigReady()) {
    Serial.println("[MQTT] disabled until src/nIxo/local_secrets.h or NIXO_* env vars provide Wi-Fi/MQTT config");
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
}

static void maintainNixoWifi(uint32_t now) {
  if (!networkConfigReady() || WiFi.status() == WL_CONNECTED) {
    return;
  }
  if (lastWifiAttemptMs != 0 && now - lastWifiAttemptMs < NIXO_WIFI_RETRY_MS) {
    return;
  }

  lastWifiAttemptMs = now;
  Serial.printf("[WiFi] connecting ssid=%s\n", NIXO_WIFI_SSID);
  WiFi.disconnect(false, false);
  WiFi.begin(NIXO_WIFI_SSID, NIXO_WIFI_PASSWORD);
}

static void maintainNixoMqtt(uint32_t now) {
  if (!networkConfigReady()) {
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (nixoMqttClient.connected()) {
    return;
  }
  if (lastMqttAttemptMs != 0 && now - lastMqttAttemptMs < NIXO_MQTT_RETRY_MS) {
    return;
  }

  lastMqttAttemptMs = now;
  char clientId[96];
  snprintf(clientId, sizeof(clientId), "battlebang-%s-%04X", NIXO_ID, (uint16_t)(ESP.getEfuseMac() & 0xFFFF));

  Serial.printf("[MQTT] connecting %s:%d client_id=%s\n", NIXO_MQTT_HOST, NIXO_MQTT_PORT, clientId);
  bool connected = false;
  if (strlen(NIXO_MQTT_USERNAME) > 0) {
    connected = nixoMqttClient.connect(clientId, NIXO_MQTT_USERNAME, NIXO_MQTT_PASSWORD);
  } else {
    connected = nixoMqttClient.connect(clientId);
  }

  if (!connected) {
    Serial.printf("[MQTT] connect failed rc=%d\n", nixoMqttClient.state());
    return;
  }

#if NIXO_CLEAR_RETAINED_COMMAND_ON_CONNECT
  nixoMqttClient.publish(nixoMqttCommandTopic, "", true);
  Serial.printf("[MQTT] cleared retained command topic=%s\n", nixoMqttCommandTopic);
#endif

  if (nixoMqttClient.subscribe(nixoMqttCommandTopic, NIXO_MQTT_QOS)) {
    Serial.printf("[MQTT] subscribed topic=%s qos=%d\n", nixoMqttCommandTopic, NIXO_MQTT_QOS);
  } else {
    Serial.printf("[MQTT] subscribe failed topic=%s\n", nixoMqttCommandTopic);
  }
}

static void nixoMqttLoopOnly() {
  if (nixoMqttClient.connected()) {
    nixoMqttClient.loop();
  }
}

static void nixoNetworkTick() {
  uint32_t now = millis();
  maintainNixoWifi(now);
  maintainNixoMqtt(now);
  nixoMqttLoopOnly();
}

// ================== HP ==================
int hp = HP_MAX;

bool blinkMask[NUM_LEDS] = {false};
bool blinkOn = false;
uint32_t lastBlinkMs = 0;
constexpr uint32_t BLINK_MS = 250;

uint32_t lastDeadBlinkMs = 0;
constexpr uint32_t DEAD_BLINK_MS = 300;
bool deadOn = false;

// ================== PIEZO / ADC ==================
constexpr uint32_t ISR_DEBOUNCE_US = 20000;
constexpr uint32_t COOLDOWN_MS     = 300;
constexpr uint32_t POST_DELAY_MS      = 0;
constexpr uint32_t SAMPLE_INTERVAL_US = 1000;
constexpr int      CAPTURE_SAMPLES    = 200;
constexpr uint16_t HIT_THRESHOLD      = 4000;

constexpr int DMG_T1 = HP_DAMAGE_PER_HIT;
constexpr int DMG_T2 = HP_DAMAGE_PER_HIT;

volatile bool t1Flag = false;
volatile bool t2Flag = false;
volatile uint32_t t1LastIsrUs = 0;
volatile uint32_t t2LastIsrUs = 0;

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

static int hpToBand(int hpVal) {
  if (hpVal <= 0) return -1;
  return (hpVal - 1) / HP_PER_LAP;
}
static int hpToLapHp(int hpVal) {
  if (hpVal <= 0) return 0;
  int r = hpVal % HP_PER_LAP;
  return (r == 0) ? HP_PER_LAP : r;
}
static int lapHpToLit(int lapHp) {
  lapHp = constrain(lapHp, 0, HP_PER_LAP);
  return (long)lapHp * NUM_LEDS / HP_PER_LAP;
}
static CRGB bandColor(int band) {
  if (band >= 2) return CRGB::Green;
  if (band == 1) return CRGB::Yellow;
  if (band == 0) return CRGB::Red;
  return CRGB::Black;
}
static CRGB nextBandColor(int band) {
  if (band <= 0) return CRGB::Black;
  return bandColor(band - 1);
}
static void clearBlinkMask() {
  for (int i = 0; i < NUM_LEDS; i++) blinkMask[i] = false;
}
static void addBlinkSegmentSameBand(int oldHp, int newHp) {
  int oldLit = lapHpToLit(hpToLapHp(oldHp));
  int newLit = lapHpToLit(hpToLapHp(newHp));
  if (newLit < oldLit) {
    for (int i = newLit; i < oldLit; i++) {
      if (i >= 0 && i < NUM_LEDS) blinkMask[i] = true;
    }
  }
}

// ================== LED show ==================
constexpr uint32_t SHOW_PERIOD_MS = 16;
uint32_t lastShowMs = 0;
bool ledsDirty = true;

static void ledShowTick() {
  uint32_t now = millis();
  if (!ledsDirty) return;
  if (now - lastShowMs < SHOW_PERIOD_MS) return;

  lastShowMs = now;
  FastLED.show();
  ledsDirty = false;
}

void renderLedsToBuffer() {
  uint32_t now = millis();

  if (now - lastBlinkMs >= BLINK_MS) {
    lastBlinkMs = now;
    blinkOn = !blinkOn;
    ledsDirty = true;
  }

  if (isDead) {
    if (now - lastDeadBlinkMs >= DEAD_BLINK_MS) {
      lastDeadBlinkMs = now;
      deadOn = !deadOn;
      ledsDirty = true;
    }
    for (int i = 0; i < NUM_LEDS; i++) leds[i] = deadOn ? CRGB::Red : CRGB::Black;
    return;
  }

  int band = hpToBand(hp);
  int lit  = lapHpToLit(hpToLapHp(hp));

  CRGB base   = bandColor(band);
  CRGB blinkC = nextBandColor(band);

  for (int i = 0; i < NUM_LEDS; i++) {
    if (i < lit) leds[i] = base;
    else if (blinkMask[i]) leds[i] = blinkOn ? blinkC : CRGB::Black;
    else leds[i] = CRGB::Black;
  }
}

// ================== HP / reset ==================
static void sendHpToJetson() {
  Serial2.println(hp);
}

static void setDeadMode() {
  isDead = true;
  deadOn = false;
  lastDeadBlinkMs = millis();
  sendHpToJetson();
  ledsDirty = true;
}

static void resetAll() {
  hp = HP_MAX;
  isDead = false;
  clearBlinkMask();

  noInterrupts();
  t1Flag = false;
  t2Flag = false;
  interrupts();

  fireState = FIRE_IDLE;
  activeFireDurationMs = DEFAULT_FIRE_DURATION_MS;
  relayOff();
  servoCur = 125;
  servoTarget = 125;
  servoWriteAngle(servoCur);

  sendHpToJetson();
  ledsDirty = true;

  Serial.println("[RESET] OK");
  if (SerialBT.hasClient()) SerialBT.println("[RESET] OK");
}

static void applyDamage(int dmg) {
  if (isDead) return;

  int oldHp = hp;
  int oldBand = hpToBand(oldHp);

  hp -= dmg;
  if (hp < 0) hp = 0;

  int newBand = hpToBand(hp);

  if (newBand != oldBand) clearBlinkMask();

  if (hp > 0 && newBand == oldBand) {
    addBlinkSegmentSameBand(oldHp, hp);
  } else if (hp > 0 && newBand != oldBand) {
    int newLit = lapHpToLit(hpToLapHp(hp));
    for (int i = newLit; i < NUM_LEDS; i++) blinkMask[i] = true;
  }

  if (hp == 0) {
    setDeadMode();
  } else {
    sendHpToJetson();
  }

  ledsDirty = true;
}

static void handleTargetHit(int targetId, uint16_t peak) {
  if (hp == 0) return;
  if (peak <= HIT_THRESHOLD) return;

  int dmg = (targetId == 1) ? DMG_T1 : DMG_T2;
  applyDamage(dmg);
}

// ================== Input ==================
static char normalizeCommandChar(char c) {
  if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
  return c;
}

static bool isIgnoredCommandChar(char c) {
  return c == '\r' || c == '\n' || c == ' ' || c == '\t';
}

static void handleImmediateChar(char c) {
  c = normalizeCommandChar(c);

  if (c == CMD_DAMAGE_T1) { applyDamage(DMG_T1); return; }
  if (c == CMD_DAMAGE_T2) { applyDamage(DMG_T2); return; }
  if (c == CMD_LOCAL_FIRE) { startFireSequence(DEFAULT_FIRE_DURATION_MS, "local"); return; }
}

static void handleJetsonChar(char c) {
  c = normalizeCommandChar(c);

  if (c == CMD_JETSON_FIRE) { startFireSequence(DEFAULT_FIRE_DURATION_MS, "jetson"); return; }
  if (c == CMD_JETSON_RESET_HP) { resetAll(); return; }
}

static void pollCommands() {
  // Jetson: fire + HP reset/recovery
  while (Serial2.available() > 0) {
    char c = (char)Serial2.read();
    if (isIgnoredCommandChar(c)) continue;
    handleJetsonChar(c);
  }

  // USB: immediate
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (isIgnoredCommandChar(c)) continue;
    handleImmediateChar(c);
  }

  // Bluetooth: immediate
  while (SerialBT.available() > 0) {
    char c = (char)SerialBT.read();
    if (isIgnoredCommandChar(c)) continue;
    handleImmediateChar(c);
  }
}

static void systemTickLite() {
  pollCommands();
  nixoMqttLoopOnly();
  updateFireSequence();
  renderLedsToBuffer();
  ledShowTick();
}

static uint16_t samplePiezoPeak(int analogPin) {
  uint16_t peak = 0;
  if (POST_DELAY_MS) delay(POST_DELAY_MS);

  uint32_t tStartUs = micros();
  for (int i = 0; i < CAPTURE_SAMPLES; i++) {
    uint32_t targetUs = tStartUs + (uint32_t)i * SAMPLE_INTERVAL_US;
    while ((int32_t)(micros() - targetUs) < 0) {
      systemTickLite();
      yield();
    }
    uint16_t v = analogRead(analogPin);
    if (v > peak) peak = v;
    systemTickLite();
  }
  return peak;
}

static void handlePendingTargetFlag(
  volatile bool& flag,
  uint32_t& lastEventMs,
  uint32_t now,
  int targetId,
  int analogPin
) {
  if (!flag) return;

  noInterrupts();
  bool pending = flag;
  flag = false;
  interrupts();

  if (!pending) return;
  if (now - lastEventMs < COOLDOWN_MS) return;
  lastEventMs = now;

  handleTargetHit(targetId, samplePiezoPeak(analogPin));
}

// ================== HP heartbeat ==================
constexpr uint32_t HP_TX_PERIOD_MS = 100;
uint32_t lastHpTxMs = 0;

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial2.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  SerialBT.begin(BT_NAME);

  pinMode(BOOT_BTN, INPUT_PULLUP);

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.setMaxPowerInVoltsAndMilliamps(LED_MAX_VOLTS, LED_MAX_MA);

  pinMode(RELAY1_PIN, OUTPUT);
  if (RELAY2_ENABLED) {
    pinMode(RELAY2_PIN, OUTPUT);
  }
  relayOff();

  servoAttachPwm();
  servoCur = 125;
  servoTarget = 125;
  servoWriteAngle(servoCur);

  analogReadResolution(12);
  analogSetPinAttenuation(T1_AO, ADC_11db);
  analogSetPinAttenuation(T2_AO, ADC_11db);

  pinMode(T1_DO, INPUT_PULLDOWN);
  pinMode(T2_DO, INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(T1_DO), isrT1, RISING);
  attachInterrupt(digitalPinToInterrupt(T2_DO), isrT2, RISING);

  // ESP32 EN/RESET button restarts firmware; reset HP explicitly on boot.
  hp = HP_MAX;
  isDead = false;
  clearBlinkMask();
  ledsDirty = true;
  renderLedsToBuffer();
  ledShowTick();

  sendHpToJetson();

  Serial.printf("[PIN] UART2 RX=%d TX=%d | LED=%d | T1_DO=%d T1_AO=%d | T2_DO=%d T2_AO=%d | SERVO=%d | RELAY1=%d RELAY2=%d relay_on=%d relay_off=%d\n",
                UART_RX_PIN,
                UART_TX_PIN,
                LED_PIN,
                T1_DO,
                T1_AO,
                T2_DO,
                T2_AO,
                SERVO_PIN,
                RELAY1_PIN,
                RELAY2_PIN,
                RELAY_ON,
                RELAY_OFF);

  Serial.printf("USB/BT CMD: %c(-%d), %c(-%d), %c(fire)\n",
                CMD_DAMAGE_T1, DMG_T1, CMD_DAMAGE_T2, DMG_T2, CMD_LOCAL_FIRE);
  Serial.printf("Jetson BYTE: '%c'=fire, '%c'=reset HP/recovery\n", CMD_JETSON_FIRE, CMD_JETSON_RESET_HP);
  Serial.print("Bluetooth name: ");
  Serial.println(BT_NAME);
  setupNixoMqtt();
}

void loop() {
  uint32_t now = millis();

  // TEST: BOOT button applies the same damage as one piezo hit.
  static bool bootPrev = true;
  bool bootNow = digitalRead(BOOT_BTN);
  if (bootPrev && !bootNow) {
    Serial.printf("[TEST] BOOT -> -%d HP\n", BOOT_BUTTON_DAMAGE);
    applyDamage(BOOT_BUTTON_DAMAGE);
  }
  bootPrev = bootNow;

  pollCommands();
  nixoNetworkTick();
  updateFireSequence();

  renderLedsToBuffer();
  ledShowTick();

  if (now - lastHpTxMs >= HP_TX_PERIOD_MS) {
    lastHpTxMs = now;
    sendHpToJetson();
  }

  static uint32_t t1LastEventMs = 0;
  static uint32_t t2LastEventMs = 0;

  handlePendingTargetFlag(t1Flag, t1LastEventMs, now, 1, T1_AO);
  handlePendingTargetFlag(t2Flag, t2LastEventMs, now, 2, T2_AO);

  delay(1);
}
