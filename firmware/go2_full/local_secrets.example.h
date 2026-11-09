#pragma once

// Optional bench fallback only. Prefer firmware/go2_full/.env.go2_full +
// scripts/go2_full/provision.py so identity, Blaster ID, Wi-Fi, MQTT, OTA, and
// hit/display tuning and Blaster fire timing are written to ESP32 NVS and can
// change without rebuilding.
//
// Do not commit firmware/go2_full/local_secrets.h.

#define ESP_WIFI_SSID "YOUR_WIFI_SSID"
#define ESP_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define ESP_MQTT_HOST "COMMAND_CENTER_OR_BROKER_HOST"
#define ESP_MQTT_PORT 1883
#define ESP_MQTT_TOPIC_PREFIX "battlebang/hit"

// Optional fallback only; NVS config normally owns these.
// #define BLASTER_ID "blaster_go2_03"
// #define BLASTER_MQTT_TOPIC_PREFIX "battlebang/blaster"
