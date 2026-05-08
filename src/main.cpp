#include <Arduino.h>
#include <FastLED.h>
#include "BluetoothSerial.h"

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
const int RELAY1_PIN = 22;
const int RELAY2_PIN = 21;
const bool RELAY_ON  = LOW;
const bool RELAY_OFF = HIGH;

static void relayOff() {
  digitalWrite(RELAY1_PIN, RELAY_OFF);
  digitalWrite(RELAY2_PIN, RELAY_OFF);
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
constexpr uint32_t RELAY_DELAY2_MS = 1500;
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

static bool isFiring() {
  return fireState != FIRE_IDLE;
}

static void startFireSequence() {
  uint32_t now = millis();

  if (isDead) return;
  if (isFiring()) return;
  if (now - lastFireStartMs < FIRE_COOLDOWN_MS) return;

  lastFireStartMs = now;

  int next = (servoCur == FIRE_POS_A) ? FIRE_POS_B : FIRE_POS_A;
  setServoTarget(next);
  fireState = FIRE_SERVO_MOVING;

  Serial.println("[FIRE] start");
  if (SerialBT.hasClient()) SerialBT.println("[FIRE] start");
}

static void updateFireSequence() {
  uint32_t now = millis();

  switch (fireState) {
    case FIRE_IDLE:
      return;

    case FIRE_SERVO_MOVING:
      if (updateServoMove()) {
        digitalWrite(RELAY1_PIN, RELAY_ON);
        digitalWrite(RELAY2_PIN, RELAY_OFF);
        fireState = FIRE_RELAY_WAIT1;
        fireTimerMs = now;

        Serial.println("[RELAY] CH1 ON");
      }
      return;

    case FIRE_RELAY_WAIT1:
      if (now - fireTimerMs >= RELAY_DELAY1_MS) {
        digitalWrite(RELAY2_PIN, RELAY_ON);
        fireState = FIRE_RELAY_WAIT2;
        fireTimerMs = now;

        Serial.println("[RELAY] CH2 ON");
      }
      return;

    case FIRE_RELAY_WAIT2:
      if (now - fireTimerMs >= RELAY_DELAY2_MS) {
        relayOff();
        fireState = FIRE_IDLE;

        Serial.println("[RELAY] ALL OFF / FIRE done");
      }
      return;
  }
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
  if (c == CMD_LOCAL_FIRE) { startFireSequence(); return; }
}

static void handleJetsonChar(char c) {
  c = normalizeCommandChar(c);

  if (c == CMD_JETSON_FIRE) { startFireSequence(); return; }
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
  pinMode(RELAY2_PIN, OUTPUT);
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

  Serial.printf("[PIN] UART2 RX=%d TX=%d | LED=%d | T1_DO=%d T1_AO=%d | T2_DO=%d T2_AO=%d | SERVO=%d | RELAY1=%d RELAY2=%d\n",
                UART_RX_PIN, UART_TX_PIN, LED_PIN, T1_DO, T1_AO, T2_DO, T2_AO, SERVO_PIN, RELAY1_PIN, RELAY2_PIN);

  Serial.printf("USB/BT CMD: %c(-%d), %c(-%d), %c(fire)\n",
                CMD_DAMAGE_T1, DMG_T1, CMD_DAMAGE_T2, DMG_T2, CMD_LOCAL_FIRE);
  Serial.printf("Jetson BYTE: '%c'=fire, '%c'=reset HP/recovery\n", CMD_JETSON_FIRE, CMD_JETSON_RESET_HP);
  Serial.print("Bluetooth name: ");
  Serial.println(BT_NAME);
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
