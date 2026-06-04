#include <Arduino.h>
#include <FastLED.h>

#define RELAY_PIN 26

#define MATRIX1_PIN 16
#define MATRIX2_PIN 17
#define MATRIX3_PIN 18
#define MATRIX4_PIN 19

#define RELAY_ON  HIGH
#define RELAY_OFF LOW

#define WIDTH 8
#define HEIGHT 8
#define NUM_LEDS 64
#define BRIGHTNESS 30

#define LED_TYPE WS2812B
#define COLOR_ORDER GRB

const unsigned long EFFECT_UPDATE_MS = 120;

// 효과 프레임 수
const int ARROW_FRAMES = 16;
const int RAINBOW_FRAMES = 28;

// 효과 시작 전 숨쉬기 효과 지속 시간 - bldc esc initialization time 고려
const unsigned long PRE_BLINK_DURATION_MS = 10000;

// 처음 4초 동안 켜진 상태에서 천천히 꺼짐
const unsigned long FADE_OUT_DURATION_MS = 4000;

// 그 다음 깜빡임 BPM
const uint16_t BLINK_BPM = 40;
const unsigned long BLINK_PERIOD_MS = 60000UL / BLINK_BPM;

// 프리 이펙트 업데이트 간격
const unsigned long PRE_EFFECT_UPDATE_MS = 20;

// 무지개 효과에서 색상 변화 속도
const uint8_t RAINBOW_SPEED = 8;

CRGB matrix1[NUM_LEDS];
CRGB matrix2[NUM_LEDS];
CRGB matrix3[NUM_LEDS];
CRGB matrix4[NUM_LEDS];

CRGB* matrices[4] = {
  matrix1, matrix2, matrix3, matrix4
};

bool ledState[4] = {
  false, false, false, false
};

bool relayState = false;
bool effectStarted = false;
bool preBlinkActive = false;

uint8_t rainbowOffset = 0;
uint16_t effectFrame = ARROW_FRAMES;   // 무지개부터 시작

unsigned long lastEffectUpdate = 0;
unsigned long preBlinkStartTime = 0;
unsigned long lastPreEffectUpdate = 0;

void allOff();
void checkSystem();
void setMatrix(int index, bool state);
void showNormalState();

void updatePreBlink();
void updateEffect();

void drawMovingArrow(int matrixIndex, int frame);
void drawRainbowTrail(int matrixIndex, int frame);
void drawFullRainbow(int matrixIndex);

bool isAllActive();

int xy(int x, int y);
void setVisualPixel(int matrixIndex, int col, int row, CRGB color);

void setup() {
  Serial.begin(115200);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_OFF);

  FastLED.addLeds<LED_TYPE, MATRIX1_PIN, COLOR_ORDER>(matrix1, NUM_LEDS);
  FastLED.addLeds<LED_TYPE, MATRIX2_PIN, COLOR_ORDER>(matrix2, NUM_LEDS);
  FastLED.addLeds<LED_TYPE, MATRIX3_PIN, COLOR_ORDER>(matrix3, NUM_LEDS);
  FastLED.addLeds<LED_TYPE, MATRIX4_PIN, COLOR_ORDER>(matrix4, NUM_LEDS);

  FastLED.setBrightness(BRIGHTNESS);

  allOff();

  Serial.println("1,2,3,4 : MATRIX ON");
  Serial.println("q,w,e,r : MATRIX OFF");
  Serial.println("f : ALL OFF");
}

void loop() {
  if (Serial.available()) {
    char cmd = Serial.read();

    if (cmd == '1') {
      setMatrix(0, true);
      Serial.println("MATRIX1 ON");
    }

    else if (cmd == '2') {
      setMatrix(1, true);
      Serial.println("MATRIX2 ON");
    }

    else if (cmd == '3') {
      setMatrix(2, true);
      Serial.println("MATRIX3 ON");
    }

    else if (cmd == '4') {
      setMatrix(3, true);
      Serial.println("MATRIX4 ON");
    }

    else if (cmd == 'q' || cmd == 'Q') {
      setMatrix(0, false);
      Serial.println("MATRIX1 OFF");
    }

    else if (cmd == 'w' || cmd == 'W') {
      setMatrix(1, false);
      Serial.println("MATRIX2 OFF");
    }

    else if (cmd == 'e' || cmd == 'E') {
      setMatrix(2, false);
      Serial.println("MATRIX3 OFF");
    }

    else if (cmd == 'r' || cmd == 'R') {
      setMatrix(3, false);
      Serial.println("MATRIX4 OFF");
    }

    else if (cmd == 'f' || cmd == 'F') {
      allOff();
      Serial.println("ALL OFF");
    }

    checkSystem();
  }

  if (isAllActive()) {
    if (preBlinkActive) {
      updatePreBlink();
    }
    else if (effectStarted) {
      updateEffect();
    }
  }
}

void setMatrix(int index, bool state) {
  ledState[index] = state;

  if (state) {
    fill_solid(matrices[index], NUM_LEDS, CRGB::Yellow);
  } else {
    fill_solid(matrices[index], NUM_LEDS, CRGB::Black);
  }

  FastLED.show();
}

void checkSystem() {
  bool allActive = isAllActive();

  digitalWrite(RELAY_PIN, allActive ? RELAY_ON : RELAY_OFF);

  if (allActive && !relayState) {
    Serial.println("ALL MATRICES ACTIVE");
    Serial.println("RELAY ON");
    Serial.println("BREATH START");

    preBlinkActive = true;
    effectStarted = false;

    preBlinkStartTime = millis();
    lastPreEffectUpdate = 0;
    lastEffectUpdate = 0;

    // 숨쉬기 이후 무지개부터 시작
    effectFrame = ARROW_FRAMES;
    rainbowOffset = 0;
  }

  if (!allActive && relayState) {
    Serial.println("RELAY OFF");

    preBlinkActive = false;
    effectStarted = false;
    showNormalState();
  }

  relayState = allActive;
}

void updatePreBlink() {
  unsigned long now = millis();

  unsigned long elapsed =
      now - preBlinkStartTime;

  // 총 10초 끝나면 무지개/화살표 이펙트 시작
  if (elapsed >= PRE_BLINK_DURATION_MS) {
    preBlinkActive = false;
    effectStarted = true;

    lastEffectUpdate = 0;

    Serial.println("EFFECT START");
    return;
  }

  if (now - lastPreEffectUpdate < PRE_EFFECT_UPDATE_MS) {
    return;
  }

  lastPreEffectUpdate = now;

  uint8_t brightness = 0;

  // 0~4초: 켜진 상태에서 천천히 Fade Out
  if (elapsed < FADE_OUT_DURATION_MS) {
    float progress =
        elapsed / (float)FADE_OUT_DURATION_MS;

    // 부드러운 감속 Fade Out
    float fade =
        (cos(progress * PI) + 1.0f) * 0.5f;

    brightness =
        fade * 255;
  }

  // 4~10초: 깜빡임
  else {
    unsigned long blinkElapsed =
        elapsed - FADE_OUT_DURATION_MS;

    bool blinkOn =
        (blinkElapsed % BLINK_PERIOD_MS) <
        (BLINK_PERIOD_MS / 2);

    brightness =
        blinkOn ? 255 : 0;
  }

  for (int i = 0; i < 4; i++) {
    fill_solid(
      matrices[i],
      NUM_LEDS,
      CHSV(
        45,          // 노란색
        255,
        brightness
      )
    );
  }

  FastLED.show();
}

void updateEffect() {
  unsigned long now = millis();

  if (now - lastEffectUpdate < EFFECT_UPDATE_MS) {
    return;
  }

  lastEffectUpdate = now;

  int cycleFrame =
      effectFrame % (ARROW_FRAMES + RAINBOW_FRAMES);

  bool arrowMode = cycleFrame < ARROW_FRAMES;

  for (int m = 0; m < 4; m++) {
    fill_solid(matrices[m], NUM_LEDS, CRGB::Black);

    if (arrowMode) {
      drawMovingArrow(m, cycleFrame);
      drawRainbowTrail(m, cycleFrame);
    } else {
      drawFullRainbow(m);
    }
  }

  FastLED.show();

  rainbowOffset += RAINBOW_SPEED;
  effectFrame++;
}

bool isAllActive() {
  return ledState[0] &&
         ledState[1] &&
         ledState[2] &&
         ledState[3];
}

void showNormalState() {
  for (int i = 0; i < 4; i++) {
    if (ledState[i]) {
      fill_solid(matrices[i], NUM_LEDS, CRGB::Yellow);
    } else {
      fill_solid(matrices[i], NUM_LEDS, CRGB::Black);
    }
  }

  FastLED.show();
}

void drawMovingArrow(int matrixIndex, int frame) {
  int baseRow = HEIGHT - 1 - frame;

  const int arrow[][2] = {
    {3,0}, {4,0},

    {2,1}, {3,1}, {4,1}, {5,1},

    {1,2}, {2,2}, {3,2}, {4,2}, {5,2}, {6,2},

    {3,3}, {4,3},
    {3,4}, {4,4},
    {3,5}, {4,5},
    {3,6}, {4,6}
  };

  const int arrowSize =
      sizeof(arrow) / sizeof(arrow[0]);

  for (int i = 0; i < arrowSize; i++) {
    int col = arrow[i][0];
    int row = arrow[i][1] + baseRow;

    if (row >= 0 && row < HEIGHT) {
      setVisualPixel(matrixIndex, col, row, CRGB::White);
    }
  }
}

void drawRainbowTrail(int matrixIndex, int frame) {
  int baseRow = HEIGHT - 1 - frame;

  const int TRAIL_LENGTH = 8;

  for (int trail = 1; trail <= TRAIL_LENGTH; trail++) {
    int row = baseRow + trail + 6;

    if (row < 0 || row >= HEIGHT) {
      continue;
    }

    uint8_t hue =
        rainbowOffset +
        trail * 25;

    for (int col = 0; col < WIDTH; col++) {
      setVisualPixel(
          matrixIndex,
          col,
          row,
          CHSV(hue, 255, 255)
      );
    }
  }
}

void drawFullRainbow(int matrixIndex) {
  for (int y = 0; y < HEIGHT; y++) {
    for (int x = 0; x < WIDTH; x++) {
      uint8_t hue =
          rainbowOffset +
          (WIDTH - 1 - x) * 20;

      matrices[matrixIndex][xy(x, y)] =
          CHSV(hue, 255, 255);
    }
  }
}

void allOff() {
  for (int i = 0; i < 4; i++) {
    ledState[i] = false;
    fill_solid(matrices[i], NUM_LEDS, CRGB::Black);
  }

  relayState = false;
  preBlinkActive = false;
  effectStarted = false;

  digitalWrite(RELAY_PIN, RELAY_OFF);
  FastLED.show();
}

void setVisualPixel(
    int matrixIndex,
    int col,
    int row,
    CRGB color) {

  // 현재 매트릭스 방향 보정 유지
  int x = WIDTH - 1 - row;
  int y = col;

  matrices[matrixIndex][xy(x, y)] = color;
}

int xy(int x, int y) {
  if (y % 2 == 0) {
    return y * WIDTH + x;
  } else {
    return y * WIDTH + (WIDTH - 1 - x);
  }
}