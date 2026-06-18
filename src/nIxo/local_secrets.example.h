#pragma once

// Copy this file to:
//   src/nIxo/local_secrets.h
//
// That file is gitignored and can safely hold local Wi-Fi/MQTT secrets.
// Values here override the defaults in src/nIxo/build_config.h at compile time.

#define NIXO_ID "nixo_go2_03"

#define NIXO_WIFI_SSID "YOUR_WIFI_SSID"
#define NIXO_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

#define NIXO_MQTT_HOST "COMMAND_CENTER_IP_OR_DNS"
#define NIXO_MQTT_PORT 1883
#define NIXO_MQTT_USERNAME ""
#define NIXO_MQTT_PASSWORD ""

// Command topic is:
//   <NIXO_MQTT_TOPIC_PREFIX>/<NIXO_ID>/command
#define NIXO_MQTT_TOPIC_PREFIX "battlebang/nixo"

// 1: clear any stale retained command on connect before subscribing.
#define NIXO_CLEAR_RETAINED_COMMAND_ON_CONNECT 1

// Duration bounds for Command Center/Nexus fire requests.
#define NIXO_FIRE_DEFAULT_DURATION_MS 1500
#define NIXO_FIRE_MIN_DURATION_MS 100
#define NIXO_FIRE_MAX_DURATION_MS 10000

// Default/legacy 1-channel Go2-mounted Nixo wiring matches the pre-MQTT Bluetooth smoke sketch:
// GPIO23, active-HIGH, single relay channel. PlatformIO variant profiles can override these
// at build time; use esp32dev_nixo_2ch_* for the BTB-766 GPIO22 flywheel + GPIO23 chain build.
#define NIXO_RELAY1_PIN 23
#define NIXO_RELAY2_PIN -1
#define NIXO_RELAY_ON_LEVEL HIGH
#define NIXO_RELAY_OFF_LEVEL LOW
