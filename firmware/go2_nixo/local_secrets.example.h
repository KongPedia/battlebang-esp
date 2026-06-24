#pragma once

// Optional bench fallback only. Prefer firmware/go2_nixo/.env.go2_nixo +
// scripts/go2_nixo/provision.py so identity, Nixo ID, Wi-Fi, MQTT, OTA, and
// hit/display tuning and Nixo fire timing are written to ESP32 NVS and can
// change without rebuilding.
//
// Do not commit firmware/go2_nixo/local_secrets.h.

#define ESP_WIFI_SSID "YOUR_WIFI_SSID"
#define ESP_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define ESP_MQTT_HOST "COMMAND_CENTER_OR_BROKER_HOST"
#define ESP_MQTT_PORT 1883
#define ESP_MQTT_TOPIC_PREFIX "battlebang/hit"

// Optional fallback only; NVS config normally owns these.
// #define NIXO_ID "nixo_go2_03"
// #define NIXO_MQTT_TOPIC_PREFIX "battlebang/nixo"
