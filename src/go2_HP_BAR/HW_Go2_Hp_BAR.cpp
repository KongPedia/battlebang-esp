#include <FastLED.h>

#define LED_PIN     4       // ESP32 D4 = GPIO4 기준
#define NUM_LEDS    84
#define GROUP_SIZE  28
#define BRIGHTNESS  120

CRGB leds[NUM_LEDS];

int redStep = 0;             // 0이면 전부 초록, 1이면 28번 그룹만 빨강
bool blinkMode = false;
bool blinkOn = true;

unsigned long lastBlinkTime = 0;
const unsigned long BLINK_INTERVAL = 500;  // 점멸 간격 ms

void setLinkedGroup(int pos1Based, CRGB color) {
  // pos1Based: 1~28
  //
  // 변경된 묶음:
  // 1  -> 1, 56, 57
  // 2  -> 2, 55, 58
  // 3  -> 3, 54, 59
  // ...
  // 28 -> 28, 29, 84

  int row1Index = pos1Based - 1;        // LED 1~28
  int row2Index = 56 - pos1Based;       // LED 56~29, 역방향
  int row3Index = 55 + pos1Based;       // LED 57~84

  leds[row1Index] = color;
  leds[row2Index] = color;
  leds[row3Index] = color;
}

void showGreenReset() {
  redStep = 0;
  blinkMode = false;
  blinkOn = true;

  fill_solid(leds, NUM_LEDS, CRGB::Green);
  FastLED.show();

  Serial.println("RESET: all green");
}

void updateRedProgress() {
  // 일단 전체 초록
  fill_solid(leds, NUM_LEDS, CRGB::Green);

  // h를 누를 때마다
  // 28번 그룹 -> 27번 그룹 -> ... -> 1번 그룹 순서로 빨강
  for (int pos = GROUP_SIZE; pos >= GROUP_SIZE - redStep + 1; pos--) {
    setLinkedGroup(pos, CRGB::Red);
  }

  FastLED.show();

  Serial.print("RED STEP: ");
  Serial.println(redStep);
}

void startRedBlink() {
  blinkMode = true;
  blinkOn = true;
  lastBlinkTime = millis();

  fill_solid(leds, NUM_LEDS, CRGB::Red);
  FastLED.show();

  Serial.println("BLINK MODE: red blinking");
}

void handleSerial() {
  while (Serial.available() > 0) {
    char cmd = Serial.read();

    if (cmd == 'h' || cmd == 'H') {
      if (!blinkMode) {
        if (redStep < GROUP_SIZE) {
          redStep++;
          updateRedProgress();

          // 1번 그룹까지 빨강이 되면 점멸 시작
          if (redStep >= GROUP_SIZE) {
            startRedBlink();
          }
        }
      }
    }

    else if (cmd == 'r' || cmd == 'R') {
      showGreenReset();
    }
  }
}

void handleBlink() {
  if (!blinkMode) return;

  unsigned long now = millis();

  if (now - lastBlinkTime >= BLINK_INTERVAL) {
    lastBlinkTime = now;
    blinkOn = !blinkOn;

    if (blinkOn) {
      fill_solid(leds, NUM_LEDS, CRGB::Red);
    } else {
      fill_solid(leds, NUM_LEDS, CRGB::Black);
    }

    FastLED.show();
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);


  FastLED.addLeds<WS2815, LED_PIN, RGB>(leds, NUM_LEDS);

  FastLED.setBrightness(BRIGHTNESS);

  showGreenReset();

  Serial.println("Ready.");
  Serial.println("Press h: red moves from group 28 to group 1");
  Serial.println("Press r: reset to green");
}

void loop() {
  handleSerial();
  handleBlink();
}