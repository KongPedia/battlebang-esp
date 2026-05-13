#pragma once

// Optional local secrets. This file is gitignored and is the recommended local
// place for Wi-Fi/MQTT values when using PlatformIO on the operator laptop.
#if __has_include("local_secrets.h") && !defined(NIXO_SKIP_LOCAL_SECRETS)
#include "local_secrets.h"
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
#define NIXO_FIRE_DEFAULT_DURATION_MS 1500
#endif

#ifndef NIXO_FIRE_MIN_DURATION_MS
#define NIXO_FIRE_MIN_DURATION_MS 100
#endif

#ifndef NIXO_FIRE_MAX_DURATION_MS
#define NIXO_FIRE_MAX_DURATION_MS 10000
#endif

#ifndef NIXO_RELAY1_PIN
#define NIXO_RELAY1_PIN 23
#endif

#ifndef NIXO_RELAY2_PIN
#define NIXO_RELAY2_PIN -1
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
