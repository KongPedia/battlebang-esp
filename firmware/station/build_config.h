#pragma once

#include <Arduino.h>

#define BATTLEBANG_STATION_STRINGIFY_INNER(value) #value
#define BATTLEBANG_STATION_STRINGIFY(value) BATTLEBANG_STATION_STRINGIFY_INNER(value)

namespace station {

static constexpr const char* STATION_ID_PREFIX = "station";
static constexpr const char* FIRMWARE_NAME = "battlebang-station";
static constexpr uint32_t SERIAL_BAUD = 115200;

// Current target-station board profile: one piezo AO input and one local station LED strip/ring.
static constexpr int LED_PIN = 33;
static constexpr int PIEZO_AO_PIN = 32;
static constexpr uint16_t LED_NUM_LEDS = 60;
static constexpr uint16_t MAX_LED_NUM_LEDS = 180;
static constexpr uint8_t LED_BRIGHTNESS = 120;
static constexpr uint8_t LED_MAX_VOLTS = 5;
static constexpr uint16_t LED_MAX_MA = 3000;
static constexpr uint32_t LED_SHOW_PERIOD_MS = 16;
static constexpr const char* LED_TYPE_NAME = "WS2812B";
static constexpr const char* COLOR_ORDER_NAME = "RGB";

static constexpr uint16_t PIEZO_AO_THRESHOLD = 3000;
static constexpr uint16_t PIEZO_AO_RELEASE = 1200;
static constexpr uint32_t PIEZO_AO_SAMPLE_PERIOD_MS = 2;
static constexpr uint32_t PIEZO_AO_SETTLE_US = 80;
static constexpr uint32_t HIT_COOLDOWN_MS = 300;
static constexpr uint32_t HIT_FLASH_MS = 220;
static constexpr uint32_t HEARTBEAT_INTERVAL_MS = 5000;
static constexpr uint32_t AUTO_RESET_MS = 0;

static constexpr uint32_t WAITING_COLOR = 0x00FF00;
static constexpr uint32_t CAPTURED_COLOR = 0xFF0000;
static constexpr uint32_t HIT_FLASH_COLOR = 0xFFFFFF;
static constexpr uint16_t WAITING_BREATH_BPM = 20;
static constexpr uint8_t WAITING_BREATH_MIN = 5;
static constexpr uint8_t WAITING_BREATH_MAX = 220;

static_assert(LED_NUM_LEDS > 0 && LED_NUM_LEDS <= MAX_LED_NUM_LEDS, "invalid station LED count");
static_assert(PIEZO_AO_THRESHOLD > PIEZO_AO_RELEASE, "piezo threshold must be above release");
static_assert(HIT_COOLDOWN_MS > 0, "hit cooldown must be positive");

}  // namespace station
