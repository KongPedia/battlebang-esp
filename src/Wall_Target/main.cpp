#include <Arduino.h>
#include <FastLED.h>

#define LED_PIN        18      // 60 LED 링 DIN 연결 핀
#define LED_COUNT      60

#define PIEZO_DO_PIN   27      // 피에조 센서 DO 연결 핀

#define LED_TYPE       WS2812B
#define COLOR_ORDER    GRB

CRGB leds[LED_COUNT];

volatile bool piezoTriggered = false;

bool blinking = false;
bool whiteOn = false;

unsigned long blinkStartTime = 0;
unsigned long lastBlinkTime = 0;
unsigned long lastTriggerTime = 0;

const unsigned long BLINK_DURATION = 2000;   // 2초 동안 흰색 깜빡임
const unsigned long BLINK_INTERVAL = 200;    // 깜빡임 속도
const unsigned long TRIGGER_COOLDOWN = 300;  // 중복 감지 방지

void IRAM_ATTR piezoISR() {
  piezoTriggered = true;
}

void setAllRed() {
  fill_solid(leds, LED_COUNT, CRGB::Red);
  FastLED.show();
}

void setAllWhite() {
  fill_solid(leds, LED_COUNT, CRGB::White);
  FastLED.show();
}

void setAllOff() {
  fill_solid(leds, LED_COUNT, CRGB::Black);
  FastLED.show();
}

void setup() {
  Serial.begin(115200);

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, LED_COUNT);
  FastLED.setBrightness(80);  // 0~255

  // 전류 제한. 필요 없으면 지워도 됨.
  // 5V, 최대 1500mA 기준
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 1500);

  /*
    피에조 DO가 감지될 때 HIGH가 되는 모듈 기준
  */
  pinMode(PIEZO_DO_PIN, INPUT_PULLDOWN);

  attachInterrupt(
    digitalPinToInterrupt(PIEZO_DO_PIN),
    piezoISR,
    RISING
  );

  setAllRed();
}

void loop() {
  unsigned long now = millis();

  // 피에조 DO 인터럽트 발생 처리
  if (piezoTriggered) {
    piezoTriggered = false;

    if (!blinking && now - lastTriggerTime > TRIGGER_COOLDOWN) {
      lastTriggerTime = now;

      blinking = true;
      whiteOn = true;
      blinkStartTime = now;
      lastBlinkTime = now;

      setAllWhite();
      Serial.println("Piezo detected -> white blink");
    }
  }

  // 2초 동안 흰색 깜빡임
  if (blinking) {
    now = millis();

    if (now - blinkStartTime >= BLINK_DURATION) {
      blinking = false;
      piezoTriggered = false;

      setAllRed();
      Serial.println("Back to red");
    } else {
      if (now - lastBlinkTime >= BLINK_INTERVAL) {
        lastBlinkTime = now;
        whiteOn = !whiteOn;

        if (whiteOn) {
          setAllWhite();
        } else {
          setAllOff();
        }
      }
    }
  }
}