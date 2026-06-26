#define FASTLED_ALLOW_INTERRUPTS 0
#include <FastLED.h>

// ================= LED 설정 =================
#define LED_TYPE        WS2811
#define COLOR_ORDER     RGB
#define BRIGHTNESS      80

#define RING_NUM_LEDS   60

// HP바: 실제 300 LED, 논리적으로 100칸
#define HP_SEGMENTS     100
#define HP_NUM_LEDS     300

#define RING1_PIN       23
#define RING2_PIN       21
#define RING3_PIN       18
#define RING4_PIN       17

// D4 = GPIO4 기준
// HP바를 다시 12번으로 쓰려면 4를 12로 변경
#define HP_LED_PIN      12

// ================= 피에조 AO 핀 =================
#define PIEZO1_AO_PIN   34
#define PIEZO2_AO_PIN   35
#define PIEZO3_AO_PIN   32
#define PIEZO4_AO_PIN   33

#define HIT_THRESHOLD       3000
#define HIT_COOLDOWN_MS     300
#define PIEZO_READ_GAP_MS   5

// ================= HP 설정 =================
#define HP_MAX          3000
#define HP_STAGE        1000
#define DAMAGE          300

// ================= LED 배열 =================
CRGB ring1[RING_NUM_LEDS];
CRGB ring2[RING_NUM_LEDS];
CRGB ring3[RING_NUM_LEDS];
CRGB ring4[RING_NUM_LEDS];

CRGB hpLeds[HP_NUM_LEDS];

// ================= FastLED 컨트롤러 =================
CLEDController* ringCtrl[4];
CLEDController* hpCtrl;

// ================= 상태 =================
bool ringState[4] = {false, false, false, false};

int hp = HP_MAX;

uint32_t lastGlobalHitMs = 0;
uint32_t lastPiezoReadMs = 0;

// ================= 링 함수 =================
CRGB* getRing(int index) {
  if (index == 0) return ring1;
  if (index == 1) return ring2;
  if (index == 2) return ring3;
  return ring4;
}

void setRingBuffer(int index, CRGB color) {
  fill_solid(getRing(index), RING_NUM_LEDS, color);
}

void showRing(int index) {
  if (index < 0 || index >= 4) return;

  ringCtrl[index]->showLeds(BRIGHTNESS);
  delayMicroseconds(300);
}

void showHp() {
  hpCtrl->showLeds(BRIGHTNESS);
  delayMicroseconds(300);
}

void showAllOnce() {
  for (int i = 0; i < 4; i++) {
    showRing(i);
  }

  showHp();
}

void toggleRing(int index) {
  if (index < 0 || index >= 4) return;

  ringState[index] = !ringState[index];

  setRingBuffer(index, ringState[index] ? CRGB::White : CRGB::Black);
}

// ================= HP 매핑 =================
// HP 논리 칸 0~99를 실제 300 LED에 매핑
// seg 0 -> LED 1, 200, 201
// seg 1 -> LED 2, 199, 202
// 0-index 기준:
// seg 0 -> 0, 199, 200
// seg 1 -> 1, 198, 201
void setHpSegment(int seg, CRGB color) {
  if (seg < 0 || seg >= HP_SEGMENTS) return;

  int led1 = seg;          // 1~100 정방향
  int led2 = 199 - seg;    // 200~101 역방향
  int led3 = 200 + seg;    // 201~300 정방향

  hpLeds[led1] = color;
  hpLeds[led2] = color;
  hpLeds[led3] = color;
}

// ================= HP 함수 =================
int getStageHp() {
  if (hp <= 0) return 0;

  int stageHp = hp % HP_STAGE;

  if (stageHp == 0) {
    stageHp = HP_STAGE;
  }

  return stageHp;
}

CRGB getHpColor() {
  if (hp > 2000) {
    return CRGB::Green;
  } else if (hp > 1000) {
    return CRGB::Yellow;
  } else if (hp > 0) {
    return CRGB::Red;
  } else {
    return CRGB::Black;
  }
}

void drawHpBuffer() {
  fill_solid(hpLeds, HP_NUM_LEDS, CRGB::Black);

  if (hp <= 0) {
    return;
  }

  int stageHp = getStageHp();
  int litSegments = (long)stageHp * HP_SEGMENTS / HP_STAGE;

  CRGB color = getHpColor();

  for (int seg = 0; seg < litSegments; seg++) {
    setHpSegment(seg, color);
  }
}

void applyDamage() {
  if (hp <= 0) return;

  hp -= DAMAGE;

  if (hp < 0) {
    hp = 0;
  }

  drawHpBuffer();
}

// ================= 피에조 처리 =================
void handlePiezo() {
  uint32_t now = millis();

  if (now - lastPiezoReadMs < PIEZO_READ_GAP_MS) {
    return;
  }

  lastPiezoReadMs = now;

  int value[4];

  value[0] = analogRead(PIEZO1_AO_PIN);
  value[1] = analogRead(PIEZO2_AO_PIN);
  value[2] = analogRead(PIEZO3_AO_PIN);
  value[3] = analogRead(PIEZO4_AO_PIN);

  int maxIndex = -1;
  int maxValue = 0;

  for (int i = 0; i < 4; i++) {
    if (value[i] > maxValue) {
      maxValue = value[i];
      maxIndex = i;
    }
  }

  if (maxValue >= HIT_THRESHOLD) {
    if (now - lastGlobalHitMs >= HIT_COOLDOWN_MS) {
      lastGlobalHitMs = now;

      toggleRing(maxIndex);
      applyDamage();

      // 여기서도 전체 show 금지
      // 바뀐 링만 출력
      showRing(maxIndex);

      // HP만 출력
      showHp();

      Serial.print("PIEZO ");
      Serial.print(maxIndex + 1);
      Serial.print(" HIT / ADC : ");
      Serial.print(maxValue);
      Serial.print(" / HP : ");
      Serial.println(hp);
    }
  }
}

// ================= 리셋 =================
void resetAll() {
  hp = HP_MAX;

  for (int i = 0; i < 4; i++) {
    ringState[i] = false;
    setRingBuffer(i, CRGB::Black);
  }

  lastGlobalHitMs = 0;
  lastPiezoReadMs = 0;

  drawHpBuffer();
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  analogReadResolution(12);

  pinMode(PIEZO1_AO_PIN, INPUT);
  pinMode(PIEZO2_AO_PIN, INPUT);
  pinMode(PIEZO3_AO_PIN, INPUT);
  pinMode(PIEZO4_AO_PIN, INPUT);

  ringCtrl[0] = &FastLED.addLeds<LED_TYPE, RING1_PIN, COLOR_ORDER>(ring1, RING_NUM_LEDS);
  ringCtrl[1] = &FastLED.addLeds<LED_TYPE, RING2_PIN, COLOR_ORDER>(ring2, RING_NUM_LEDS);
  ringCtrl[2] = &FastLED.addLeds<LED_TYPE, RING3_PIN, COLOR_ORDER>(ring3, RING_NUM_LEDS);
  ringCtrl[3] = &FastLED.addLeds<LED_TYPE, RING4_PIN, COLOR_ORDER>(ring4, RING_NUM_LEDS);

  hpCtrl = &FastLED.addLeds<LED_TYPE, HP_LED_PIN, COLOR_ORDER>(hpLeds, HP_NUM_LEDS);

  FastLED.setBrightness(BRIGHTNESS);
  FastLED.setDither(0);

  resetAll();
  showAllOnce();

  Serial.println("===== Ring + HP + Piezo Separate Controller Test =====");
  Serial.println("1 : Toggle Ring 1 White");
  Serial.println("2 : Toggle Ring 2 White");
  Serial.println("3 : Toggle Ring 3 White");
  Serial.println("4 : Toggle Ring 4 White");
  Serial.println("5 : HP Damage -300");
  Serial.println("Piezo ADC >= 3000 : Toggle Ring + HP Damage -300");
  Serial.println("r : Reset");
}

// ================= LOOP =================
void loop() {
  handlePiezo();

  if (Serial.available()) {
    char c = Serial.read();

    if (c >= '1' && c <= '4') {
      int idx = c - '1';

      toggleRing(idx);

      // 링만 출력
      // HP는 건드리지 않음
      showRing(idx);

      Serial.print("Ring ");
      Serial.print(idx + 1);
      Serial.println(ringState[idx] ? " ON" : " OFF");
    }

    else if (c == '5') {
      applyDamage();

      // HP만 출력
      // 링은 건드리지 않음
      showHp();

      Serial.print("HP : ");
      Serial.println(hp);
    }

    else if (c == 'r' || c == 'R') {
      resetAll();

      // 리셋 때만 전체 출력
      showAllOnce();

      Serial.println("RESET");
    }
  }
}