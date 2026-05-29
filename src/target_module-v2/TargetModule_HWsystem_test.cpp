#include <FastLED.h>

#define LED_PIN        26
#define NUM_LEDS       40
#define PIEZO_DO_PIN   27

#define BRIGHTNESS     80
#define LED_TYPE       WS2811
#define COLOR_ORDER    RGB

CRGB leds[NUM_LEDS];

constexpr int HP_MAX = 6000;
constexpr int HP_PER_LAP = 2000;
constexpr int DAMAGE = 300;

constexpr uint32_t ISR_DEBOUNCE_US = 20000;
constexpr uint32_t HIT_COOLDOWN_MS = 300;
constexpr uint32_t BLINK_MS = 250;
constexpr uint32_t HIT_FLASH_MS = 60;
constexpr uint32_t DEAD_BLINK_MS = 300;

int hp = HP_MAX;

bool blinkMask[NUM_LEDS] = {false};
bool blinkOn = false;
uint32_t lastBlinkMs = 0;

bool flashActive = false;
uint32_t flashStartMs = 0;

bool deadOn = false;
uint32_t lastDeadBlinkMs = 0;

volatile bool piezoTriggered = false;
volatile uint32_t lastIsrUs = 0;

uint32_t lastHitMs = 0;

void IRAM_ATTR piezoISR() {
  uint32_t nowUs = micros();

  if (nowUs - lastIsrUs < ISR_DEBOUNCE_US) return;

  lastIsrUs = nowUs;
  piezoTriggered = true;
}

int hpToBand(int hpVal) {
  if (hpVal <= 0) return -1;
  return (hpVal - 1) / HP_PER_LAP;
}

int hpToLapHp(int hpVal) {
  if (hpVal <= 0) return 0;

  int r = hpVal % HP_PER_LAP;
  return (r == 0) ? HP_PER_LAP : r;
}

int lapHpToLit(int lapHp) {
  lapHp = constrain(lapHp, 0, HP_PER_LAP);
  return (long)lapHp * NUM_LEDS / HP_PER_LAP;
}

CRGB bandColor(int band) {
  if (band >= 2) return CRGB::Green;
  if (band == 1) return CRGB::Yellow;
  if (band == 0) return CRGB::Red;
  return CRGB::Black;
}

CRGB nextBandColor(int band) {
  if (band <= 0) return CRGB::Black;
  return bandColor(band - 1);
}

void clearBlinkMask() {
  for (int i = 0; i < NUM_LEDS; i++) {
    blinkMask[i] = false;
  }
}

void addBlinkSegmentSameBand(int oldHp, int newHp) {
  int oldLit = lapHpToLit(hpToLapHp(oldHp));
  int newLit = lapHpToLit(hpToLapHp(newHp));

  if (newLit < oldLit) {
    for (int i = newLit; i < oldLit; i++) {
      if (i >= 0 && i < NUM_LEDS) {
        blinkMask[i] = true;
      }
    }
  }
}

void renderLeds() {
  uint32_t now = millis();

  if (flashActive) {
    if (now - flashStartMs < HIT_FLASH_MS) {
      fill_solid(leds, NUM_LEDS, CRGB::White);
      FastLED.show();
      return;
    } else {
      flashActive = false;
    }
  }

  if (now - lastBlinkMs >= BLINK_MS) {
    lastBlinkMs = now;
    blinkOn = !blinkOn;
  }

  if (hp <= 0) {
    if (now - lastDeadBlinkMs >= DEAD_BLINK_MS) {
      lastDeadBlinkMs = now;
      deadOn = !deadOn;
    }

    fill_solid(leds, NUM_LEDS, deadOn ? CRGB::Red : CRGB::Black);
    FastLED.show();
    return;
  }

  int band = hpToBand(hp);
  int lit = lapHpToLit(hpToLapHp(hp));

  CRGB base = bandColor(band);
  CRGB blinkC = nextBandColor(band);

  for (int i = 0; i < NUM_LEDS; i++) {
    if (i < lit) {
      leds[i] = base;
    }
    else if (blinkMask[i]) {
      leds[i] = blinkOn ? blinkC : CRGB::Black;
    }
    else {
      leds[i] = CRGB::Black;
    }
  }

  FastLED.show();
}

void applyDamage() {
  if (hp <= 0) return;

  int oldHp = hp;
  int oldBand = hpToBand(oldHp);

  hp -= DAMAGE;
  if (hp < 0) hp = 0;

  int newBand = hpToBand(hp);

  if (newBand != oldBand) {
    clearBlinkMask();
  }

  if (hp > 0 && newBand == oldBand) {
    addBlinkSegmentSameBand(oldHp, hp);
  }
  else if (hp > 0 && newBand != oldBand) {
    int newLit = lapHpToLit(hpToLapHp(hp));

    for (int i = newLit; i < NUM_LEDS; i++) {
      blinkMask[i] = true;
    }
  }

  flashActive = true;
  flashStartMs = millis();

  Serial.print("PIEZO HIT! HP = ");
  Serial.println(hp);
}

void resetHp() {
  hp = HP_MAX;
  clearBlinkMask();

  flashActive = false;
  deadOn = false;

  Serial.println("HP RESET");
  renderLeds();
}

void setup() {
  Serial.begin(115200);

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);

  pinMode(PIEZO_DO_PIN, INPUT_PULLDOWN);

  attachInterrupt(
    digitalPinToInterrupt(PIEZO_DO_PIN),
    piezoISR,
    RISING
  );

  clearBlinkMask();
  renderLeds();

  Serial.println("Piezo DO Boss HP Ready");
  Serial.println("r = reset");
}

void loop() {
  uint32_t now = millis();

  if (piezoTriggered) {
    piezoTriggered = false;

    if (now - lastHitMs >= HIT_COOLDOWN_MS) {
      lastHitMs = now;
      applyDamage();
    }
  }

  renderLeds();

  if (Serial.available()) {
    char c = Serial.read();

    if (c == 'r' || c == 'R') {
      resetHp();
    }
  }
}