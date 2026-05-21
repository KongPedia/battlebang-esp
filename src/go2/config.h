#pragma once

#include <Arduino.h>

#if __has_include("local_secrets.h") && !defined(BATTLEBANG_SKIP_LOCAL_SECRETS)
#include "local_secrets.h"
#elif __has_include("../local_secrets.h") && !defined(BATTLEBANG_SKIP_LOCAL_SECRETS)
// Legacy path used before Go2 adopted the turret-style per-module config.
// Keep this fallback so existing operator laptops do not break immediately.
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

// Overrides injected by scripts/go2_config.py from PlatformIO profile/shell env.
// These intentionally apply after local_secrets.h so explicit env/profile builds
// win without editing the gitignored local file.
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

#ifdef BATTLEBANG_BUILD_HP_MAX
#undef BATTLEBANG_HP_MAX
#define BATTLEBANG_HP_MAX BATTLEBANG_BUILD_HP_MAX
#endif

#ifdef BATTLEBANG_BUILD_PIEZO_DAMAGE_DIVISOR
#undef BATTLEBANG_PIEZO_DAMAGE_DIVISOR
#define BATTLEBANG_PIEZO_DAMAGE_DIVISOR BATTLEBANG_BUILD_PIEZO_DAMAGE_DIVISOR
#endif

#ifdef BATTLEBANG_BUILD_HIT_THRESHOLD
#undef BATTLEBANG_HIT_THRESHOLD
#define BATTLEBANG_HIT_THRESHOLD BATTLEBANG_BUILD_HIT_THRESHOLD
#endif

#ifdef BATTLEBANG_BUILD_HIT_COOLDOWN_MS
#undef BATTLEBANG_HIT_COOLDOWN_MS
#define BATTLEBANG_HIT_COOLDOWN_MS BATTLEBANG_BUILD_HIT_COOLDOWN_MS
#endif

#ifdef BATTLEBANG_BUILD_HIT_REARM_THRESHOLD
#undef BATTLEBANG_HIT_REARM_THRESHOLD
#define BATTLEBANG_HIT_REARM_THRESHOLD BATTLEBANG_BUILD_HIT_REARM_THRESHOLD
#endif

#ifdef BATTLEBANG_BUILD_AUTHORITY_FALLBACK_TIMEOUT_MS
#undef BATTLEBANG_AUTHORITY_FALLBACK_TIMEOUT_MS
#define BATTLEBANG_AUTHORITY_FALLBACK_TIMEOUT_MS BATTLEBANG_BUILD_AUTHORITY_FALLBACK_TIMEOUT_MS
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

#ifdef BATTLEBANG_BUILD_T1_AO_PIN
#undef BATTLEBANG_T1_AO_PIN
#define BATTLEBANG_T1_AO_PIN BATTLEBANG_BUILD_T1_AO_PIN
#endif

#ifdef BATTLEBANG_BUILD_T2_DO_PIN
#undef BATTLEBANG_T2_DO_PIN
#define BATTLEBANG_T2_DO_PIN BATTLEBANG_BUILD_T2_DO_PIN
#endif

#ifdef BATTLEBANG_BUILD_T2_AO_PIN
#undef BATTLEBANG_T2_AO_PIN
#define BATTLEBANG_T2_AO_PIN BATTLEBANG_BUILD_T2_AO_PIN
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
#define BATTLEBANG_MQTT_TOPIC_PREFIX "battlebang/esp"
#endif

#ifndef BATTLEBANG_HP_MAX
#define BATTLEBANG_HP_MAX 100
#endif

#ifndef BATTLEBANG_PIEZO_DAMAGE_DIVISOR
// Temporary game-rule mapping for local fallback damage.
// The piezo ADC peak is not a calibrated force value yet. Until the final
// game rule is decided, use the first two digits of the peak as damage:
//   peak=4000 -> damage=40, peak=3500 -> damage=35.
#define BATTLEBANG_PIEZO_DAMAGE_DIVISOR 100
#endif

#ifndef BATTLEBANG_HIT_THRESHOLD
#define BATTLEBANG_HIT_THRESHOLD 3000
#endif

#ifndef BATTLEBANG_HIT_COOLDOWN_MS
#define BATTLEBANG_HIT_COOLDOWN_MS 1500
#endif

#ifndef BATTLEBANG_HIT_REARM_THRESHOLD
#define BATTLEBANG_HIT_REARM_THRESHOLD 1500
#endif

#ifndef BATTLEBANG_AUTHORITY_FALLBACK_TIMEOUT_MS
#define BATTLEBANG_AUTHORITY_FALLBACK_TIMEOUT_MS 600
#endif

#ifndef BATTLEBANG_LED_PIN
#define BATTLEBANG_LED_PIN 4
#endif

#ifndef BATTLEBANG_NUM_LEDS
#define BATTLEBANG_NUM_LEDS 40
#endif

#ifndef BATTLEBANG_LED_BRIGHTNESS
#define BATTLEBANG_LED_BRIGHTNESS 60
#endif

#ifndef BATTLEBANG_T1_DO_PIN
#define BATTLEBANG_T1_DO_PIN 25
#endif

#ifndef BATTLEBANG_T1_AO_PIN
#define BATTLEBANG_T1_AO_PIN 34
#endif

#ifndef BATTLEBANG_T2_DO_PIN
#define BATTLEBANG_T2_DO_PIN 26
#endif

#ifndef BATTLEBANG_T2_AO_PIN
#define BATTLEBANG_T2_AO_PIN 35
#endif

namespace go2 {

static constexpr const char* BT_NAME = "ESP32_HP_SERVO";

static constexpr int UART_RX_PIN = 16;
static constexpr int UART_TX_PIN = 17;
static constexpr uint32_t UART_BAUD = 115200;

static constexpr char CMD_JETSON_FIRE = '1';
static constexpr char CMD_JETSON_RESET_HP = '2';
static constexpr char CMD_LOCAL_FIRE = 'f';

static constexpr int HP_MAX = BATTLEBANG_HP_MAX;
static constexpr int HP_PER_LAP = HP_MAX;
static constexpr int PIEZO_DAMAGE_DIVISOR = BATTLEBANG_PIEZO_DAMAGE_DIVISOR;

static constexpr int LED_PIN = BATTLEBANG_LED_PIN;
static constexpr int NUM_LEDS = BATTLEBANG_NUM_LEDS;
static constexpr uint8_t LED_BRIGHTNESS = BATTLEBANG_LED_BRIGHTNESS;
static constexpr uint8_t LED_MAX_VOLTS = 5;
static constexpr uint16_t LED_MAX_MA = 900;
static constexpr uint32_t LED_SHOW_PERIOD_MS = 16;
static constexpr uint32_t LED_BLINK_MS = 250;
static constexpr uint32_t LED_DEAD_BLINK_MS = 300;

static constexpr int T1_DO = BATTLEBANG_T1_DO_PIN;
static constexpr int T1_AO = BATTLEBANG_T1_AO_PIN;
static constexpr int T2_DO = BATTLEBANG_T2_DO_PIN;
static constexpr int T2_AO = BATTLEBANG_T2_AO_PIN;
static constexpr uint32_t ISR_DEBOUNCE_US = 20000;
static constexpr uint32_t HIT_COOLDOWN_MS = BATTLEBANG_HIT_COOLDOWN_MS;
static constexpr uint32_t HIT_REARM_STABLE_MS = 300;
static constexpr uint32_t HIT_REARM_CHECK_MS = 50;
static constexpr uint16_t HIT_REARM_THRESHOLD = BATTLEBANG_HIT_REARM_THRESHOLD;
static constexpr uint32_t POST_DELAY_MS = 0;
static constexpr uint32_t SAMPLE_INTERVAL_US = 1000;
static constexpr int CAPTURE_SAMPLES = 200;
static constexpr uint16_t HIT_THRESHOLD = BATTLEBANG_HIT_THRESHOLD;

static constexpr int RELAY1_PIN = 22;
static constexpr int RELAY2_PIN = 21;
static constexpr bool RELAY_ON = LOW;
static constexpr bool RELAY_OFF = HIGH;

static constexpr int SERVO_PIN = 18;
static constexpr int SERVO_PWM_FREQ = 50;
static constexpr int SERVO_PWM_RES = 16;
static constexpr int SERVO_PWM_CHANNEL = 0;
static constexpr int FIRE_POS_A = 55;
static constexpr int FIRE_POS_B = 145;
static constexpr int SERVO_HOME_ANGLE = 125;
static constexpr int SERVO_STEP = 2;
static constexpr uint32_t SERVO_STEP_DT_MS = 15;
static constexpr uint32_t RELAY_DELAY1_MS = 800;
static constexpr uint32_t RELAY_DELAY2_MS = 1500;
static constexpr uint32_t FIRE_COOLDOWN_MS = 2500;

static constexpr const char* ROBOT_ID = BATTLEBANG_ROBOT_ID;
static constexpr const char* WIFI_SSID = BATTLEBANG_WIFI_SSID;
static constexpr const char* WIFI_PASSWORD = BATTLEBANG_WIFI_PASSWORD;
static constexpr const char* MQTT_HOST = BATTLEBANG_MQTT_HOST;
static constexpr uint16_t MQTT_PORT = BATTLEBANG_MQTT_PORT;
static constexpr const char* MQTT_TOPIC_PREFIX = BATTLEBANG_MQTT_TOPIC_PREFIX;
static constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 5000;
static constexpr uint32_t MQTT_RETRY_INTERVAL_MS = 2000;
static constexpr uint32_t HEARTBEAT_TX_PERIOD_MS = 1000;
static constexpr uint32_t AUTHORITY_FALLBACK_TIMEOUT_MS = BATTLEBANG_AUTHORITY_FALLBACK_TIMEOUT_MS;
static constexpr uint16_t MQTT_BUFFER_SIZE = 768;

inline int peakToDamage(uint16_t peak) {
  // TODO(game-rule): replace this temporary peak/100 rule once damage
  // balancing is finalized. For now 4000 is treated as 40 damage.
  int damage = peak / PIEZO_DAMAGE_DIVISOR;
  if (damage < 1) return 1;
  if (damage > HP_MAX) return HP_MAX;
  return damage;
}

inline const char* targetIdToSensorId(int targetId) {
  return (targetId == 1) ? "piezo_t1" : "piezo_t2";
}

}  // namespace go2
