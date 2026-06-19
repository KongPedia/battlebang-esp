#include <Arduino.h>
#include <FastLED.h>
#include <BluetoothSerial.h>

// ==================================================
// Bluetooth Classic SPP 지원 확인
// ==================================================

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error "Bluetooth is not enabled for this ESP32 target."
#endif

#if !defined(CONFIG_BT_SPP_ENABLED)
#error "Bluetooth SPP is only available on the original ESP32."
#endif

// ==================================================
// 핀 설정
// ==================================================

#define RELAY_PIN 26

#define MATRIX1_PIN 23
#define MATRIX2_PIN 22
#define MATRIX3_PIN 21
#define MATRIX4_PIN 19

#define RELAY_ON  HIGH
#define RELAY_OFF LOW

// 릴레이가 반대로 동작하면 아래처럼 변경
// #define RELAY_ON  LOW
// #define RELAY_OFF HIGH

// ==================================================
// WS2812B 설정
// ==================================================

#define WIDTH       8
#define HEIGHT      8
#define NUM_LEDS    64
#define BRIGHTNESS  30

#define LED_TYPE     WS2812B
#define COLOR_ORDER  GRB

// ==================================================
// Bluetooth 설정
// ==================================================

const char* BT_DEVICE_NAME = "ESP32_LED_CTRL";

BluetoothSerial SerialBT;

bool bluetoothReady = false;
bool btClientConnected = false;

// ==================================================
// 효과 설정
// ==================================================

// 무지개/화살표 프레임 갱신 간격
const unsigned long EFFECT_UPDATE_MS = 120;

// 화살표와 무지개 유지 프레임
const uint16_t ARROW_FRAMES = 16;
const uint16_t RAINBOW_FRAMES = 28;

// 효과 시작 전 준비 단계 전체 시간
const unsigned long PRE_BLINK_DURATION_MS = 10000;

// 처음 4초 동안 켜진 상태에서 천천히 꺼짐
const unsigned long FADE_OUT_DURATION_MS = 4000;

// 4초 이후 깜빡임 속도
const uint16_t BLINK_BPM = 40;
const unsigned long BLINK_PERIOD_MS =
    60000UL / BLINK_BPM;

// 페이드 효과 업데이트 간격
const unsigned long PRE_EFFECT_UPDATE_MS = 20;

// 무지개 색상 이동 속도
const uint8_t RAINBOW_SPEED = 8;

// ==================================================
// LED 데이터
// ==================================================

CRGB matrix1[NUM_LEDS];
CRGB matrix2[NUM_LEDS];
CRGB matrix3[NUM_LEDS];
CRGB matrix4[NUM_LEDS];

CRGB* matrices[4] = {
  matrix1,
  matrix2,
  matrix3,
  matrix4
};

bool ledState[4] = {
  false,
  false,
  false,
  false
};

// ==================================================
// 시스템 상태
// ==================================================

bool relayState = false;
bool effectStarted = false;
bool preBlinkActive = false;

uint8_t rainbowOffset = 0;

// ARROW_FRAMES부터 시작하므로 첫 효과는 무지개
uint32_t effectFrame = ARROW_FRAMES;

unsigned long lastEffectUpdate = 0;
unsigned long preBlinkStartTime = 0;
unsigned long lastPreEffectUpdate = 0;

// ==================================================
// 함수 선언
// ==================================================

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

void setVisualPixel(
    int matrixIndex,
    int col,
    int row,
    CRGB color
);

// Bluetooth/시리얼 관련
void processInput(Stream& input);
void handleCommand(char cmd);

void sendLine(const char* message);
void printHelp();

void updateBluetoothConnection();

// ==================================================
// Setup
// ==================================================

void setup() {
  Serial.begin(115200);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_OFF);

  FastLED.addLeds<
      LED_TYPE,
      MATRIX1_PIN,
      COLOR_ORDER
  >(matrix1, NUM_LEDS);

  FastLED.addLeds<
      LED_TYPE,
      MATRIX2_PIN,
      COLOR_ORDER
  >(matrix2, NUM_LEDS);

  FastLED.addLeds<
      LED_TYPE,
      MATRIX3_PIN,
      COLOR_ORDER
  >(matrix3, NUM_LEDS);

  FastLED.addLeds<
      LED_TYPE,
      MATRIX4_PIN,
      COLOR_ORDER
  >(matrix4, NUM_LEDS);

  FastLED.setBrightness(BRIGHTNESS);

  allOff();

  // Bluetooth SPP 시작
  bluetoothReady =
      SerialBT.begin(BT_DEVICE_NAME);

  Serial.println();
  Serial.println("==============================");

  if (bluetoothReady) {
    Serial.println("Bluetooth SPP STARTED");
    Serial.print("Bluetooth name: ");
    Serial.println(BT_DEVICE_NAME);
  }
  else {
    Serial.println("Bluetooth SPP START FAILED");
  }

  Serial.println("==============================");

  printHelp();
}

// ==================================================
// Main Loop
// ==================================================

void loop() {
  updateBluetoothConnection();

  // USB 시리얼 명령
  processInput(Serial);

  // Bluetooth 시리얼 명령
  if (bluetoothReady) {
    processInput(SerialBT);
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

// ==================================================
// Bluetooth 연결 상태
// ==================================================

void updateBluetoothConnection() {
  if (!bluetoothReady) {
    return;
  }

  bool connected = SerialBT.hasClient();

  if (connected && !btClientConnected) {
    btClientConnected = true;

    Serial.println("BLUETOOTH CLIENT CONNECTED");

    SerialBT.println();
    SerialBT.println("==============================");
    SerialBT.println("ESP32 LED CONTROLLER CONNECTED");
    SerialBT.println("==============================");

    printHelp();
  }

  else if (!connected && btClientConnected) {
    btClientConnected = false;

    Serial.println("BLUETOOTH CLIENT DISCONNECTED");
  }
}

// ==================================================
// 입력 처리
// ==================================================

void processInput(Stream& input) {
  while (input.available() > 0) {
    char cmd = static_cast<char>(input.read());

    handleCommand(cmd);
  }
}

void handleCommand(char cmd) {
  // Tera Term에서 함께 들어오는 개행 문자 무시
  if (cmd == '\r' ||
      cmd == '\n' ||
      cmd == '\t' ||
      cmd == ' ') {
    return;
  }

  // Backspace / Delete 무시
  if (cmd == 0x08 || cmd == 0x7F) {
    return;
  }

  switch (cmd) {
    // ==============================================
    // MATRIX ON
    // ==============================================

    case '1':
      setMatrix(0, true);
      sendLine("MATRIX1 ON");
      checkSystem();
      break;

    case '2':
      setMatrix(1, true);
      sendLine("MATRIX2 ON");
      checkSystem();
      break;

    case '3':
      setMatrix(2, true);
      sendLine("MATRIX3 ON");
      checkSystem();
      break;

    case '4':
      setMatrix(3, true);
      sendLine("MATRIX4 ON");
      checkSystem();
      break;

    // ==============================================
    // MATRIX OFF
    // ==============================================

    case 'q':
    case 'Q':
      setMatrix(0, false);
      sendLine("MATRIX1 OFF");
      checkSystem();
      break;

    case 'w':
    case 'W':
      setMatrix(1, false);
      sendLine("MATRIX2 OFF");
      checkSystem();
      break;

    case 'e':
    case 'E':
      setMatrix(2, false);
      sendLine("MATRIX3 OFF");
      checkSystem();
      break;

    case 'r':
    case 'R':
      setMatrix(3, false);
      sendLine("MATRIX4 OFF");
      checkSystem();
      break;

    // ==============================================
    // ALL OFF
    // ==============================================

    case 'f':
    case 'F':
      allOff();
      sendLine("ALL OFF");
      break;

    // ==============================================
    // 도움말
    // ==============================================

    case 'h':
    case 'H':
      printHelp();
      break;

    default: {
      char message[40];

      snprintf(
        message,
        sizeof(message),
        "UNKNOWN COMMAND: %c",
        cmd
      );

      sendLine(message);
      break;
    }
  }
}

// ==================================================
// USB + Bluetooth 출력
// ==================================================

void sendLine(const char* message) {
  Serial.println(message);

  if (bluetoothReady && btClientConnected) {
    SerialBT.println(message);
  }
}

void printHelp() {
  sendLine("");
  sendLine("======= COMMAND LIST =======");
  sendLine("1 : MATRIX1 ON");
  sendLine("2 : MATRIX2 ON");
  sendLine("3 : MATRIX3 ON");
  sendLine("4 : MATRIX4 ON");
  sendLine("q : MATRIX1 OFF");
  sendLine("w : MATRIX2 OFF");
  sendLine("e : MATRIX3 OFF");
  sendLine("r : MATRIX4 OFF");
  sendLine("f : ALL OFF");
  sendLine("h : HELP");
  sendLine("============================");
}

// ==================================================
// 매트릭스 개별 제어
// ==================================================

void setMatrix(int index, bool state) {
  if (index < 0 || index >= 4) {
    return;
  }

  ledState[index] = state;

  if (state) {
    fill_solid(
      matrices[index],
      NUM_LEDS,
      CRGB::Yellow
    );
  }
  else {
    fill_solid(
      matrices[index],
      NUM_LEDS,
      CRGB::Black
    );
  }

  FastLED.show();
}

// ==================================================
// 전체 시스템 상태 확인
// ==================================================

void checkSystem() {
  bool allActive = isAllActive();

  digitalWrite(
    RELAY_PIN,
    allActive ? RELAY_ON : RELAY_OFF
  );

  // 처음으로 네 매트릭스가 모두 켜진 순간
  if (allActive && !relayState) {
    sendLine("ALL MATRICES ACTIVE");
    sendLine("RELAY ON");
    sendLine("PRE EFFECT START");
    sendLine("FADE OUT: 4 SEC");
    sendLine("BLINK: 6 SEC");

    preBlinkActive = true;
    effectStarted = false;

    preBlinkStartTime = millis();
    lastPreEffectUpdate = 0;
    lastEffectUpdate = 0;

    // 준비 효과 이후 무지개부터 시작
    effectFrame = ARROW_FRAMES;
    rainbowOffset = 0;
  }

  // 네 매트릭스 중 하나라도 꺼진 순간
  if (!allActive && relayState) {
    sendLine("RELAY OFF");
    sendLine("EFFECT STOP");

    preBlinkActive = false;
    effectStarted = false;

    showNormalState();
  }

  relayState = allActive;
}

bool isAllActive() {
  return ledState[0] &&
         ledState[1] &&
         ledState[2] &&
         ledState[3];
}

// ==================================================
// 일반 상태 표시
// ==================================================

void showNormalState() {
  for (int i = 0; i < 4; i++) {
    if (ledState[i]) {
      fill_solid(
        matrices[i],
        NUM_LEDS,
        CRGB::Yellow
      );
    }
    else {
      fill_solid(
        matrices[i],
        NUM_LEDS,
        CRGB::Black
      );
    }
  }

  FastLED.show();
}

// ==================================================
// 10초 준비 효과
//
// 0~4초  : 천천히 Fade Out
// 4~10초 : 40 BPM 깜빡임
// ==================================================

void updatePreBlink() {
  unsigned long now = millis();

  unsigned long elapsed =
      now - preBlinkStartTime;

  // 총 10초 종료
  if (elapsed >= PRE_BLINK_DURATION_MS) {
    preBlinkActive = false;
    effectStarted = true;

    lastEffectUpdate = 0;

    sendLine("PRE EFFECT END");
    sendLine("RAINBOW EFFECT START");

    return;
  }

  if (now - lastPreEffectUpdate <
      PRE_EFFECT_UPDATE_MS) {
    return;
  }

  lastPreEffectUpdate = now;

  uint8_t brightness = 0;

  // ================================================
  // 0~4초: 켜진 상태에서 천천히 Fade Out
  // ================================================

  if (elapsed < FADE_OUT_DURATION_MS) {
    float progress =
        elapsed /
        static_cast<float>(FADE_OUT_DURATION_MS);

    // 1.0 -> 0.0으로 부드럽게 감소
    float fade =
        (cos(progress * PI) + 1.0f) * 0.5f;

    brightness =
        static_cast<uint8_t>(fade * 255.0f);
  }

  // ================================================
  // 4~10초: 40 BPM 깜빡임
  // ================================================

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

// ==================================================
// 무지개 / 화살표 효과
// ==================================================

void updateEffect() {
  unsigned long now = millis();

  if (now - lastEffectUpdate <
      EFFECT_UPDATE_MS) {
    return;
  }

  lastEffectUpdate = now;

  uint16_t cycleFrame =
      effectFrame %
      (ARROW_FRAMES + RAINBOW_FRAMES);

  bool arrowMode =
      cycleFrame < ARROW_FRAMES;

  for (int m = 0; m < 4; m++) {
    fill_solid(
      matrices[m],
      NUM_LEDS,
      CRGB::Black
    );

    if (arrowMode) {
      drawMovingArrow(
        m,
        cycleFrame
      );

      drawRainbowTrail(
        m,
        cycleFrame
      );
    }
    else {
      drawFullRainbow(m);
    }
  }

  FastLED.show();

  rainbowOffset += RAINBOW_SPEED;
  effectFrame++;
}

// ==================================================
// 움직이는 흰색 화살표
// ==================================================

void drawMovingArrow(
    int matrixIndex,
    int frame) {

  int baseRow =
      HEIGHT - 1 - frame;

  const int arrow[][2] = {
    {3,0}, {4,0},

    {2,1}, {3,1}, {4,1}, {5,1},

    {1,2}, {2,2}, {3,2},
    {4,2}, {5,2}, {6,2},

    {3,3}, {4,3},
    {3,4}, {4,4},
    {3,5}, {4,5},
    {3,6}, {4,6}
  };

  const int arrowSize =
      sizeof(arrow) /
      sizeof(arrow[0]);

  for (int i = 0; i < arrowSize; i++) {
    int col = arrow[i][0];
    int row =
        arrow[i][1] + baseRow;

    if (row >= 0 && row < HEIGHT) {
      setVisualPixel(
        matrixIndex,
        col,
        row,
        CRGB::White
      );
    }
  }
}

// ==================================================
// 화살표 뒤 무지개 꼬리
// ==================================================

void drawRainbowTrail(
    int matrixIndex,
    int frame) {

  int baseRow =
      HEIGHT - 1 - frame;

  const int TRAIL_LENGTH = 8;

  for (int trail = 1;
       trail <= TRAIL_LENGTH;
       trail++) {

    int row =
        baseRow + trail + 6;

    if (row < 0 || row >= HEIGHT) {
      continue;
    }

    uint8_t hue =
        rainbowOffset +
        trail * 25;

    for (int col = 0;
         col < WIDTH;
         col++) {

      setVisualPixel(
        matrixIndex,
        col,
        row,
        CHSV(hue, 255, 255)
      );
    }
  }
}

// ==================================================
// 전체 무지개
// ==================================================

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

// ==================================================
// 전체 OFF
// ==================================================

void allOff() {
  for (int i = 0; i < 4; i++) {
    ledState[i] = false;

    fill_solid(
      matrices[i],
      NUM_LEDS,
      CRGB::Black
    );
  }

  relayState = false;
  preBlinkActive = false;
  effectStarted = false;

  digitalWrite(
    RELAY_PIN,
    RELAY_OFF
  );

  FastLED.show();
}

// ==================================================
// 보이는 좌표 -> 실제 매트릭스 좌표
// ==================================================

void setVisualPixel(
    int matrixIndex,
    int col,
    int row,
    CRGB color) {

  // 현재 설치 방향에 맞춘 기존 보정 유지
  int x = WIDTH - 1 - row;
  int y = col;

  matrices[matrixIndex][xy(x, y)] =
      color;
}

// ==================================================
// 지그재그 매트릭스 매핑
// ==================================================

int xy(int x, int y) {
  if (y % 2 == 0) {
    return y * WIDTH + x;
  }
  else {
    return y * WIDTH +
           (WIDTH - 1 - x);
  }
}