#include <FastLED.h>

// ================= LED 설정 =================
#define RING_NUM_LEDS   40
#define HP_NUM_LEDS     92

#define BRIGHTNESS      80
#define LED_TYPE        WS2811
#define COLOR_ORDER     RGB

// 원형 LED 4개 데이터 핀
#define RING1_PIN       23
#define RING2_PIN       21
#define RING3_PIN       18
#define RING4_PIN       17

// HP 선형 LED 데이터 핀
#define HP_LED_PIN      26

// 피에조 DO 핀 4개
#define PIEZO1_DO_PIN   27
#define PIEZO2_DO_PIN   32
#define PIEZO3_DO_PIN   33
#define PIEZO4_DO_PIN   25

// ================= 게임 설정 =================
constexpr int HP_MAX = 3000;
constexpr int HP_PER_STAGE = 1000;
constexpr int DAMAGE = 300;

constexpr uint32_t TARGET_DURATION_MS = 2500;
constexpr uint32_t HIT_COOLDOWN_MS = 300;
constexpr uint32_t ISR_DEBOUNCE_US = 20000;

constexpr uint32_t HIT_FLASH_MS = 60;
constexpr uint32_t BLINK_MS = 250;
constexpr uint32_t DEAD_BLINK_MS = 300;

// ================= LED 배열 =================
CRGB ring1[RING_NUM_LEDS];
CRGB ring2[RING_NUM_LEDS];
CRGB ring3[RING_NUM_LEDS];
CRGB ring4[RING_NUM_LEDS];

CRGB hpLeds[HP_NUM_LEDS];

// ================= HP 상태 =================
int hp = HP_MAX;

bool blinkMask[HP_NUM_LEDS];
bool blinkOn = false;
uint32_t lastBlinkMs = 0;

bool hpFlashActive = false;
uint32_t hpFlashStartMs = 0;

bool deadBlinkOn = false;
uint32_t lastDeadBlinkMs = 0;

// ================= 타겟 상태 =================
int activeTarget = -1;
uint32_t targetStartMs = 0;

bool targetFlashActive = false;
uint32_t targetFlashStartMs = 0;
int flashTarget = -1;

// ================= 인터럽트 상태 =================
volatile bool piezoTriggered[4] = {false, false, false, false};
volatile uint32_t lastIsrUs[4] = {0, 0, 0, 0};

uint32_t lastHitMs[4] = {0, 0, 0, 0};

// ================= 인터럽트 함수 =================
void IRAM_ATTR piezoISR0() {
  uint32_t nowUs = micros();
  if (nowUs - lastIsrUs[0] < ISR_DEBOUNCE_US) return;
  lastIsrUs[0] = nowUs;
  piezoTriggered[0] = true;
}

void IRAM_ATTR piezoISR1() {
  uint32_t nowUs = micros();
  if (nowUs - lastIsrUs[1] < ISR_DEBOUNCE_US) return;
  lastIsrUs[1] = nowUs;
  piezoTriggered[1] = true;
}

void IRAM_ATTR piezoISR2() {
  uint32_t nowUs = micros();
  if (nowUs - lastIsrUs[2] < ISR_DEBOUNCE_US) return;
  lastIsrUs[2] = nowUs;
  piezoTriggered[2] = true;
}

void IRAM_ATTR piezoISR3() {
  uint32_t nowUs = micros();
  if (nowUs - lastIsrUs[3] < ISR_DEBOUNCE_US) return;
  lastIsrUs[3] = nowUs;
  piezoTriggered[3] = true;
}

// ================= 유틸 =================
CRGB* getRing(int index) {
  if (index == 0) return ring1;
  if (index == 1) return ring2;
  if (index == 2) return ring3;
  return ring4;
}

void fillRing(int index, CRGB color) {
  CRGB* ring = getRing(index);
  fill_solid(ring, RING_NUM_LEDS, color);
}

void showAll() {
  FastLED.show();
}

// ================= HP 계산 =================
int hpToBand(int hpVal) {
  if (hpVal <= 0) return -1;
  return (hpVal - 1) / HP_PER_STAGE;
}

int hpToStageHp(int hpVal) {
  if (hpVal <= 0) return 0;

  int r = hpVal % HP_PER_STAGE;
  return (r == 0) ? HP_PER_STAGE : r;
}

int stageHpToLit(int stageHp) {
  stageHp = constrain(stageHp, 0, HP_PER_STAGE);
  return (long)stageHp * HP_NUM_LEDS / HP_PER_STAGE;
}

CRGB hpColor(int band) {
  if (band >= 2) return CRGB::Green;
  if (band == 1) return CRGB::Yellow;
  if (band == 0) return CRGB::Red;
  return CRGB::Black;
}

CRGB nextHpColor(int band) {
  if (band >= 2) return CRGB::Yellow;
  if (band == 1) return CRGB::Red;
  if (band == 0) return CRGB::Red;
  return CRGB::Black;
}

void clearBlinkMask() {
  for (int i = 0; i < HP_NUM_LEDS; i++) {
    blinkMask[i] = false;
  }
}

void addBlinkSegment(int oldHp, int newHp) {
  int oldLit = stageHpToLit(hpToStageHp(oldHp));
  int newLit = stageHpToLit(hpToStageHp(newHp));

  for (int i = newLit; i < oldLit; i++) {
    if (i >= 0 && i < HP_NUM_LEDS) {
      blinkMask[i] = true;
    }
  }
}

// ================= 타겟 =================
void selectNewTarget() {
  int nextTarget;

  do {
    nextTarget = random(0, 4);
  } while (nextTarget == activeTarget);

  activeTarget = nextTarget;
  targetStartMs = millis();

  Serial.print("ACTIVE TARGET: ");
  Serial.println(activeTarget + 1);
}

void renderTargets() {
  for (int i = 0; i < 4; i++) {
    fillRing(i, CRGB::Black);
  }

  if (activeTarget >= 0 && activeTarget < 4) {
    fillRing(activeTarget, CRGB::Blue);
  }

  if (targetFlashActive) {
    if (millis() - targetFlashStartMs < HIT_FLASH_MS) {
      fillRing(flashTarget, CRGB::White);
    } else {
      targetFlashActive = false;
    }
  }
}

// ================= HP LED 렌더 =================
void renderHpLed() {
  uint32_t now = millis();

  if (hpFlashActive) {
    if (now - hpFlashStartMs < HIT_FLASH_MS) {
      fill_solid(hpLeds, HP_NUM_LEDS, CRGB::White);
      return;
    } else {
      hpFlashActive = false;
    }
  }

  if (now - lastBlinkMs >= BLINK_MS) {
    lastBlinkMs = now;
    blinkOn = !blinkOn;
  }

  if (hp <= 0) {
    if (now - lastDeadBlinkMs >= DEAD_BLINK_MS) {
      lastDeadBlinkMs = now;
      deadBlinkOn = !deadBlinkOn;
    }

    fill_solid(hpLeds, HP_NUM_LEDS, deadBlinkOn ? CRGB::Red : CRGB::Black);
    return;
  }

  int band = hpToBand(hp);
  int lit = stageHpToLit(hpToStageHp(hp));

  CRGB base = hpColor(band);
  CRGB blinkColor = nextHpColor(band);

  for (int i = 0; i < HP_NUM_LEDS; i++) {
    if (i < lit) {
      hpLeds[i] = base;
    }
    else if (blinkMask[i]) {
      hpLeds[i] = blinkOn ? blinkColor : CRGB::Black;
    }
    else {
      hpLeds[i] = CRGB::Black;
    }
  }
}

// ================= 데미지 =================
void applyDamage(int targetIndex) {
  if (hp <= 0) return;

  int oldHp = hp;
  int oldBand = hpToBand(hp);

  hp -= DAMAGE;
  if (hp < 0) hp = 0;

  int newBand = hpToBand(hp);

  if (newBand != oldBand) {
    clearBlinkMask();
  }

  if (hp > 0) {
    addBlinkSegment(oldHp, hp);
  }

  hpFlashActive = true;
  hpFlashStartMs = millis();

  targetFlashActive = true;
  targetFlashStartMs = millis();
  flashTarget = targetIndex;

  Serial.print("HIT TARGET ");
  Serial.print(targetIndex + 1);
  Serial.print(" / HP = ");
  Serial.println(hp);

  if (hp > 0) {
    selectNewTarget();
  }
}

void resetGame() {
  hp = HP_MAX;
  clearBlinkMask();

  hpFlashActive = false;
  targetFlashActive = false;
  deadBlinkOn = false;

  selectNewTarget();

  Serial.println("GAME RESET");
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  FastLED.addLeds<LED_TYPE, RING1_PIN, COLOR_ORDER>(ring1, RING_NUM_LEDS);
  FastLED.addLeds<LED_TYPE, RING2_PIN, COLOR_ORDER>(ring2, RING_NUM_LEDS);
  FastLED.addLeds<LED_TYPE, RING3_PIN, COLOR_ORDER>(ring3, RING_NUM_LEDS);
  FastLED.addLeds<LED_TYPE, RING4_PIN, COLOR_ORDER>(ring4, RING_NUM_LEDS);
  FastLED.addLeds<LED_TYPE, HP_LED_PIN, COLOR_ORDER>(hpLeds, HP_NUM_LEDS);

  FastLED.setBrightness(BRIGHTNESS);

  pinMode(PIEZO1_DO_PIN, INPUT_PULLDOWN);
  pinMode(PIEZO2_DO_PIN, INPUT_PULLDOWN);
  pinMode(PIEZO3_DO_PIN, INPUT_PULLDOWN);
  pinMode(PIEZO4_DO_PIN, INPUT_PULLDOWN);

  attachInterrupt(digitalPinToInterrupt(PIEZO1_DO_PIN), piezoISR0, RISING);
  attachInterrupt(digitalPinToInterrupt(PIEZO2_DO_PIN), piezoISR1, RISING);
  attachInterrupt(digitalPinToInterrupt(PIEZO3_DO_PIN), piezoISR2, RISING);
  attachInterrupt(digitalPinToInterrupt(PIEZO4_DO_PIN), piezoISR3, RISING);

  randomSeed(esp_random());

  clearBlinkMask();
  selectNewTarget();

  Serial.println("4 Target Piezo Boss HP Test Ready");
  Serial.println("r = reset");
}

// ================= LOOP =================
void loop() {
  uint32_t now = millis();

  if (hp > 0 && now - targetStartMs >= TARGET_DURATION_MS) {
    selectNewTarget();
  }

  for (int i = 0; i < 4; i++) {
    if (piezoTriggered[i]) {
      piezoTriggered[i] = false;

      if (now - lastHitMs[i] >= HIT_COOLDOWN_MS) {
        lastHitMs[i] = now;

        if (i == activeTarget && hp > 0) {
          applyDamage(i);
        } else {
          Serial.print("WRONG TARGET: ");
          Serial.println(i + 1);
        }
      }
    }
  }

  renderTargets();
  renderHpLed();
  showAll();

  if (Serial.available()) {
    char c = Serial.read();

    if (c == 'r' || c == 'R') {
      resetGame();
    }
  }
}