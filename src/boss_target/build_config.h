#pragma once

#include <Arduino.h>

#define BATTLEBANG_BOSS_TARGET_STRINGIFY_INNER(value) #value
#define BATTLEBANG_BOSS_TARGET_STRINGIFY(value) BATTLEBANG_BOSS_TARGET_STRINGIFY_INNER(value)

namespace boss_target {

static constexpr const char* TARGET_ID_PREFIX = "boss_target";
static constexpr const char* FIRMWARE_NAME = "battlebang-boss-target";
static constexpr uint32_t SERIAL_BAUD = 115200;

// Current boss-target board profile: 4 target LED rings, 4 piezo DO inputs, 1 HP bar.
// Future 6-target hardware should add a new build profile/env with a different capacity/pin map.
static constexpr uint8_t kMaxTargets = 4;
static constexpr uint8_t DEFAULT_TARGET_COUNT = 4;

static constexpr uint16_t RING_NUM_LEDS = 40;
static constexpr uint16_t HP_BAR_NUM_LEDS = 92;
static constexpr uint16_t MAX_RING_NUM_LEDS = 120;
static constexpr uint16_t MAX_HP_BAR_NUM_LEDS = 240;

static constexpr int RING1_PIN = 23;
static constexpr int RING2_PIN = 21;
static constexpr int RING3_PIN = 18;
static constexpr int RING4_PIN = 17;
static constexpr int RING_PINS[kMaxTargets] = {RING1_PIN, RING2_PIN, RING3_PIN, RING4_PIN};

static constexpr int PIEZO1_DO_PIN = 27;
static constexpr int PIEZO2_DO_PIN = 32;
static constexpr int PIEZO3_DO_PIN = 33;
static constexpr int PIEZO4_DO_PIN = 25;
static constexpr int PIEZO_DO_PINS[kMaxTargets] = {PIEZO1_DO_PIN, PIEZO2_DO_PIN, PIEZO3_DO_PIN, PIEZO4_DO_PIN};

static constexpr int HP_BAR_PIN = 26;

static constexpr uint8_t LED_BRIGHTNESS = 80;
static constexpr uint8_t LED_MAX_VOLTS = 5;
static constexpr uint16_t LED_MAX_MA = 6000;
static constexpr uint32_t LED_SHOW_PERIOD_MS = 16;
static constexpr const char* LED_TYPE_NAME = "WS2811";
static constexpr const char* COLOR_ORDER_NAME = "RGB";

static constexpr uint16_t HP_MAX = 10;
static constexpr uint16_t DAMAGE_PER_HIT = 1;
static constexpr uint8_t HP_PHASE_COUNT = 3;
static constexpr uint32_t TARGET_DURATION_MS = 2500;
static constexpr uint32_t HIT_COOLDOWN_MS = 300;
static constexpr uint32_t DIGITAL_ISR_DEBOUNCE_US = 20000;
static constexpr uint32_t HIT_FLASH_MS = 60;
static constexpr uint32_t BLINK_MS = 250;
static constexpr uint32_t DEAD_BLINK_MS = 300;

static constexpr uint32_t HP_GREEN = 0x00FF00;
static constexpr uint32_t HP_YELLOW = 0xFFFF00;
static constexpr uint32_t HP_RED = 0xFF0000;
static constexpr uint32_t TARGET_ACTIVE_BLUE = 0x0000FF;
static constexpr uint32_t TARGET_HIT_FLASH_WHITE = 0xFFFFFF;

static_assert(kMaxTargets == 4, "Current boss-target profile is the 4-channel board");
static_assert(DEFAULT_TARGET_COUNT > 0 && DEFAULT_TARGET_COUNT <= kMaxTargets, "invalid default target count");
static_assert(RING_NUM_LEDS > 0 && RING_NUM_LEDS <= MAX_RING_NUM_LEDS, "invalid ring LED count");
static_assert(HP_BAR_NUM_LEDS > 0 && HP_BAR_NUM_LEDS <= MAX_HP_BAR_NUM_LEDS, "invalid HP bar LED count");
static_assert(HP_MAX > 0, "HP_MAX must be positive");
static_assert(DAMAGE_PER_HIT > 0, "DAMAGE_PER_HIT must be positive");
static_assert(HP_PHASE_COUNT > 0, "HP_PHASE_COUNT must be positive");

}  // namespace boss_target
