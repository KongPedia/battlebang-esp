// HIT 순간 깨짐 줄이기용
#define FASTLED_ALLOW_INTERRUPTS 0

#include <FastLED.h>
#include "esp_system.h"

#define NUM_SETS      4
#define NUM_LEDS      60

// 4개 원형 링 LED 핀
#define RING1_PIN     23
#define RING2_PIN     21
#define RING3_PIN     18
#define RING4_PIN     17

// HP바 LED 핀
#define HP_BAR_PIN    12
#define HP_LEDS       300

// 300개 LED를 3개씩 묶어서 100칸 HP바처럼 사용
#define HP_SEGMENTS   100

// 피에조 AO 핀 4개
#define PIEZO_AO_1    34
#define PIEZO_AO_2    35
#define PIEZO_AO_3    32
#define PIEZO_AO_4    33

#define LED_TYPE          WS2812B

// 링 LED 색상 순서
#define RING_COLOR_ORDER  GRB

// HP바 빨강/초록 반대 수정용
#define HP_COLOR_ORDER    RGB

#define BRIGHTNESS        80

// 피에조 감지 기준
#define ADC_HIT_THRESHOLD      3000
#define ADC_RELEASE_THRESHOLD  1200

// 0이면 가장 큰 ADC 채널이 현재 타겟이면 인정
// 오검출 심하면 200~500 정도로 올려보기
#define ADC_WIN_MARGIN         0

// HP 설정
#define HP_MAX        1000
#define HP_DAMAGE     50

#define GLOBAL_LOCKOUT_TIME    250
#define ADC_SAMPLE_INTERVAL    2
#define ADC_SETTLE_US          80

#define HP_DEAD_BLINK_INTERVAL 300

// 랜덤 타겟 설정
#define TARGET_DURATION        5000  // ms, 한 타겟 유지 시간

// HIT 순간 바로 LED show하지 않고 잠깐 기다린 뒤 업데이트
#define HIT_LED_UPDATE_DELAY   30    // ms
#define LED_SHOW_GAP_DELAY     5     // ms

// HIT 성공 시 링 깜빡임
#define HIT_BLINK_INTERVAL     120   // ms
#define HIT_BLINK_TOGGLES      6     // 6번 토글 = 약 3번 깜빡임

CRGB ringLeds[NUM_SETS][NUM_LEDS];
CRGB hpLeds[HP_LEDS];

CLEDController* ringCtrl[NUM_SETS];
CLEDController* hpCtrl;

const int piezoPins[NUM_SETS] = {
  PIEZO_AO_1,
  PIEZO_AO_2,
  PIEZO_AO_3,
  PIEZO_AO_4
};

int adcValue[NUM_SETS] = {
  0, 0, 0, 0
};

bool globalArmed = true;

unsigned long lastAdcSampleTime = 0;
unsigned long lastHitTime = 0;

int currentHp = HP_MAX;
bool hpDead = false;
bool hpBlinkOn = false;
unsigned long lastHpBlinkTime = 0;

// 랜덤 타겟 상태
int currentTarget = -1;
bool targetActive = false;
unsigned long targetStartTime = 0;

// HIT 예약 처리용
bool hitPending = false;
int pendingHitIndex = 0;
int pendingHitAdc = 0;
unsigned long pendingHitTime = 0;

// HIT 성공 링 깜빡임 상태
bool hitBlinking = false;
int blinkRingIndex = -1;
bool blinkOn = false;
int blinkToggleCount = 0;
unsigned long lastBlinkTime = 0;

int readAdcStable(int pin) {
  // ESP32 ADC mux 잔류값 제거용 dummy read
  analogRead(pin);
  delayMicroseconds(ADC_SETTLE_US);

  int sum = 0;

  for (int i = 0; i < 3; i++) {
    sum += analogRead(pin);
    delayMicroseconds(20);
  }

  return sum / 3;
}

void showRingOnly(int index) {
  ringCtrl[index]->showLeds(BRIGHTNESS);
}

void showHpOnly() {
  hpCtrl->showLeds(BRIGHTNESS);
}

void setRingBuffer(int index, CRGB color) {
  fill_solid(ringLeds[index], NUM_LEDS, color);
}

void showAllRings() {
  for (int i = 0; i < NUM_SETS; i++) {
    showRingOnly(i);
    delay(3);
  }
}

void showAllRingsOff() {
  for (int i = 0; i < NUM_SETS; i++) {
    setRingBuffer(i, CRGB::Black);
  }

  showAllRings();
}

void showTargetRing() {
  for (int i = 0; i < NUM_SETS; i++) {
    setRingBuffer(i, CRGB::Black);
  }

  if (currentTarget >= 0 && currentTarget < NUM_SETS) {
    setRingBuffer(currentTarget, CRGB::Red);
  }

  showAllRings();
}

void chooseNewTarget(unsigned long now) {
  if (hpDead) {
    return;
  }

  int oldTarget = currentTarget;
  int newTarget = random(NUM_SETS);

  if (NUM_SETS > 1) {
    while (newTarget == oldTarget) {
      newTarget = random(NUM_SETS);
    }
  }

  currentTarget = newTarget;
  targetStartTime = now;
  targetActive = true;

  showTargetRing();

  Serial.print("NEW TARGET: SET ");
  Serial.println(currentTarget + 1);
}

// 300개 물리 LED 중 3개를 1개의 HP칸으로 묶는 함수
// 사용자 기준:
// 1번 칸 = LED 1, LED 200, LED 201
// 2번 칸 = LED 2, LED 199, LED 202
// 3번 칸 = LED 3, LED 198, LED 203
// ...
// 100번 칸 = LED 100, LED 101, LED 300
void setHpSegment(int segmentIndex, CRGB color) {
  // segmentIndex: 0 ~ 99

  int ledA = segmentIndex;        // 0, 1, 2, ... 99
  int ledB = 199 - segmentIndex;  // 199, 198, ... 100
  int ledC = 200 + segmentIndex;  // 200, 201, ... 299

  hpLeds[ledA] = color;
  hpLeds[ledB] = color;
  hpLeds[ledC] = color;
}

void drawHpBarBufferOnly() {
  if (hpDead) {
    return;
  }

  int litSegments = 0;

  if (currentHp > 0) {
    litSegments = (currentHp * HP_SEGMENTS + HP_MAX - 1) / HP_MAX;
  }

  litSegments = constrain(litSegments, 0, HP_SEGMENTS);

  for (int i = 0; i < HP_SEGMENTS; i++) {
    // HP 감소 방향: 우 -> 좌
    if (i >= HP_SEGMENTS - litSegments) {
      setHpSegment(i, CRGB::Green);
    } else {
      setHpSegment(i, CRGB::Black);
    }
  }
}

void drawHpBar() {
  drawHpBarBufferOnly();
  showHpOnly();
}

void startDeathBlinkBufferOnly(unsigned long now) {
  hpDead = true;
  hpBlinkOn = true;
  lastHpBlinkTime = now;

  targetActive = false;
  hitPending = false;

  fill_solid(hpLeds, HP_LEDS, CRGB::Red);
}

void updateDeathBlink(unsigned long now) {
  if (!hpDead) {
    return;
  }

  if (now - lastHpBlinkTime >= HP_DEAD_BLINK_INTERVAL) {
    lastHpBlinkTime = now;
    hpBlinkOn = !hpBlinkOn;

    if (hpBlinkOn) {
      fill_solid(hpLeds, HP_LEDS, CRGB::Red);
    } else {
      fill_solid(hpLeds, HP_LEDS, CRGB::Black);
    }

    showHpOnly();
  }
}

void startHitBlink(int index, unsigned long now) {
  hitBlinking = true;
  blinkRingIndex = index;
  blinkOn = true;
  blinkToggleCount = 0;
  lastBlinkTime = now;

  setRingBuffer(index, CRGB::White);
  showRingOnly(index);
}

void updateHitBlink(unsigned long now) {
  if (!hitBlinking) {
    return;
  }

  if (now - lastBlinkTime < HIT_BLINK_INTERVAL) {
    return;
  }

  lastBlinkTime = now;
  blinkOn = !blinkOn;
  blinkToggleCount++;

  if (blinkOn) {
    setRingBuffer(blinkRingIndex, CRGB::White);
  } else {
    setRingBuffer(blinkRingIndex, CRGB::Black);
  }

  showRingOnly(blinkRingIndex);

  if (blinkToggleCount >= HIT_BLINK_TOGGLES) {
    hitBlinking = false;

    showAllRingsOff();

    if (!hpDead) {
      chooseNewTarget(now);
    }
  }
}

bool allPiezoReleased() {
  for (int i = 0; i < NUM_SETS; i++) {
    if (adcValue[i] > ADC_RELEASE_THRESHOLD) {
      return false;
    }
  }

  return true;
}

void printAdcValues() {
  Serial.print("adc = ");
  Serial.print(adcValue[0]);
  Serial.print(", ");
  Serial.print(adcValue[1]);
  Serial.print(", ");
  Serial.print(adcValue[2]);
  Serial.print(", ");
  Serial.println(adcValue[3]);
}

void queueHit(int hitIndex, int hitAdc, unsigned long now) {
  if (hitPending) {
    return;
  }

  hitPending = true;
  pendingHitIndex = hitIndex;
  pendingHitAdc = hitAdc;
  pendingHitTime = now;

  Serial.print("SET ");
  Serial.print(hitIndex + 1);
  Serial.print(" hit queued adc: ");
  Serial.println(hitAdc);
}

void processPendingHit(unsigned long now) {
  if (!hitPending) {
    return;
  }

  // 피에조 충격 직후 노이즈가 가라앉을 시간
  if (now - pendingHitTime < HIT_LED_UPDATE_DELAY) {
    return;
  }

  hitPending = false;

  if (hpDead) {
    return;
  }

  int hitIndex = pendingHitIndex;
  int hitAdc = pendingHitAdc;

  // 현재 켜진 타겟이 아니면 무시
  if (!targetActive || hitIndex != currentTarget) {
    Serial.print("IGNORED HIT / target SET ");
    Serial.print(currentTarget + 1);
    Serial.print(" / hit SET ");
    Serial.println(hitIndex + 1);
    return;
  }

  targetActive = false;

  currentHp -= HP_DAMAGE;

  if (currentHp < 0) {
    currentHp = 0;
  }

  Serial.print("CORRECT HIT SET ");
  Serial.print(hitIndex + 1);
  Serial.print(" / damage ");
  Serial.print(HP_DAMAGE);
  Serial.print(" / adc ");
  Serial.print(hitAdc);
  Serial.print(" / HP: ");
  Serial.print(currentHp);
  Serial.print(" / ");
  Serial.println(HP_MAX);

  if (currentHp <= 0) {
    startDeathBlinkBufferOnly(now);
  } else {
    drawHpBarBufferOnly();
  }

  // 중요:
  // HIT 순간 깨짐 줄이기 위해 HP바 먼저 출력
  showHpOnly();

  delay(LED_SHOW_GAP_DELAY);

  // 그 다음 맞은 링만 흰색 깜빡임 시작
  startHitBlink(hitIndex, millis());

  if (currentHp <= 0) {
    Serial.println("HP DEAD");
  }
}

void simulateTargetHit(unsigned long now) {
  if (hpDead) {
    return;
  }

  if (!targetActive) {
    return;
  }

  if (hitPending || hitBlinking) {
    return;
  }

  Serial.println("SERIAL TEST HIT");

  lastHitTime = now;
  globalArmed = false;

  queueHit(currentTarget, 4095, now);
}

void handlePiezo(unsigned long now) {
  if (hpDead) {
    return;
  }

  if (!targetActive) {
    return;
  }

  if (hitPending || hitBlinking) {
    return;
  }

  if (now - lastAdcSampleTime < ADC_SAMPLE_INTERVAL) {
    return;
  }

  lastAdcSampleTime = now;

  // 4개 ADC 읽기
  for (int i = 0; i < NUM_SETS; i++) {
    adcValue[i] = readAdcStable(piezoPins[i]);
  }

  // 한 번 HIT 후에는 모든 피에조 값이 충분히 내려와야 다시 감지
  if (!globalArmed) {
    if (allPiezoReleased()) {
      globalArmed = true;
    }

    return;
  }

  if (now - lastHitTime < GLOBAL_LOCKOUT_TIME) {
    return;
  }

  // 4개 중 가장 큰 ADC 값 찾기
  int maxIndex = 0;
  int maxValue = adcValue[0];
  int secondValue = 0;

  for (int i = 1; i < NUM_SETS; i++) {
    if (adcValue[i] > maxValue) {
      secondValue = maxValue;
      maxValue = adcValue[i];
      maxIndex = i;
    } else if (adcValue[i] > secondValue) {
      secondValue = adcValue[i];
    }
  }

  if (maxValue > ADC_HIT_THRESHOLD) {
    lastHitTime = now;
    globalArmed = false;

    Serial.print("HIT CANDIDATE / target SET ");
    Serial.print(currentTarget + 1);
    Serial.print(" / max SET ");
    Serial.print(maxIndex + 1);
    Serial.print(" / max adc ");
    Serial.print(maxValue);
    Serial.print(" / second ");
    Serial.print(secondValue);
    Serial.print(" / ");
    printAdcValues();

    // 현재 켜진 타겟 링의 피에조가 가장 크게 들어왔을 때만 인정
    if (maxIndex == currentTarget && (maxValue - secondValue >= ADC_WIN_MARGIN)) {
      queueHit(maxIndex, maxValue, now);
    } else {
      Serial.print("WRONG OR AMBIGUOUS HIT IGNORED / target SET ");
      Serial.print(currentTarget + 1);
      Serial.print(" / detected SET ");
      Serial.println(maxIndex + 1);
    }
  }
}

void updateTarget(unsigned long now) {
  if (hpDead) {
    return;
  }

  if (hitPending || hitBlinking) {
    return;
  }

  if (!targetActive) {
    chooseNewTarget(now);
    return;
  }

  if (now - targetStartTime >= TARGET_DURATION) {
    Serial.print("TARGET TIMEOUT: SET ");
    Serial.println(currentTarget + 1);

    chooseNewTarget(now);
  }
}

void resetGame() {
  currentHp = HP_MAX;
  hpDead = false;
  hpBlinkOn = false;

  globalArmed = true;
  hitPending = false;
  hitBlinking = false;

  lastHitTime = 0;
  currentTarget = -1;
  targetActive = false;

  showAllRingsOff();
  drawHpBar();

  chooseNewTarget(millis());

  Serial.println("RESET");
  Serial.print("HP: ");
  Serial.print(currentHp);
  Serial.print(" / ");
  Serial.println(HP_MAX);
}

void handleSerial(unsigned long now) {
  if (Serial.available() > 0) {
    char c = Serial.read();

    if (c == 'r' || c == 'R') {
      resetGame();
    }

    if (c == 'f' || c == 'F') {
      simulateTargetHit(now);
    }

    if (c == 'p' || c == 'P') {
      for (int i = 0; i < NUM_SETS; i++) {
        adcValue[i] = readAdcStable(piezoPins[i]);
      }

      printAdcValues();
    }
  }
}

void setup() {
  Serial.begin(115200);

  randomSeed(esp_random());

  ringCtrl[0] = &FastLED.addLeds<LED_TYPE, RING1_PIN, RING_COLOR_ORDER>(ringLeds[0], NUM_LEDS);
  ringCtrl[1] = &FastLED.addLeds<LED_TYPE, RING2_PIN, RING_COLOR_ORDER>(ringLeds[1], NUM_LEDS);
  ringCtrl[2] = &FastLED.addLeds<LED_TYPE, RING3_PIN, RING_COLOR_ORDER>(ringLeds[2], NUM_LEDS);
  ringCtrl[3] = &FastLED.addLeds<LED_TYPE, RING4_PIN, RING_COLOR_ORDER>(ringLeds[3], NUM_LEDS);

  hpCtrl = &FastLED.addLeds<LED_TYPE, HP_BAR_PIN, HP_COLOR_ORDER>(hpLeds, HP_LEDS);

  FastLED.setBrightness(BRIGHTNESS);

  analogReadResolution(12);

  for (int i = 0; i < NUM_SETS; i++) {
    pinMode(piezoPins[i], INPUT);
    analogSetPinAttenuation(piezoPins[i], ADC_11db);
  }

  resetGame();

  Serial.println("Random Target Game Start");
  Serial.println("One ring turns ON for 5 seconds");
  Serial.println("Hit correct piezo -> blink + HP -50");
  Serial.println("Serial 'f' -> simulate current target hit");
  Serial.println("Serial 'r' -> reset");
  Serial.println("Serial 'p' -> print ADC");
}

void loop() {
  unsigned long now = millis();

  handleSerial(now);
  updateDeathBlink(now);
  updateHitBlink(now);
  processPendingHit(now);
  updateTarget(now);
  handlePiezo(now);
}