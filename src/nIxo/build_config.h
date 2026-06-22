#pragma once

// Optional local secrets. This file is gitignored and is the recommended local
// place for Wi-Fi/MQTT values when using PlatformIO on the operator laptop.
#if __has_include("local_secrets.h") && !defined(NIXO_SKIP_LOCAL_SECRETS)
#include "local_secrets.h"
#elif __has_include("../local_secrets.h") && !defined(NIXO_SKIP_LOCAL_SECRETS)
#include "../local_secrets.h"
#endif


// Compatibility with the shared repo-local src/local_secrets.h used by Go2.
// Only network fields are aliased; the Nixo command topic remains battlebang/nixo.
#if defined(ESP_WIFI_SSID) && !defined(NIXO_WIFI_SSID)
#define NIXO_WIFI_SSID ESP_WIFI_SSID
#endif

#if defined(ESP_WIFI_PASSWORD) && !defined(NIXO_WIFI_PASSWORD)
#define NIXO_WIFI_PASSWORD ESP_WIFI_PASSWORD
#endif

#if defined(ESP_MQTT_HOST) && !defined(NIXO_MQTT_HOST)
#define NIXO_MQTT_HOST ESP_MQTT_HOST
#endif

#if defined(ESP_MQTT_PORT) && !defined(NIXO_MQTT_PORT)
#define NIXO_MQTT_PORT ESP_MQTT_PORT
#endif

// Overrides injected by scripts/nixo_config.py from shell environment. These
// intentionally apply after local_secrets.h so command-line/env builds can
// override a local file without editing it.
#ifdef NIXO_BUILD_ID
#undef NIXO_ID
#define NIXO_ID NIXO_BUILD_ID
#endif

#ifdef NIXO_BUILD_WIFI_SSID
#undef NIXO_WIFI_SSID
#define NIXO_WIFI_SSID NIXO_BUILD_WIFI_SSID
#endif

#ifdef NIXO_BUILD_WIFI_PASSWORD
#undef NIXO_WIFI_PASSWORD
#define NIXO_WIFI_PASSWORD NIXO_BUILD_WIFI_PASSWORD
#endif

#ifdef NIXO_BUILD_MQTT_HOST
#undef NIXO_MQTT_HOST
#define NIXO_MQTT_HOST NIXO_BUILD_MQTT_HOST
#endif

#ifdef NIXO_BUILD_MQTT_PORT
#undef NIXO_MQTT_PORT
#define NIXO_MQTT_PORT NIXO_BUILD_MQTT_PORT
#endif

#ifdef NIXO_BUILD_MQTT_USERNAME
#undef NIXO_MQTT_USERNAME
#define NIXO_MQTT_USERNAME NIXO_BUILD_MQTT_USERNAME
#endif

#ifdef NIXO_BUILD_MQTT_PASSWORD
#undef NIXO_MQTT_PASSWORD
#define NIXO_MQTT_PASSWORD NIXO_BUILD_MQTT_PASSWORD
#endif

#ifdef NIXO_BUILD_MQTT_TOPIC_PREFIX
#undef NIXO_MQTT_TOPIC_PREFIX
#define NIXO_MQTT_TOPIC_PREFIX NIXO_BUILD_MQTT_TOPIC_PREFIX
#endif

#ifdef NIXO_BUILD_FIRE_DEFAULT_DURATION_MS
#undef NIXO_FIRE_DEFAULT_DURATION_MS
#define NIXO_FIRE_DEFAULT_DURATION_MS NIXO_BUILD_FIRE_DEFAULT_DURATION_MS
#endif

#ifdef NIXO_BUILD_FIRE_MIN_DURATION_MS
#undef NIXO_FIRE_MIN_DURATION_MS
#define NIXO_FIRE_MIN_DURATION_MS NIXO_BUILD_FIRE_MIN_DURATION_MS
#endif

#ifdef NIXO_BUILD_FIRE_MAX_DURATION_MS
#undef NIXO_FIRE_MAX_DURATION_MS
#define NIXO_FIRE_MAX_DURATION_MS NIXO_BUILD_FIRE_MAX_DURATION_MS
#endif

#ifdef NIXO_BUILD_FIRE_COOLDOWN_MS
#undef NIXO_FIRE_COOLDOWN_MS
#define NIXO_FIRE_COOLDOWN_MS NIXO_BUILD_FIRE_COOLDOWN_MS
#endif

#ifdef NIXO_BUILD_PREFIRE_DELAY_MS
#undef NIXO_PREFIRE_DELAY_MS
#define NIXO_PREFIRE_DELAY_MS NIXO_BUILD_PREFIRE_DELAY_MS
#endif

#ifdef NIXO_BUILD_RELAY_VARIANT_NAME
#undef NIXO_RELAY_VARIANT_NAME
#define NIXO_RELAY_VARIANT_NAME NIXO_BUILD_RELAY_VARIANT_NAME
#endif

#ifdef NIXO_BUILD_RELAY_CHANNELS
#undef NIXO_RELAY_CHANNELS
#define NIXO_RELAY_CHANNELS NIXO_BUILD_RELAY_CHANNELS
#endif

#ifdef NIXO_BUILD_RELAY1_PIN
#undef NIXO_RELAY1_PIN
#define NIXO_RELAY1_PIN NIXO_BUILD_RELAY1_PIN
#endif

#ifdef NIXO_BUILD_RELAY2_PIN
#undef NIXO_RELAY2_PIN
#define NIXO_RELAY2_PIN NIXO_BUILD_RELAY2_PIN
#endif

#ifdef NIXO_BUILD_RELAY1_ROLE
#undef NIXO_RELAY1_ROLE
#define NIXO_RELAY1_ROLE NIXO_BUILD_RELAY1_ROLE
#endif

#ifdef NIXO_BUILD_RELAY2_ROLE
#undef NIXO_RELAY2_ROLE
#define NIXO_RELAY2_ROLE NIXO_BUILD_RELAY2_ROLE
#endif

#ifdef NIXO_BUILD_RELAY_DELAY1_MS
#undef NIXO_RELAY_DELAY1_MS
#define NIXO_RELAY_DELAY1_MS NIXO_BUILD_RELAY_DELAY1_MS
#endif

#ifdef NIXO_BUILD_RELAY_ON_LEVEL
#undef NIXO_RELAY_ON_LEVEL
#define NIXO_RELAY_ON_LEVEL NIXO_BUILD_RELAY_ON_LEVEL
#endif

#ifdef NIXO_BUILD_RELAY_OFF_LEVEL
#undef NIXO_RELAY_OFF_LEVEL
#define NIXO_RELAY_OFF_LEVEL NIXO_BUILD_RELAY_OFF_LEVEL
#endif

#ifndef NIXO_ID
#define NIXO_ID "nixo_go2_03"
#endif

#ifndef NIXO_WIFI_SSID
#define NIXO_WIFI_SSID "YOUR_WIFI_SSID"
#endif

#ifndef NIXO_WIFI_PASSWORD
#define NIXO_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#endif

#ifndef NIXO_MQTT_HOST
#define NIXO_MQTT_HOST "YOUR_MQTT_HOST"
#endif

#ifndef NIXO_MQTT_PORT
#define NIXO_MQTT_PORT 1883
#endif

#ifndef NIXO_MQTT_USERNAME
#define NIXO_MQTT_USERNAME ""
#endif

#ifndef NIXO_MQTT_PASSWORD
#define NIXO_MQTT_PASSWORD ""
#endif

#ifndef NIXO_MQTT_TOPIC_PREFIX
#define NIXO_MQTT_TOPIC_PREFIX "battlebang/nixo"
#endif

#ifndef NIXO_MQTT_QOS
#define NIXO_MQTT_QOS 1
#endif

#ifndef NIXO_MQTT_BUFFER_BYTES
#define NIXO_MQTT_BUFFER_BYTES 512
#endif

#ifndef NIXO_CLEAR_RETAINED_COMMAND_ON_CONNECT
#define NIXO_CLEAR_RETAINED_COMMAND_ON_CONNECT 1
#endif

#ifndef NIXO_FIRE_DEFAULT_DURATION_MS
#define NIXO_FIRE_DEFAULT_DURATION_MS 3000
#endif

#ifndef NIXO_FIRE_MIN_DURATION_MS
#define NIXO_FIRE_MIN_DURATION_MS 100
#endif

#ifndef NIXO_FIRE_MAX_DURATION_MS
#define NIXO_FIRE_MAX_DURATION_MS 10000
#endif

#ifndef NIXO_FIRE_COOLDOWN_MS
#define NIXO_FIRE_COOLDOWN_MS 1500
#endif

#ifndef NIXO_PREFIRE_DELAY_MS
#define NIXO_PREFIRE_DELAY_MS 600
#endif

#ifndef NIXO_RELAY_DELAY1_MS
#define NIXO_RELAY_DELAY1_MS 800
#endif

#ifndef NIXO_RELAY_VARIANT_NAME
#define NIXO_RELAY_VARIANT_NAME "relay_1ch"
#endif

#ifndef NIXO_RELAY_CHANNELS
#define NIXO_RELAY_CHANNELS 1
#endif

#ifndef NIXO_RELAY1_PIN
#define NIXO_RELAY1_PIN 23
#endif

#ifndef NIXO_RELAY2_PIN
#define NIXO_RELAY2_PIN -1
#endif

#ifndef NIXO_RELAY1_ROLE
#define NIXO_RELAY1_ROLE "single_fire_gpio23"
#endif

#ifndef NIXO_RELAY2_ROLE
#define NIXO_RELAY2_ROLE "disabled"
#endif

#ifndef NIXO_RELAY_ON_LEVEL
#define NIXO_RELAY_ON_LEVEL HIGH
#endif

#ifndef NIXO_RELAY_OFF_LEVEL
#define NIXO_RELAY_OFF_LEVEL LOW
#endif

#ifndef NIXO_WIFI_RETRY_MS
#define NIXO_WIFI_RETRY_MS 5000
#endif

#ifndef NIXO_MQTT_RETRY_MS
#define NIXO_MQTT_RETRY_MS 3000
#endif
