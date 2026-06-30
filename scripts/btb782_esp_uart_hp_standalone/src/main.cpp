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
#ifndef BTB782_HP_BAR_GROUP_COUNT
#define BTB782_HP_BAR_GROUP_COUNT 28
#endif
#ifndef BTB782_HP_BAR_LEDS_PER_GROUP
#define BTB782_HP_BAR_LEDS_PER_GROUP 3
#endif
#ifndef BTB782_LED_SHOW_PERIOD_MS
#define BTB782_LED_SHOW_PERIOD_MS 16
#endif
#ifndef BTB782_LED_BLINK_MS
#define BTB782_LED_BLINK_MS 250
#endif
#ifndef BTB782_LED_DEAD_BLINK_MS
#define BTB782_LED_DEAD_BLINK_MS 300
#endif
#ifndef BTB782_LED_MAX_VOLTS
#define BTB782_LED_MAX_VOLTS 5
#endif
#ifndef BTB782_LED_MAX_MA
#define BTB782_LED_MAX_MA 900
#endif
#ifndef BTB782_LED_BRIGHTNESS
#define BTB782_LED_BRIGHTNESS 120
#endif
#ifndef BTB782_LEFT_PIEZO_PIN
#define BTB782_LEFT_PIEZO_PIN 34
#endif
#ifndef BTB782_RIGHT_PIEZO_PIN
#define BTB782_RIGHT_PIEZO_PIN 35
#endif
#ifndef BTB782_FRONT_PIEZO_PIN
#define BTB782_FRONT_PIEZO_PIN 32
#endif
#ifndef BTB782_PIEZO_THRESHOLD_RAW
#define BTB782_PIEZO_THRESHOLD_RAW 2400
#endif
#ifndef BTB782_PIEZO_REARM_RAW
#define BTB782_PIEZO_REARM_RAW 1800
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
#ifndef BTB782_NIXO_RELAY1_PIN
#define BTB782_NIXO_RELAY1_PIN 23
#endif
#ifndef BTB782_NIXO_RELAY2_PIN
#define BTB782_NIXO_RELAY2_PIN -1
#endif
#ifndef BTB782_NIXO_RELAY_ON_LEVEL
#define BTB782_NIXO_RELAY_ON_LEVEL HIGH
#endif
#ifndef BTB782_NIXO_RELAY_OFF_LEVEL
#define BTB782_NIXO_RELAY_OFF_LEVEL LOW
#endif
#ifndef BTB782_NIXO_PREFIRE_DELAY_MS
#define BTB782_NIXO_PREFIRE_DELAY_MS 600
#endif
#ifndef BTB782_NIXO_RELAY_DELAY1_MS
#define BTB782_NIXO_RELAY_DELAY1_MS 800
#endif
#ifndef BTB782_NIXO_FIRE_DEFAULT_DURATION_MS
#define BTB782_NIXO_FIRE_DEFAULT_DURATION_MS 3000
#endif
#ifndef BTB782_NIXO_FIRE_MIN_DURATION_MS
#define BTB782_NIXO_FIRE_MIN_DURATION_MS 100
#endif
#ifndef BTB782_NIXO_FIRE_MAX_DURATION_MS
#define BTB782_NIXO_FIRE_MAX_DURATION_MS 10000
#endif
#ifndef BTB782_NIXO_FIRE_COOLDOWN_MS
#define BTB782_NIXO_FIRE_COOLDOWN_MS 1500
#endif

static_assert(BTB782_NUM_LEDS ==
                  BTB782_HP_BAR_GROUP_COUNT * BTB782_HP_BAR_LEDS_PER_GROUP,
              "BTB782_NUM_LEDS must equal HP bar groups * LEDs per group");
static_assert(BTB782_HP_BAR_LEDS_PER_GROUP == 3,
              "Legacy Go2 HP bar mapping expects 3 LEDs per group");
static_assert(BTB782_NIXO_RELAY1_PIN >= 0,
              "BTB782_NIXO_RELAY1_PIN must be a valid GPIO");
static_assert(BTB782_NIXO_RELAY2_PIN < 0 ||
                  BTB782_NIXO_RELAY1_PIN != BTB782_NIXO_RELAY2_PIN,
              "Nixo relay pins must be different");
static_assert(BTB782_NIXO_FIRE_MIN_DURATION_MS <=
                  BTB782_NIXO_FIRE_DEFAULT_DURATION_MS,
              "default Nixo fire duration below min");
static_assert(BTB782_NIXO_FIRE_DEFAULT_DURATION_MS <=
                  BTB782_NIXO_FIRE_MAX_DURATION_MS,
              "default Nixo fire duration above max");

static HardwareSerial &JetsonSerial = Serial2;
static CRGB leds[BTB782_NUM_LEDS];

struct PiezoSample {
  int raw = 0;
  int left = 0;
  int right = 0;
  int front = 0;
  const char *source = "piezo:left";
};

enum FireState : uint8_t {
  FIRE_IDLE,
  FIRE_PREFIRE_DELAY,
  FIRE_RELAY_WAIT1,
  FIRE_RELAY_WAIT2,
};

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

static uint32_t last_led_show_ms = 0;
static uint32_t last_blink_ms = 0;
static uint32_t last_down_blink_ms = 0;
static bool blink_on = false;
static bool down_blink_on = false;
static bool led_dirty = true;

static FireState fire_state = FIRE_IDLE;
static uint32_t fire_timer_ms = 0;
static uint32_t cooldown_started_ms = 0;
static uint32_t active_fire_duration_ms = BTB782_NIXO_FIRE_DEFAULT_DURATION_MS;

static String usb_line;
static String jetson_line;

static bool relay2Enabled() { return BTB782_NIXO_RELAY2_PIN >= 0; }

static const char *fireStateName() {
  switch (fire_state) {
  case FIRE_IDLE:
    return "idle";
  case FIRE_PREFIRE_DELAY:
    return "prefire_delay";
  case FIRE_RELAY_WAIT1:
    return "relay_wait1";
  case FIRE_RELAY_WAIT2:
    return "relay_wait2";
  }
  return "unknown";
}

static bool isFiring() { return fire_state != FIRE_IDLE; }

static uint32_t clampFireDuration(uint32_t durationMs) {
  if (durationMs < BTB782_NIXO_FIRE_MIN_DURATION_MS)
    return BTB782_NIXO_FIRE_MIN_DURATION_MS;
  if (durationMs > BTB782_NIXO_FIRE_MAX_DURATION_MS)
    return BTB782_NIXO_FIRE_MAX_DURATION_MS;
  return durationMs;
}

static uint32_t cooldownRemainingMs(uint32_t now) {
  if (cooldown_started_ms == 0)
    return 0;
  const uint32_t elapsed = now - cooldown_started_ms;
  if (elapsed >= BTB782_NIXO_FIRE_COOLDOWN_MS)
    return 0;
  return BTB782_NIXO_FIRE_COOLDOWN_MS - elapsed;
}

static void configureRelayPinOff(int pin) {
  if (pin < 0)
    return;
  digitalWrite(pin, BTB782_NIXO_RELAY_OFF_LEVEL);
  pinMode(pin,
          BTB782_NIXO_RELAY_OFF_LEVEL == HIGH ? INPUT_PULLUP : INPUT_PULLDOWN);
  pinMode(pin, OUTPUT);
  digitalWrite(pin, BTB782_NIXO_RELAY_OFF_LEVEL);
}

static void relayOff() {
  if (relay2Enabled())
    digitalWrite(BTB782_NIXO_RELAY2_PIN, BTB782_NIXO_RELAY_OFF_LEVEL);
  digitalWrite(BTB782_NIXO_RELAY1_PIN, BTB782_NIXO_RELAY_OFF_LEVEL);
}

static void beginCooldown(uint32_t now) {
  if (BTB782_NIXO_FIRE_COOLDOWN_MS == 0) {
    cooldown_started_ms = 0;
    return;
  }
  cooldown_started_ms = now;
  Serial.printf("[FIRE] cooldown start duration_ms=%lu\n",
                (unsigned long)BTB782_NIXO_FIRE_COOLDOWN_MS);
}

static void stopFireSequence(const char *source) {
  const bool wasFiring = isFiring();
  relayOff();
  fire_state = FIRE_IDLE;
  if (wasFiring)
    beginCooldown(millis());
  Serial.printf("[FIRE] stop source=%s%s\n", source,
                wasFiring ? "" : " already_idle=true");
}

static bool startFireSequence(uint32_t durationMs, const char *source) {
  const uint32_t now = millis();

  if (isFiring()) {
    Serial.printf(
        "[FIRE] ignored source=%s reason=already_firing state=%s\n", source,
        fireStateName());
    return false;
  }

  const uint32_t remainingMs = cooldownRemainingMs(now);
  if (remainingMs > 0) {
    Serial.printf(
        "[FIRE] ignored source=%s reason=cooldown remaining_ms=%lu\n", source,
        (unsigned long)remainingMs);
    return false;
  }

  active_fire_duration_ms = clampFireDuration(durationMs);
  fire_state = FIRE_PREFIRE_DELAY;
  fire_timer_ms = now;
  Serial.printf(
      "[FIRE] start source=%s duration_ms=%lu prefire_delay_ms=%lu\n", source,
      (unsigned long)active_fire_duration_ms,
      (unsigned long)BTB782_NIXO_PREFIRE_DELAY_MS);
  return true;
}

static void updateFireSequence(uint32_t now) {
  switch (fire_state) {
  case FIRE_IDLE:
    return;

  case FIRE_PREFIRE_DELAY:
    if (now - fire_timer_ms >= BTB782_NIXO_PREFIRE_DELAY_MS) {
      digitalWrite(BTB782_NIXO_RELAY1_PIN, BTB782_NIXO_RELAY_ON_LEVEL);
      if (relay2Enabled())
        digitalWrite(BTB782_NIXO_RELAY2_PIN, BTB782_NIXO_RELAY_OFF_LEVEL);
      fire_state = FIRE_RELAY_WAIT1;
      fire_timer_ms = now;
      Serial.printf("[RELAY] CH1 ON pin=%d level=%d readback=%d\n",
                    BTB782_NIXO_RELAY1_PIN, BTB782_NIXO_RELAY_ON_LEVEL,
                    digitalRead(BTB782_NIXO_RELAY1_PIN));
    }
    return;

  case FIRE_RELAY_WAIT1:
    if (!relay2Enabled()) {
      if (now - fire_timer_ms >= active_fire_duration_ms) {
        relayOff();
        fire_state = FIRE_IDLE;
        Serial.printf("[RELAY] CH1 OFF pin=%d level=%d readback=%d\n",
                      BTB782_NIXO_RELAY1_PIN, BTB782_NIXO_RELAY_OFF_LEVEL,
                      digitalRead(BTB782_NIXO_RELAY1_PIN));
        Serial.println("[RELAY] ALL OFF / FIRE done");
        beginCooldown(now);
      }
      return;
    }

    if (now - fire_timer_ms >= BTB782_NIXO_RELAY_DELAY1_MS) {
      digitalWrite(BTB782_NIXO_RELAY2_PIN, BTB782_NIXO_RELAY_ON_LEVEL);
      fire_state = FIRE_RELAY_WAIT2;
      fire_timer_ms = now;
      Serial.printf("[RELAY] CH2 ON pin=%d level=%d readback=%d\n",
                    BTB782_NIXO_RELAY2_PIN, BTB782_NIXO_RELAY_ON_LEVEL,
                    digitalRead(BTB782_NIXO_RELAY2_PIN));
    }
    return;

  case FIRE_RELAY_WAIT2:
    if (now - fire_timer_ms >= active_fire_duration_ms) {
      digitalWrite(BTB782_NIXO_RELAY2_PIN, BTB782_NIXO_RELAY_OFF_LEVEL);
      Serial.printf("[RELAY] CH2 OFF pin=%d level=%d readback=%d\n",
                    BTB782_NIXO_RELAY2_PIN, BTB782_NIXO_RELAY_OFF_LEVEL,
                    digitalRead(BTB782_NIXO_RELAY2_PIN));
      digitalWrite(BTB782_NIXO_RELAY1_PIN, BTB782_NIXO_RELAY_OFF_LEVEL);
      fire_state = FIRE_IDLE;
      Serial.printf("[RELAY] CH1 OFF pin=%d level=%d readback=%d\n",
                    BTB782_NIXO_RELAY1_PIN, BTB782_NIXO_RELAY_OFF_LEVEL,
                    digitalRead(BTB782_NIXO_RELAY1_PIN));
      Serial.println("[RELAY] ALL OFF / FIRE done");
      beginCooldown(now);
    }
    return;
  }
}

static PiezoSample readPiezoSample() {
  PiezoSample sample;
  sample.left = analogRead(BTB782_LEFT_PIEZO_PIN);
  sample.right = analogRead(BTB782_RIGHT_PIEZO_PIN);
  sample.front = analogRead(BTB782_FRONT_PIEZO_PIN);
  sample.raw = sample.left;
  sample.source = "piezo:left";
  if (sample.right > sample.raw) {
    sample.raw = sample.right;
    sample.source = "piezo:right";
  }
  if (sample.front > sample.raw) {
    sample.raw = sample.front;
    sample.source = "piezo:front";
  }
  return sample;
}

static void markLedDirty() { led_dirty = true; }

static float hpFillRatio() {
  if (BTB782_HP_MAX < 1)
    return 1.0f;
  return constrain(static_cast<float>(hp_remaining) /
                       static_cast<float>(BTB782_HP_MAX),
                   0.0f, 1.0f);
}

static void setHpBarGroup(int group1Based, const CRGB &color) {
  if (group1Based < 1 || group1Based > BTB782_HP_BAR_GROUP_COUNT)
    return;

  // Same 84-LED HP harness mapping as firmware/go2/display/bar_display.cpp:
  // group 1  -> LEDs 1, 56, 57
  // group 2  -> LEDs 2, 55, 58
  // ...
  // group 28 -> LEDs 28, 29, 84
  const int row1Index = group1Based - 1;
  const int row2Index = 2 * BTB782_HP_BAR_GROUP_COUNT - group1Based;
  const int row3Index = 2 * BTB782_HP_BAR_GROUP_COUNT - 1 + group1Based;

  leds[row1Index] = color;
  leds[row2Index] = color;
  leds[row3Index] = color;
}

static void renderGroupedHpBar(float fillRatio, const CRGB &healthyColor,
                               const CRGB &damagedColor) {
  const int healthyGroups =
      constrain(static_cast<int>(fillRatio * BTB782_HP_BAR_GROUP_COUNT + 0.5f),
                0, BTB782_HP_BAR_GROUP_COUNT);
  for (int group = 1; group <= BTB782_HP_BAR_GROUP_COUNT; ++group) {
    setHpBarGroup(group, group <= healthyGroups ? healthyColor : damagedColor);
  }
}

static void renderHpBar(uint32_t now, bool force = false) {
  if (now - last_blink_ms >= BTB782_LED_BLINK_MS) {
    last_blink_ms = now;
    blink_on = !blink_on;
    markLedDirty();
  }

  if (down || hp_remaining <= 0) {
    if (now - last_down_blink_ms >= BTB782_LED_DEAD_BLINK_MS) {
      last_down_blink_ms = now;
      down_blink_on = !down_blink_on;
      markLedDirty();
    }
  }

  if (!force && !led_dirty)
    return;
  if (!force && now - last_led_show_ms < BTB782_LED_SHOW_PERIOD_MS)
    return;

  if (down || hp_remaining <= 0) {
    fill_solid(leds, BTB782_NUM_LEDS, down_blink_on ? CRGB::Red : CRGB::Black);
  } else {
    CRGB healthyColor = CRGB::Green;
    CRGB damagedColor = CRGB::Red;
    if (now - last_hit_flash_ms < 80U) {
      healthyColor = blink_on ? CRGB::White : CRGB::Green;
      damagedColor = blink_on ? CRGB::White : CRGB::Red;
    } else if (hpFillRatio() <= 0.25f) {
      healthyColor = CRGB::Orange;
    }
    renderGroupedHpBar(hpFillRatio(), healthyColor, damagedColor);
  }

  FastLED.show();
  last_led_show_ms = now;
  led_dirty = false;
}

static void markHitFlash(uint32_t now) {
  last_hit_flash_ms = now;
  blink_on = true;
  markLedDirty();
}

static void sendHpToJetson(uint32_t now, bool force = false) {
  if (!force && now - last_hp_tx_ms < BTB782_HP_TX_PERIOD_MS)
    return;
  last_hp_tx_ms = now;
  JetsonSerial.println(hp_remaining);
}

static void printStatusTo(Stream &out, const char *source) {
  const PiezoSample piezo = readPiezoSample();
  const uint32_t now = millis();
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
  out.print(piezo.raw);
  out.print(",\"piezo_left_raw\":");
  out.print(piezo.left);
  out.print(",\"piezo_right_raw\":");
  out.print(piezo.right);
  out.print(",\"piezo_front_raw\":");
  out.print(piezo.front);
  out.print(",\"piezo_left_pin\":");
  out.print(BTB782_LEFT_PIEZO_PIN);
  out.print(",\"piezo_right_pin\":");
  out.print(BTB782_RIGHT_PIEZO_PIN);
  out.print(",\"piezo_front_pin\":");
  out.print(BTB782_FRONT_PIEZO_PIN);
  out.print(",\"fire_state\":\"");
  out.print(fireStateName());
  out.print("\",\"fire_cooldown_remaining_ms\":");
  out.print(cooldownRemainingMs(now));
  out.print(",\"nixo_relay1_pin\":");
  out.print(BTB782_NIXO_RELAY1_PIN);
  out.print(",\"nixo_relay2_pin\":");
  out.print(BTB782_NIXO_RELAY2_PIN);
  out.print(",\"uart_rx_pin\":");
  out.print(BTB782_UART_RX_PIN);
  out.print(",\"uart_tx_pin\":");
  out.print(BTB782_UART_TX_PIN);
  out.print(
      ",\"led_type\":\"WS2815\",\"color_order\":\"RGB\",\"hp_bar_groups\":");
  out.print(BTB782_HP_BAR_GROUP_COUNT);
  out.println("}");
}

static void printStatus(const char *source) {
  printStatusTo(Serial, source);
  if (strcmp(source, "jetson") == 0)
    printStatusTo(JetsonSerial, source);
}

static void resetHp(const char *source) {
  hp_remaining = BTB782_HP_MAX;
  accepted_hit_count = 0;
  down = false;
  piezo_armed = true;
  capture_active = false;
  quiet_started_ms = millis();
  down_blink_on = false;
  markHitFlash(millis());
  sendHpToJetson(millis(), true);
  Serial.printf("[reset] source=%s hp=%d\n", source, hp_remaining);
  if (strcmp(source, "jetson") == 0)
    JetsonSerial.println(hp_remaining);
}

static void acceptHit(int peak_raw, const char *source) {
  if (down || hp_remaining <= 0)
    return;
  accepted_hit_count++;
  hp_remaining = max(0, hp_remaining - 1);
  down = hp_remaining <= 0;
  markHitFlash(millis());
  sendHpToJetson(millis(), true);
  Serial.printf("[hit] source=%s peak=%d hp=%d/%d down=%d count=%lu\n", source,
                peak_raw, hp_remaining, BTB782_HP_MAX, down ? 1 : 0,
                static_cast<unsigned long>(accepted_hit_count));
}

static bool isIgnoredLeadingChar(char c) {
  return c == '\0' || c == ' ' || c == '\t';
}

static bool sourceCanFire(const char *source) {
  return strcmp(source, "jetson") == 0;
}

static void handleCommandLine(String line, const char *source) {
  line.trim();
  if (line.length() == 0)
    return;
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
    acceptHit(readPiezoSample().raw, source);
    return;
  }
  if (lower == "x" || lower == "stop-fire" || lower == "fire off") {
    stopFireSequence(source);
    return;
  }
  if (lower == "f" || lower == "fire") {
    if (!sourceCanFire(source)) {
      Serial.printf("[FIRE] ignored source=%s reason=jetson_uart_required\n",
                    source);
      return;
    }
    startFireSequence(BTB782_NIXO_FIRE_DEFAULT_DURATION_MS, source);
    return;
  }

  Serial.printf("[cmd] ignored source=%s line=%s\n", source, line.c_str());
  if (strcmp(source, "jetson") == 0) {
    JetsonSerial.print("{\"event\":\"command_ignored\",\"source\":\"");
    JetsonSerial.print(source);
    JetsonSerial.println("\"}");
  }
}

static void pollCommandStream(Stream &stream, String &line,
                              const char *source) {
  while (stream.available() > 0) {
    const char c = static_cast<char>(stream.read());
    if (c == '\r')
      continue;
    if (c == '\n') {
      handleCommandLine(line, source);
      line = "";
      continue;
    }
    if (line.length() == 0 && isIgnoredLeadingChar(c))
      continue;
    if (line.length() < 256) {
      line += c;
    } else {
      line = "";
      Serial.printf("[cmd] source=%s line too long; dropped\n", source);
    }
  }
}

static void pollPiezo(uint32_t now) {
  const PiezoSample piezo = readPiezoSample();

  if (capture_active) {
    if (piezo.raw > capture_peak_raw)
      capture_peak_raw = piezo.raw;
    if (now - capture_started_ms >= BTB782_CAPTURE_WINDOW_MS) {
      capture_active = false;
    }
    return;
  }

  if (!piezo_armed) {
    if (piezo.raw <= BTB782_PIEZO_REARM_RAW) {
      if (quiet_started_ms == 0)
        quiet_started_ms = now;
      if (now - quiet_started_ms >= BTB782_REARM_STABLE_MS) {
        piezo_armed = true;
      }
    } else {
      quiet_started_ms = 0;
    }
    return;
  }

  if (!down && piezo.raw >= BTB782_PIEZO_THRESHOLD_RAW) {
    capture_active = true;
    capture_started_ms = now;
    capture_peak_raw = piezo.raw;
    piezo_armed = false;
    quiet_started_ms = 0;
    acceptHit(piezo.raw, piezo.source);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  JetsonSerial.begin(BTB782_UART_BAUD, SERIAL_8N1, BTB782_UART_RX_PIN,
                     BTB782_UART_TX_PIN);
  analogReadResolution(12);
  pinMode(BTB782_LEFT_PIEZO_PIN, INPUT);
  pinMode(BTB782_RIGHT_PIEZO_PIN, INPUT);
  pinMode(BTB782_FRONT_PIEZO_PIN, INPUT);
  if (relay2Enabled())
    configureRelayPinOff(BTB782_NIXO_RELAY2_PIN);
  configureRelayPinOff(BTB782_NIXO_RELAY1_PIN);
  relayOff();

  FastLED.addLeds<WS2815, BTB782_LED_PIN, RGB>(leds, BTB782_NUM_LEDS);
  FastLED.setBrightness(BTB782_LED_BRIGHTNESS);
  FastLED.setMaxPowerInVoltsAndMilliamps(BTB782_LED_MAX_VOLTS,
                                         BTB782_LED_MAX_MA);
  fill_solid(leds, BTB782_NUM_LEDS, CRGB::Black);
  FastLED.show();

  quiet_started_ms = millis();
  renderHpBar(millis(), true);
  sendHpToJetson(millis(), true);

  Serial.println("[btb782] standalone ESP UART HP sketch booted");
  Serial.printf(
      "[pin] uart2 rx=D%d tx=D%d baud=%lu | led=D%d type=WS2815 order=RGB "
      "count=%d groups=%d leds_per_group=%d brightness=%d | piezo left=D%d "
      "right=D%d front=D%d threshold=%d rearm=%d | nixo relay1=D%d relay2=D%d "
      "on=%d off=%d fire_ms=%lu cooldown_ms=%lu | hp_max=%d\n",
      BTB782_UART_RX_PIN, BTB782_UART_TX_PIN,
      static_cast<unsigned long>(BTB782_UART_BAUD), BTB782_LED_PIN,
      BTB782_NUM_LEDS, BTB782_HP_BAR_GROUP_COUNT, BTB782_HP_BAR_LEDS_PER_GROUP,
      BTB782_LED_BRIGHTNESS, BTB782_LEFT_PIEZO_PIN, BTB782_RIGHT_PIEZO_PIN,
      BTB782_FRONT_PIEZO_PIN, BTB782_PIEZO_THRESHOLD_RAW,
      BTB782_PIEZO_REARM_RAW, BTB782_NIXO_RELAY1_PIN, BTB782_NIXO_RELAY2_PIN,
      BTB782_NIXO_RELAY_ON_LEVEL, BTB782_NIXO_RELAY_OFF_LEVEL,
      (unsigned long)BTB782_NIXO_FIRE_DEFAULT_DURATION_MS,
      (unsigned long)BTB782_NIXO_FIRE_COOLDOWN_MS, BTB782_HP_MAX);
  Serial.println("[cmd] UART: s=status, h=simulate hit, f/fire=Nixo fire, "
                 "x=stop fire, 2/r/reset=reset HP");
  printStatus("boot");
}

void loop() {
  const uint32_t now = millis();
  pollCommandStream(Serial, usb_line, "usb");
  pollCommandStream(JetsonSerial, jetson_line, "jetson");
  pollPiezo(now);
  updateFireSequence(now);
  renderHpBar(now);
  sendHpToJetson(now);

  if (now - last_debug_ms >= BTB782_DEBUG_PERIOD_MS) {
    last_debug_ms = now;
    const PiezoSample piezo = readPiezoSample();
    Serial.printf("[debug] hp=%d/%d raw=%d left=%d right=%d front=%d armed=%d "
                  "down=%d fire=%s cooldown_ms=%lu\n",
                  hp_remaining, BTB782_HP_MAX, piezo.raw, piezo.left,
                  piezo.right, piezo.front, piezo_armed ? 1 : 0, down ? 1 : 0,
                  fireStateName(), (unsigned long)cooldownRemainingMs(now));
  }

  yield();
}
