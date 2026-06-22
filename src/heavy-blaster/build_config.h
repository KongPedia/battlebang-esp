#pragma once

#include <Arduino.h>

#define BATTLEBANG_HEAVY_BLASTER_STRINGIFY_INNER(value) #value
#define BATTLEBANG_HEAVY_BLASTER_STRINGIFY(value) BATTLEBANG_HEAVY_BLASTER_STRINGIFY_INNER(value)

namespace heavy_blaster {

static constexpr const char* TARGET_ID_PREFIX = "heavy-blaster";
static constexpr const char* FIRMWARE_NAME = "battlebang-heavy-blaster";
static constexpr uint32_t SERIAL_BAUD = 115200;

// Current Heavy Blaster prototype profile: 4 WS2812B 8x8 matrices + one relay.
static constexpr uint8_t kSlotCount = 4;
static constexpr uint8_t DEFAULT_REQUIRED_SLOTS = 4;

static constexpr int MATRIX1_PIN = 23;
static constexpr int MATRIX2_PIN = 22;
static constexpr int MATRIX3_PIN = 21;
static constexpr int MATRIX4_PIN = 19;
static constexpr int MATRIX_PINS[kSlotCount] = {23, 22, 21, 19};
static constexpr int RELAY_PIN = 26;
static constexpr bool RELAY_ACTIVE_LOW = false;

static constexpr uint8_t MATRIX_WIDTH = 8;
static constexpr uint8_t MATRIX_HEIGHT = 8;
static constexpr uint16_t MATRIX_NUM_LEDS = 64;
static constexpr uint16_t MAX_MATRIX_NUM_LEDS = 64;
static constexpr uint8_t LED_BRIGHTNESS = 30;
static constexpr uint8_t LED_MAX_VOLTS = 5;
static constexpr uint16_t LED_MAX_MA = 2000;
static constexpr uint32_t LED_SHOW_PERIOD_MS = 16;
static constexpr const char* LED_TYPE_NAME = "WS2812B";
static constexpr const char* COLOR_ORDER_NAME = "GRB";
static constexpr uint32_t SLOT_ACTIVE_YELLOW = 0xFFFF00;

static constexpr uint32_t EFFECT_UPDATE_MS = 120;
static constexpr uint16_t ARROW_FRAMES = 16;
static constexpr uint16_t RAINBOW_FRAMES = 28;
static constexpr uint32_t PRE_UNLOCK_EFFECT_MS = 10000;
static constexpr uint32_t FADE_OUT_MS = 4000;
static constexpr uint16_t BLINK_BPM = 40;
static constexpr uint32_t PRE_EFFECT_UPDATE_MS = 20;
static constexpr uint8_t RAINBOW_SPEED = 8;

static_assert(kSlotCount == 4, "Current heavy-blaster profile is the 4-matrix board");
static_assert(DEFAULT_REQUIRED_SLOTS > 0 && DEFAULT_REQUIRED_SLOTS <= kSlotCount, "invalid required slot count");
static_assert(MATRIX_WIDTH * MATRIX_HEIGHT == MATRIX_NUM_LEDS, "matrix dimensions must match LED count");
static_assert(MAX_MATRIX_NUM_LEDS >= MATRIX_NUM_LEDS, "max matrix LED count must hold default matrix");
static_assert(BLINK_BPM > 0, "blink BPM must be positive");

}  // namespace heavy_blaster
