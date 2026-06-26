#include <Arduino.h>
#include <FastLED.h>

#ifndef BTB782_UART_RX_PIN
#define BTB782_UART_RX_PIN 16
#endif
#ifndef BTB782_UART_TX_PIN
#define BTB782_UART_TX_PIN 17
#endif
#ifndef BTB782_UART_BAUD
#define BTB782_UART_BAUD 115200
#endif
#ifndef BTB782_LED_PIN
#define BTB782_LED_PIN 18
#endif
#ifndef BTB782_NUM_LEDS
#define BTB782_NUM_LEDS 84
#endif
#ifndef BTB782_LED_BRIGHTNESS
#define BTB782_LED_BRIGHTNESS 120
#endif
#ifndef BTB782_PIEZO_AO_PIN
#define BTB782_PIEZO_AO_PIN 34
#endif
#ifndef BTB782_PIEZO_THRESHOLD_RAW
#define BTB782_PIEZO_THRESHOLD_RAW 200
#endif
#ifndef BTB782_PIEZO_REARM_RAW
#define BTB782_PIEZO_REARM_RAW 150
#endif
#ifndef BTB782_CAPTURE_WINDOW_MS
#define BTB782_CAPTURE_WINDOW_MS 30
#endif
#ifndef BTB782_REARM_STABLE_MS
#define BTB782_REARM_STABLE_MS 30
#endif
#ifndef BTB782_HP_MAX
#define BTB782_HP_MAX 14
#endif
#ifndef BTB782_HP_TX_PERIOD_MS
#define BTB782_HP_TX_PERIOD_MS 100
#endif
#ifndef BTB782_DEBUG_PERIOD_MS
#define BTB782_DEBUG_PERIOD_MS 1000
#endif

static HardwareSerial& JetsonSerial = Serial2;
static CRGB leds[BTB782_NUM_LEDS];

static int hp_remaining = BTB782_HP_MAX;
static uint32_t accepted_hit_count = 0;
static bool down = false;

static bool piezo_armed = true;
static bool capture_active = false;
static uint32_t capture_started_ms = 0;
static int capture_peak_raw = 0;
static uint32_t quiet_started_ms = 0;
static uint32_t last_hit_flash_ms = 0;
static uint32_t last_hp_tx_ms = 0;
static uint32_t last_debug_ms = 0;

static String usb_line;
static String jetson_line;

static CRGB hpColor() {
  if (down || hp_remaining <= 0) return CRGB::Red;
  const float ratio = static_cast<float>(hp_remaining) / static_cast<float>(BTB782_HP_MAX);
  if (ratio > 0.60f) return CRGB::Green;
  if (ratio > 0.30f) return CRGB::Yellow;
  return CRGB::Red;
}

static void renderHpBar(uint32_t now) {
  if (down || hp_remaining <= 0) {
    const bool blink_on = ((now / 250U) % 2U) == 0U;
    fill_solid(leds, BTB782_NUM_LEDS, blink_on ? CRGB::Red : CRGB::Black);
    FastLED.show();
    return;
  }

  const int lit = constrain(
      (hp_remaining * BTB782_NUM_LEDS + BTB782_HP_MAX - 1) / BTB782_HP_MAX,
      0,
      BTB782_NUM_LEDS);
  const CRGB color = hpColor();
  const bool flash = now - last_hit_flash_ms < 80U;
  for (int i = 0; i < BTB782_NUM_LEDS; ++i) {
    if (i < lit) {
      leds[i] = flash ? CRGB::White : color;
    } else {
      leds[i] = CRGB::Black;
    }
  }
  FastLED.show();
}

static void sendHpToJetson(uint32_t now, bool force = false) {
  if (!force && now - last_hp_tx_ms < BTB782_HP_TX_PERIOD_MS) return;
  last_hp_tx_ms = now;
  JetsonSerial.println(hp_remaining);
}

static void printStatusTo(Stream& out, const char* source) {
  out.print("{\"event\":\"status\",\"source\":\"");
  out.print(source);
  out.print("\",\"hp_remaining\":");
  out.print(hp_remaining);
  out.print(",\"hp_max\":");
  out.print(BTB782_HP_MAX);
  out.print(",\"accepted_hit_count\":");
  out.print(accepted_hit_count);
  out.print(",\"down\":");
  out.print(down ? "true" : "false");
  out.print(",\"piezo_raw\":");
  out.print(analogRead(BTB782_PIEZO_AO_PIN));
  out.print(",\"uart_rx_pin\":");
  out.print(BTB782_UART_RX_PIN);
  out.print(",\"uart_tx_pin\":");
  out.print(BTB782_UART_TX_PIN);
  out.println("}");
}

static void printStatus(const char* source) {
  printStatusTo(Serial, source);
  if (strcmp(source, "jetson") == 0) printStatusTo(JetsonSerial, source);
}

static void resetHp(const char* source) {
  hp_remaining = BTB782_HP_MAX;
  accepted_hit_count = 0;
  down = false;
  piezo_armed = true;
  capture_active = false;
  quiet_started_ms = millis();
  last_hit_flash_ms = millis();
  sendHpToJetson(millis(), true);
  Serial.printf("[reset] source=%s hp=%d\n", source, hp_remaining);
  if (strcmp(source, "jetson") == 0) JetsonSerial.println(hp_remaining);
}

static void acceptHit(int peak_raw, const char* source) {
  if (down || hp_remaining <= 0) return;
  accepted_hit_count++;
  hp_remaining = max(0, hp_remaining - 1);
  down = hp_remaining <= 0;
  last_hit_flash_ms = millis();
  sendHpToJetson(millis(), true);
  Serial.printf("[hit] source=%s peak=%d hp=%d/%d down=%d count=%lu\n",
                source,
                peak_raw,
                hp_remaining,
                BTB782_HP_MAX,
                down ? 1 : 0,
                static_cast<unsigned long>(accepted_hit_count));
}

static bool isIgnoredLeadingChar(char c) {
  return c == '\0' || c == ' ' || c == '\t';
}

static void handleCommandLine(String line, const char* source) {
  line.trim();
  if (line.length() == 0) return;
  String lower = line;
  lower.toLowerCase();

  if (lower == "r" || lower == "reset" || lower == "2") {
    resetHp(source);
    return;
  }
  if (lower == "s" || lower == "status" || lower == "show-status") {
    printStatus(source);
    return;
  }
  if (lower == "h" || lower == "hit" || lower == "simulate-hit") {
    acceptHit(analogRead(BTB782_PIEZO_AO_PIN), source);
    return;
  }

  Serial.printf("[cmd] ignored source=%s line=%s\n", source, line.c_str());
  if (strcmp(source, "jetson") == 0) JetsonSerial.println("{\"event\":\"command_ignored\"}");
}

static void pollCommandStream(Stream& stream, String& line, const char* source) {
  while (stream.available() > 0) {
    const char c = static_cast<char>(stream.read());
    if (c == '\r') continue;
    if (c == '\n') {
      handleCommandLine(line, source);
      line = "";
      continue;
    }
    if (line.length() == 0 && isIgnoredLeadingChar(c)) continue;
    if (line.length() == 0 && (c == '2' || c == 'r' || c == 'R' || c == 's' || c == 'S' || c == 'h' || c == 'H') &&
        stream.available() == 0) {
      String one;
      one += c;
      handleCommandLine(one, source);
      continue;
    }
    if (line.length() < 256) {
      line += c;
    } else {
      line = "";
      Serial.printf("[cmd] source=%s line too long; dropped\n", source);
    }
  }
}

static void pollPiezo(uint32_t now) {
  const int raw = analogRead(BTB782_PIEZO_AO_PIN);

  if (capture_active) {
    if (raw > capture_peak_raw) capture_peak_raw = raw;
    if (now - capture_started_ms >= BTB782_CAPTURE_WINDOW_MS) {
      capture_active = false;
      piezo_armed = false;
      quiet_started_ms = 0;
      acceptHit(capture_peak_raw, "piezo");
    }
    return;
  }

  if (!piezo_armed) {
    if (raw <= BTB782_PIEZO_REARM_RAW) {
      if (quiet_started_ms == 0) quiet_started_ms = now;
      if (now - quiet_started_ms >= BTB782_REARM_STABLE_MS) {
        piezo_armed = true;
      }
    } else {
      quiet_started_ms = 0;
    }
    return;
  }

  if (!down && raw >= BTB782_PIEZO_THRESHOLD_RAW) {
    capture_active = true;
    capture_started_ms = now;
    capture_peak_raw = raw;
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  JetsonSerial.begin(BTB782_UART_BAUD, SERIAL_8N1, BTB782_UART_RX_PIN, BTB782_UART_TX_PIN);
  analogReadResolution(12);
  pinMode(BTB782_PIEZO_AO_PIN, INPUT);

  FastLED.addLeds<WS2812B, BTB782_LED_PIN, GRB>(leds, BTB782_NUM_LEDS);
  FastLED.setBrightness(BTB782_LED_BRIGHTNESS);
  fill_solid(leds, BTB782_NUM_LEDS, CRGB::Black);
  FastLED.show();

  quiet_started_ms = millis();
  renderHpBar(millis());
  sendHpToJetson(millis(), true);

  Serial.println("[btb782] standalone ESP UART HP sketch booted");
  Serial.printf("[pin] uart2 rx=D%d tx=D%d baud=%lu | led=D%d count=%d brightness=%d | piezo_ao=D%d threshold=%d rearm=%d | hp_max=%d\n",
                BTB782_UART_RX_PIN,
                BTB782_UART_TX_PIN,
                static_cast<unsigned long>(BTB782_UART_BAUD),
                BTB782_LED_PIN,
                BTB782_NUM_LEDS,
                BTB782_LED_BRIGHTNESS,
                BTB782_PIEZO_AO_PIN,
                BTB782_PIEZO_THRESHOLD_RAW,
                BTB782_PIEZO_REARM_RAW,
                BTB782_HP_MAX);
  Serial.println("[cmd] USB/Jetson: s=status, h=simulate hit, 2/r/reset=reset HP");
  printStatus("boot");
}

void loop() {
  const uint32_t now = millis();
  pollCommandStream(Serial, usb_line, "usb");
  pollCommandStream(JetsonSerial, jetson_line, "jetson");
  pollPiezo(now);
  renderHpBar(now);
  sendHpToJetson(now);

  if (now - last_debug_ms >= BTB782_DEBUG_PERIOD_MS) {
    last_debug_ms = now;
    Serial.printf("[debug] hp=%d/%d raw=%d armed=%d down=%d\n",
                  hp_remaining,
                  BTB782_HP_MAX,
                  analogRead(BTB782_PIEZO_AO_PIN),
                  piezo_armed ? 1 : 0,
                  down ? 1 : 0);
  }

  delay(1);
}
