#pragma once

// Copy to src/go2/local_secrets.h and fill local lab values before flashing
// the Go2 hit ESP firmware. Do not commit src/go2/local_secrets.h.
//
// Robot identity is normally selected by PlatformIO env/custom_robot_id
// (for example: esp32dev_go2_05) or GO2_ID. Keep it out of this file unless
// you intentionally want a local fallback for generic esp32dev builds.

// #define ESP_ROBOT_ID "go2_05"
#define ESP_WIFI_SSID "YOUR_WIFI_SSID"
#define ESP_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define ESP_MQTT_HOST "COMMAND_CENTER_OR_BROKER_HOST"
#define ESP_MQTT_PORT 1883
#define ESP_MQTT_TOPIC_PREFIX "battlebang/esp"
