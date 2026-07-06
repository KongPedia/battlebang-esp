#define FASTLED_ALLOW_INTERRUPTS 0
#include <Arduino.h>
#include <FastLED.h>

// ================= LED =================
#define LED_PIN         33
#define NUM_LEDS        60
#define BRIGHTNESS      120

#define LED_TYPE        WS2812B
#define COLOR_ORDER     RGB

// ================= Piezo =================
#define PIEZO_PIN       32
#define HIT_THRESHOLD   3000
#define HIT_COOLDOWN    300

// ================= Effect =================
#define TAIL_LENGTH         20

#define BREATH_UPDATE_MS    35
#define BREATH_BPM          20
#define BREATH_MIN          5
#define BREATH_MAX          220

CRGB leds[NUM_LEDS];

// false = 아직 안 맞음(제품 있음)
// true = 맞음(제품 제거)
bool targetHit = false;

unsigned long lastHitTime = 0;

// 함수 선언
void greenBreathingEffect();
void redIdleEffect();
void productRemovedEffect();
void productPlacedEffect();
void handlePiezo();
void resetTarget();

void setup() {

    Serial.begin(115200);

    analogReadResolution(12);
    pinMode(PIEZO_PIN, INPUT);

    FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
    FastLED.setBrightness(BRIGHTNESS);

    // 시작은 Product IN 효과
    productPlacedEffect();

    Serial.println("===== Target Demo =====");
    Serial.println("Piezo Hit : Product OUT");
    Serial.println("r : Reset");
}

void loop() {

    handlePiezo();

    if (!targetHit) {
        greenBreathingEffect();
    } else {
        redIdleEffect();
    }

    if (Serial.available()) {

        char c = Serial.read();

        if (c == 'r' || c == 'R') {

            resetTarget();

            Serial.println("RESET");
        }
    }
}

//------------------------------------------------------------
// 피에조 감지
//------------------------------------------------------------
void handlePiezo() {

    if (targetHit) return;

    int value = analogRead(PIEZO_PIN);

    if (value >= HIT_THRESHOLD &&
        millis() - lastHitTime >= HIT_COOLDOWN) {

        lastHitTime = millis();

        targetHit = true;

        Serial.print("HIT! ADC = ");
        Serial.println(value);

        productRemovedEffect();
    }
}

//------------------------------------------------------------
// 리셋
//------------------------------------------------------------
void resetTarget() {

    targetHit = false;
    lastHitTime = 0;

    productPlacedEffect();
}

//------------------------------------------------------------
// 초록 Breathing
//------------------------------------------------------------
void greenBreathingEffect() {

    static unsigned long lastUpdate = 0;

    if (millis() - lastUpdate < BREATH_UPDATE_MS)
        return;

    lastUpdate = millis();

    uint8_t breath = beatsin8(
        BREATH_BPM,
        BREATH_MIN,
        BREATH_MAX);

    fill_solid(leds, NUM_LEDS, CRGB(0, breath, 0));

    FastLED.show();
}

//------------------------------------------------------------
// 빨간색 고정
//------------------------------------------------------------
void redIdleEffect() {

    static bool shown = false;

    if (!targetHit) {
        shown = false;
    }

    if (shown) return;

    shown = true;

    fill_solid(leds, NUM_LEDS, CRGB::Red);
    FastLED.show();
}

//------------------------------------------------------------
// Product OUT 효과
//------------------------------------------------------------
void productRemovedEffect() {

    fill_solid(leds, NUM_LEDS, CRGB::White);
    FastLED.show();
    delay(150);

    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
    delay(60);

    for (int i = 0; i < NUM_LEDS + TAIL_LENGTH; i++) {

        fill_solid(leds, NUM_LEDS, CRGB::Black);

        for (int tail = 0; tail < TAIL_LENGTH; tail++) {

            int idx = i - tail;

            if (idx < 0 || idx >= NUM_LEDS)
                continue;

            uint8_t b = map(tail, 0, TAIL_LENGTH - 1, 255, 0);

            leds[idx] = CRGB(b, b, b);
        }

        FastLED.show();
        delay(8);
    }

    fill_solid(leds, NUM_LEDS, CRGB::Red);
    FastLED.show();
}

//------------------------------------------------------------
// Product IN 효과
//------------------------------------------------------------
void productPlacedEffect() {

    for (int i = 0; i < NUM_LEDS + TAIL_LENGTH; i++) {

        fill_solid(leds, NUM_LEDS, CRGB::Black);

        for (int tail = 0; tail < TAIL_LENGTH; tail++) {

            int idx = i - tail;

            if (idx < 0 || idx >= NUM_LEDS)
                continue;

            uint8_t b = map(tail, 0, TAIL_LENGTH - 1, 255, 0);

            leds[idx] = CRGB(0, 0, b);
        }

        FastLED.show();
        delay(8);
    }

    fill_solid(leds, NUM_LEDS, CRGB::Green);
    FastLED.show();
    delay(200);
}