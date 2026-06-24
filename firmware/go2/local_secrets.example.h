#pragma once

// Optional bench fallback only. Prefer firmware/go2/.env.go2 +
// scripts/go2/provision.py so identity, Wi-Fi, MQTT, OTA, and hit tuning are
// written to ESP32 NVS and can change without rebuilding.
//
// Do not commit firmware/go2/local_secrets.h.

#define ESP_WIFI_SSID "YOUR_WIFI_SSID"
#define ESP_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define ESP_MQTT_HOST "COMMAND_CENTER_OR_BROKER_HOST"
#define ESP_MQTT_PORT 1883
#define ESP_MQTT_TOPIC_PREFIX "battlebang/hit"
