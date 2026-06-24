#pragma once

#include <Arduino.h>

#if __has_include("local_secrets.h") && !defined(BATTLEBANG_SKIP_LOCAL_SECRETS)
#include "local_secrets.h"
#elif __has_include("../local_secrets.h") &&                                   \
                    !defined(BATTLEBANG_SKIP_LOCAL_SECRETS)
// Compatibility include path used by older local operator setups.
#include "../local_secrets.h"
#endif

// Local secret aliases. The operator-facing local_secrets.h should use ESP_*
// names; BATTLEBANG_* remains the internal firmware macro namespace.
#if defined(ESP_ROBOT_ID) && !defined(BATTLEBANG_ROBOT_ID)
#define BATTLEBANG_ROBOT_ID ESP_ROBOT_ID
#endif

#if defined(ESP_WIFI_SSID) && !defined(BATTLEBANG_WIFI_SSID)
#define BATTLEBANG_WIFI_SSID ESP_WIFI_SSID
#endif

#if defined(ESP_WIFI_PASSWORD) && !defined(BATTLEBANG_WIFI_PASSWORD)
#define BATTLEBANG_WIFI_PASSWORD ESP_WIFI_PASSWORD
#endif

#if defined(ESP_MQTT_HOST) && !defined(BATTLEBANG_MQTT_HOST)
#define BATTLEBANG_MQTT_HOST ESP_MQTT_HOST
#endif

#if defined(ESP_MQTT_PORT) && !defined(BATTLEBANG_MQTT_PORT)
#define BATTLEBANG_MQTT_PORT ESP_MQTT_PORT
#endif

#if defined(ESP_MQTT_TOPIC_PREFIX) && !defined(BATTLEBANG_MQTT_TOPIC_PREFIX)
#define BATTLEBANG_MQTT_TOPIC_PREFIX ESP_MQTT_TOPIC_PREFIX
#endif


// Overrides injected by scripts/go2_config.py from PlatformIO profile/shell
// env. These intentionally apply after local_secrets.h so explicit env/profile
// builds win without editing the gitignored local file.
#ifdef BATTLEBANG_BUILD_ROBOT_ID
#undef BATTLEBANG_ROBOT_ID
#define BATTLEBANG_ROBOT_ID BATTLEBANG_BUILD_ROBOT_ID
#endif

#ifdef BATTLEBANG_BUILD_WIFI_SSID
#undef BATTLEBANG_WIFI_SSID
#define BATTLEBANG_WIFI_SSID BATTLEBANG_BUILD_WIFI_SSID
#endif

#ifdef BATTLEBANG_BUILD_WIFI_PASSWORD
#undef BATTLEBANG_WIFI_PASSWORD
#define BATTLEBANG_WIFI_PASSWORD BATTLEBANG_BUILD_WIFI_PASSWORD
#endif

#ifdef BATTLEBANG_BUILD_MQTT_HOST
#undef BATTLEBANG_MQTT_HOST
#define BATTLEBANG_MQTT_HOST BATTLEBANG_BUILD_MQTT_HOST
#endif

#ifdef BATTLEBANG_BUILD_MQTT_PORT
#undef BATTLEBANG_MQTT_PORT
#define BATTLEBANG_MQTT_PORT BATTLEBANG_BUILD_MQTT_PORT
#endif

#ifdef BATTLEBANG_BUILD_MQTT_TOPIC_PREFIX
#undef BATTLEBANG_MQTT_TOPIC_PREFIX
#define BATTLEBANG_MQTT_TOPIC_PREFIX BATTLEBANG_BUILD_MQTT_TOPIC_PREFIX
#endif


#ifdef BATTLEBANG_BUILD_HIT_COOLDOWN_MS
#undef BATTLEBANG_HIT_COOLDOWN_MS
#define BATTLEBANG_HIT_COOLDOWN_MS BATTLEBANG_BUILD_HIT_COOLDOWN_MS
#endif

#ifdef BATTLEBANG_BUILD_OFFLINE_HIT_QUEUE_CAPACITY
#undef BATTLEBANG_OFFLINE_HIT_QUEUE_CAPACITY
#define BATTLEBANG_OFFLINE_HIT_QUEUE_CAPACITY                                  \
  BATTLEBANG_BUILD_OFFLINE_HIT_QUEUE_CAPACITY
#endif

#ifdef BATTLEBANG_BUILD_OFFLINE_HIT_QUEUE_FLUSH_INTERVAL_MS
#undef BATTLEBANG_OFFLINE_HIT_QUEUE_FLUSH_INTERVAL_MS
#define BATTLEBANG_OFFLINE_HIT_QUEUE_FLUSH_INTERVAL_MS                         \
  BATTLEBANG_BUILD_OFFLINE_HIT_QUEUE_FLUSH_INTERVAL_MS
#endif

#ifdef BATTLEBANG_BUILD_LED_PIN
#undef BATTLEBANG_LED_PIN
#define BATTLEBANG_LED_PIN BATTLEBANG_BUILD_LED_PIN
#endif

#ifdef BATTLEBANG_BUILD_NUM_LEDS
#undef BATTLEBANG_NUM_LEDS
#define BATTLEBANG_NUM_LEDS BATTLEBANG_BUILD_NUM_LEDS
#endif

#ifdef BATTLEBANG_BUILD_LED_BRIGHTNESS
#undef BATTLEBANG_LED_BRIGHTNESS
#define BATTLEBANG_LED_BRIGHTNESS BATTLEBANG_BUILD_LED_BRIGHTNESS
#endif


#ifdef BATTLEBANG_BUILD_T1_DO_PIN
#undef BATTLEBANG_T1_DO_PIN
#define BATTLEBANG_T1_DO_PIN BATTLEBANG_BUILD_T1_DO_PIN
#endif

#ifdef BATTLEBANG_BUILD_T2_DO_PIN
#undef BATTLEBANG_T2_DO_PIN
#define BATTLEBANG_T2_DO_PIN BATTLEBANG_BUILD_T2_DO_PIN
#endif

#ifdef BATTLEBANG_BUILD_PIEZO_AO_PIN
#undef BATTLEBANG_PIEZO_AO_PIN
#define BATTLEBANG_PIEZO_AO_PIN BATTLEBANG_BUILD_PIEZO_AO_PIN
#endif

#ifdef BATTLEBANG_BUILD_PIEZO_AO_THRESHOLD_RAW
#undef BATTLEBANG_PIEZO_AO_THRESHOLD_RAW
#define BATTLEBANG_PIEZO_AO_THRESHOLD_RAW                                      \
  BATTLEBANG_BUILD_PIEZO_AO_THRESHOLD_RAW
#endif

#ifdef BATTLEBANG_BUILD_PIEZO_AO_REARM_RAW
#undef BATTLEBANG_PIEZO_AO_REARM_RAW
#define BATTLEBANG_PIEZO_AO_REARM_RAW BATTLEBANG_BUILD_PIEZO_AO_REARM_RAW
#endif

#ifdef BATTLEBANG_BUILD_PIEZO_AO_CAPTURE_WINDOW_MS
#undef BATTLEBANG_PIEZO_AO_CAPTURE_WINDOW_MS
#define BATTLEBANG_PIEZO_AO_CAPTURE_WINDOW_MS                                  \
  BATTLEBANG_BUILD_PIEZO_AO_CAPTURE_WINDOW_MS
#endif

#ifdef BATTLEBANG_BUILD_PIEZO_AO_DEBUG_PERIOD_MS
#undef BATTLEBANG_PIEZO_AO_DEBUG_PERIOD_MS
#define BATTLEBANG_PIEZO_AO_DEBUG_PERIOD_MS                                    \
  BATTLEBANG_BUILD_PIEZO_AO_DEBUG_PERIOD_MS
#endif

#ifndef BATTLEBANG_ROBOT_ID
#define BATTLEBANG_ROBOT_ID "go2_05"
#endif

#ifndef BATTLEBANG_WIFI_SSID
#define BATTLEBANG_WIFI_SSID ""
#endif

#ifndef BATTLEBANG_WIFI_PASSWORD
#define BATTLEBANG_WIFI_PASSWORD ""
#endif

#ifndef BATTLEBANG_MQTT_HOST
#define BATTLEBANG_MQTT_HOST ""
#endif

#ifndef BATTLEBANG_MQTT_PORT
#define BATTLEBANG_MQTT_PORT 1883
#endif

#ifndef BATTLEBANG_MQTT_TOPIC_PREFIX
#define BATTLEBANG_MQTT_TOPIC_PREFIX "battlebang/hit"
#endif


#ifndef BATTLEBANG_HIT_COOLDOWN_MS
#define BATTLEBANG_HIT_COOLDOWN_MS 0
#endif

#ifndef BATTLEBANG_OFFLINE_HIT_QUEUE_CAPACITY
#define BATTLEBANG_OFFLINE_HIT_QUEUE_CAPACITY 32
#endif

#ifndef BATTLEBANG_OFFLINE_HIT_QUEUE_FLUSH_INTERVAL_MS
#define BATTLEBANG_OFFLINE_HIT_QUEUE_FLUSH_INTERVAL_MS 50
#endif

#ifndef BATTLEBANG_LED_PIN
#define BATTLEBANG_LED_PIN 18
#endif

#ifndef BATTLEBANG_NUM_LEDS
#define BATTLEBANG_NUM_LEDS 84
#endif

#ifndef BATTLEBANG_LED_BRIGHTNESS
#define BATTLEBANG_LED_BRIGHTNESS 120
#endif

#ifndef BATTLEBANG_HP_BAR_GROUP_COUNT
#define BATTLEBANG_HP_BAR_GROUP_COUNT 28
#endif

#ifndef BATTLEBANG_HP_BAR_LEDS_PER_GROUP
#define BATTLEBANG_HP_BAR_LEDS_PER_GROUP 3
#endif


#ifndef BATTLEBANG_T1_DO_PIN
#define BATTLEBANG_T1_DO_PIN 27
#endif

#ifndef BATTLEBANG_T2_DO_PIN
#define BATTLEBANG_T2_DO_PIN -1
#endif

#ifndef BATTLEBANG_PIEZO_AO_PIN
#define BATTLEBANG_PIEZO_AO_PIN 34
#endif

#ifndef BATTLEBANG_PIEZO_AO_THRESHOLD_RAW
// BTB-770 sensitivity trial: lower threshold so off-center harness hits still
// publish hit candidates. Tune per harness/target with robots.json or env.
#define BATTLEBANG_PIEZO_AO_THRESHOLD_RAW 200
#endif

#ifndef BATTLEBANG_PIEZO_AO_REARM_RAW
#define BATTLEBANG_PIEZO_AO_REARM_RAW 150
#endif

#ifndef BATTLEBANG_PIEZO_AO_CAPTURE_WINDOW_MS
#define BATTLEBANG_PIEZO_AO_CAPTURE_WINDOW_MS 30
#endif

#ifndef BATTLEBANG_PIEZO_AO_DEBUG_PERIOD_MS
#define BATTLEBANG_PIEZO_AO_DEBUG_PERIOD_MS 100
#endif

namespace go2 {

static constexpr const char *FIRMWARE_NAME = "go2";
static constexpr const char *FIRMWARE_ROLE = "hit_led";
static constexpr const char *BT_NAME = "ESP32_GO2_HIT";

static constexpr int UART_RX_PIN = 16;
static constexpr int UART_TX_PIN = 17;
static constexpr uint32_t UART_BAUD = 115200;

static constexpr char CMD_RESET_HIT_DISPLAY = '2';

static constexpr int HP_BAR_LED_PIN = BATTLEBANG_LED_PIN;
static constexpr int HP_BAR_NUM_LEDS = BATTLEBANG_NUM_LEDS;
static constexpr uint8_t HP_BAR_LED_BRIGHTNESS = BATTLEBANG_LED_BRIGHTNESS;
static constexpr int LED_PIN = HP_BAR_LED_PIN;
static constexpr int NUM_LEDS = HP_BAR_NUM_LEDS;
static constexpr uint8_t LED_BRIGHTNESS = HP_BAR_LED_BRIGHTNESS;
static constexpr int HP_BAR_GROUP_COUNT = BATTLEBANG_HP_BAR_GROUP_COUNT;
static constexpr int HP_BAR_LEDS_PER_GROUP = BATTLEBANG_HP_BAR_LEDS_PER_GROUP;
static constexpr int HP_BAR_EXPECTED_LED_COUNT =
    HP_BAR_GROUP_COUNT * HP_BAR_LEDS_PER_GROUP;
static constexpr uint8_t LED_MAX_VOLTS = 5;
static constexpr uint16_t LED_MAX_MA = 900;
static constexpr uint32_t LED_SHOW_PERIOD_MS = 16;
static constexpr uint32_t LED_BLINK_MS = 250;
static constexpr uint32_t LED_DEAD_BLINK_MS = 300;

static constexpr int PIEZO_DO_PIN = BATTLEBANG_T1_DO_PIN;
static constexpr int PIEZO_AO_PIN = BATTLEBANG_PIEZO_AO_PIN;
static constexpr int T1_DO = PIEZO_DO_PIN;
static constexpr int T2_DO = BATTLEBANG_T2_DO_PIN;
static constexpr int PIEZO_AO_THRESHOLD_RAW = BATTLEBANG_PIEZO_AO_THRESHOLD_RAW;
static constexpr int PIEZO_AO_REARM_RAW = BATTLEBANG_PIEZO_AO_REARM_RAW;
static constexpr uint32_t PIEZO_AO_CAPTURE_WINDOW_MS =
    BATTLEBANG_PIEZO_AO_CAPTURE_WINDOW_MS;
static constexpr uint32_t PIEZO_AO_DEBUG_PERIOD_MS =
    BATTLEBANG_PIEZO_AO_DEBUG_PERIOD_MS;
static constexpr uint32_t ISR_DEBOUNCE_US = 20000;
static constexpr uint32_t HIT_COOLDOWN_MS = BATTLEBANG_HIT_COOLDOWN_MS;
static constexpr uint32_t HIT_REARM_STABLE_MS = 300;
static constexpr uint32_t HIT_REARM_CHECK_MS = 50;
static constexpr int OFFLINE_HIT_QUEUE_CAPACITY =
    BATTLEBANG_OFFLINE_HIT_QUEUE_CAPACITY;
static constexpr uint32_t OFFLINE_HIT_QUEUE_FLUSH_INTERVAL_MS =
    BATTLEBANG_OFFLINE_HIT_QUEUE_FLUSH_INTERVAL_MS;

static_assert(OFFLINE_HIT_QUEUE_CAPACITY > 0,
              "offline hit queue capacity must be positive");
static_assert(OFFLINE_HIT_QUEUE_CAPACITY <= 255,
              "offline hit queue capacity must fit uint8_t counters");
static_assert(HP_BAR_GROUP_COUNT > 0, "HP bar group count must be positive");
static_assert(HP_BAR_LEDS_PER_GROUP == 3,
              "Go2 HP bar renderer expects 3 linked LEDs per group");
static_assert(NUM_LEDS == HP_BAR_EXPECTED_LED_COUNT,
              "HP bar LED count must match grouped bar layout");
static_assert(PIEZO_AO_PIN >= 0,
              "piezo AO pin must be configured for ADC threshold hit firmware");
static_assert(PIEZO_AO_THRESHOLD_RAW > 0,
              "piezo AO threshold must be positive");
static_assert(PIEZO_AO_THRESHOLD_RAW <= 4095,
              "piezo AO threshold must fit 12-bit ADC raw range");
static_assert(PIEZO_AO_REARM_RAW >= 0,
              "piezo AO rearm raw must be non-negative");
static_assert(PIEZO_AO_REARM_RAW < PIEZO_AO_THRESHOLD_RAW,
              "piezo AO rearm raw must be below threshold");
static_assert(PIEZO_AO_CAPTURE_WINDOW_MS > 0,
              "piezo AO capture window must be positive");

static constexpr const char *ROBOT_ID = BATTLEBANG_ROBOT_ID;
static constexpr const char *WIFI_SSID = BATTLEBANG_WIFI_SSID;
static constexpr const char *WIFI_PASSWORD = BATTLEBANG_WIFI_PASSWORD;
static constexpr const char *MQTT_HOST = BATTLEBANG_MQTT_HOST;
static constexpr uint16_t MQTT_PORT = BATTLEBANG_MQTT_PORT;
static constexpr const char *MQTT_TOPIC_PREFIX = BATTLEBANG_MQTT_TOPIC_PREFIX;
static constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 5000;
static constexpr uint32_t MQTT_RETRY_INTERVAL_MS = 2000;
static constexpr uint32_t HEARTBEAT_TX_PERIOD_MS = 1000;
static constexpr uint16_t MQTT_BUFFER_SIZE = 768;

inline const char *targetIdToSensorId(int targetId) {
  return (targetId == 1) ? "piezo_t1" : "piezo_t2";
}

} // namespace go2
