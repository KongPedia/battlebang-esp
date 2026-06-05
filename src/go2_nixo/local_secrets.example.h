#pragma once

// Copy to src/go2_nixo/local_secrets.h and fill local lab values before flashing
// the Go2 hit ESP firmware. Do not commit src/go2_nixo/local_secrets.h.
//
// Robot identity is normally selected by PlatformIO env/custom_robot_id
// (for example: esp32dev_go2_05) or GO2_ID. Keep it out of this file unless
// you intentionally need a manual robot id override for generic esp32dev builds.

// #define ESP_ROBOT_ID "go2_05"
#define ESP_WIFI_SSID "YOUR_WIFI_SSID"
#define ESP_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define ESP_MQTT_HOST "COMMAND_CENTER_OR_BROKER_HOST"
#define ESP_MQTT_PORT 1883
#define ESP_MQTT_TOPIC_PREFIX "battlebang/hit"

// Optional integrated Nixo/game-blaster fire overrides. By default, the
// combined Go2 firmware derives NIXO_ID as "nixo_" + ESP_ROBOT_ID and listens
// on battlebang/nixo/<NIXO_ID>/command using the same broker as ESP_MQTT_HOST.
//
// Live go2_03 mapping:
//   NIXO_ID = "nixo_go2_03"
//   topic   = "battlebang/nixo/nixo_go2_03/command"
//
// #define NIXO_ID "nixo_go2_03"
// #define NIXO_MQTT_TOPIC_PREFIX "battlebang/nixo"
// #define NIXO_RELAY1_PIN 23
// #define NIXO_RELAY2_PIN -1
// #define NIXO_RELAY_ON_LEVEL HIGH
// #define NIXO_RELAY_OFF_LEVEL LOW
