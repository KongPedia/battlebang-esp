#include <ESP32Servo.h>

// =========================
// 핀 설정
// =========================
const int BUTTON_PIN = 4;    // 풀다운 버튼 입력 핀
const int ESC_PIN    = 18;   // BLDC ESC PWM 신호 핀
const int RELAY_PIN  = 23;   // 릴레이 제어 핀

// =========================
// 릴레이 설정
// =========================
// HIGH 입력 시 ON 되는 릴레이 모듈 기준
const int RELAY_ON  = HIGH;
const int RELAY_OFF = LOW;

// 만약 LOW 입력 시 ON 되는 릴레이 모듈이면 위 두 줄 대신 아래처럼 바꾸세요.
// const int RELAY_ON  = LOW;
// const int RELAY_OFF = HIGH;

// =========================
// ESC PWM 설정
// =========================
const int ESC_STOP_US = 1000;  // ESC 정지 신호
const int ESC_RUN_US  = 2000;  // ESC 동작 신호, 처음에는 낮게 시작

// =========================
// 시간 설정
// =========================
const unsigned long ESC_ARM_TIME_MS = 3000; // ESC 초기화 시간
const unsigned long ON_DELAY_MS     = 300;  // ESC ON 후 릴레이 ON까지 대기
const unsigned long OFF_DELAY_MS    = 300;  // 릴레이 OFF 후 ESC OFF까지 대기
const unsigned long DEBOUNCE_MS     = 30;   // 버튼 채터링 방지

Servo esc;

// =========================
// 상태 정의
// =========================
enum SystemState {
  STATE_OFF,
  STATE_ESC_ON_WAIT,
  STATE_RUNNING,
  STATE_RELAY_OFF_WAIT
};

SystemState state = STATE_OFF;

unsigned long stateStartTime = 0;
unsigned long lastDebounceTime = 0;

bool lastRawButtonState = LOW;
bool buttonPressed = false;

// =========================
// 버튼 상태 업데이트
// 풀다운 저항 기준
// 안 누름 = LOW
// 누름 = HIGH
// =========================
void updateButton() {
  bool rawState = digitalRead(BUTTON_PIN);

  if (rawState != lastRawButtonState) {
    lastDebounceTime = millis();
    lastRawButtonState = rawState;
  }

  if (millis() - lastDebounceTime > DEBOUNCE_MS) {
    buttonPressed = rawState == HIGH;
  }
}

void setup() {
  Serial.begin(115200);

  // 외부 풀다운 저항을 사용하므로 INPUT
  pinMode(BUTTON_PIN, INPUT);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_OFF);

  // ESC 설정
  esc.setPeriodHertz(50); // 일반 RC ESC는 50Hz PWM 사용
  esc.attach(ESC_PIN, 1000, 2000);

  // ESC 초기화
  Serial.println("ESC arming...");
  esc.writeMicroseconds(ESC_STOP_US);
  delay(ESC_ARM_TIME_MS);

  Serial.println("System ready");
}

void loop() {
  updateButton();

  switch (state) {

    case STATE_OFF:
      digitalWrite(RELAY_PIN, RELAY_OFF);
      esc.writeMicroseconds(ESC_STOP_US);

      if (buttonPressed) {
        Serial.println("Button pressed: ESC ON");

        // 1단계: ESC ON
        esc.writeMicroseconds(ESC_RUN_US);

        state = STATE_ESC_ON_WAIT;
        stateStartTime = millis();
      }
      break;

    case STATE_ESC_ON_WAIT:
      esc.writeMicroseconds(ESC_RUN_US);

      // 버튼을 중간에 떼면 바로 정지
      if (!buttonPressed) {
        Serial.println("Button released during start: STOP");

        digitalWrite(RELAY_PIN, RELAY_OFF);
        esc.writeMicroseconds(ESC_STOP_US);

        state = STATE_OFF;
      }

      // ESC ON 후 일정 시간 지나면 릴레이 ON
      else if (millis() - stateStartTime >= ON_DELAY_MS) {
        Serial.println("Relay ON");

        digitalWrite(RELAY_PIN, RELAY_ON);

        state = STATE_RUNNING;
      }
      break;

    case STATE_RUNNING:
      esc.writeMicroseconds(ESC_RUN_US);
      digitalWrite(RELAY_PIN, RELAY_ON);

      if (!buttonPressed) {
        Serial.println("Button released: Relay OFF");

        // 1단계: 릴레이 OFF
        digitalWrite(RELAY_PIN, RELAY_OFF);

        state = STATE_RELAY_OFF_WAIT;
        stateStartTime = millis();
      }
      break;

    case STATE_RELAY_OFF_WAIT:
      digitalWrite(RELAY_PIN, RELAY_OFF);

      // 정지 대기 중 다시 버튼 누르면 재시작
      if (buttonPressed) {
        Serial.println("Button pressed again: ESC ON");

        esc.writeMicroseconds(ESC_RUN_US);

        state = STATE_ESC_ON_WAIT;
        stateStartTime = millis();
      }

      // 릴레이 OFF 후 일정 시간 지나면 ESC OFF
      else if (millis() - stateStartTime >= OFF_DELAY_MS) {
        Serial.println("ESC OFF");

        // 2단계: ESC OFF
        esc.writeMicroseconds(ESC_STOP_US);

        state = STATE_OFF;
      }
      break;
  }
}