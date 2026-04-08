#pragma once

#include <stdint.h>

#if __has_include("local_secrets.h")
#include "local_secrets.h"
#endif

#ifndef TURRET_ID
#define TURRET_ID "turret_1"
#endif

#ifndef TURRET_X_CM
#define TURRET_X_CM -300.0f
#endif

#ifndef TURRET_Y_CM
#define TURRET_Y_CM 470.0f
#endif

#ifndef TURRET_Z_CM
#define TURRET_Z_CM 134.5f
#endif

#ifndef TURRET_DEFAULT_TARGET_Z_CM
#define TURRET_DEFAULT_TARGET_Z_CM 70.0f
#endif

#ifndef TURRET_WIFI_SSID
#define TURRET_WIFI_SSID "YOUR_WIFI_SSID"
#endif

#ifndef TURRET_WIFI_PASSWORD
#define TURRET_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#endif

#ifndef TURRET_MQTT_HOST
#define TURRET_MQTT_HOST "192.168.0.10"
#endif

#ifndef TURRET_MQTT_PORT
#define TURRET_MQTT_PORT 1883
#endif

#ifndef TURRET_MQTT_USERNAME
#define TURRET_MQTT_USERNAME ""
#endif

#ifndef TURRET_MQTT_PASSWORD
#define TURRET_MQTT_PASSWORD ""
#endif

#ifndef TURRET_MQTT_TOPIC_PREFIX
#define TURRET_MQTT_TOPIC_PREFIX "battlebang/turrets"
#endif

#ifndef TURRET_MQTT_CLIENT_PREFIX
#define TURRET_MQTT_CLIENT_PREFIX "battlebang-esp"
#endif

#ifndef TURRET_MQTT_COORDS_IN_METERS
#define TURRET_MQTT_COORDS_IN_METERS 1
#endif

#ifndef TURRET_AUTO_FIRE_ON_TARGET
#define TURRET_AUTO_FIRE_ON_TARGET 1
#endif

#ifndef TURRET_MQTT_FIELD_COMMAND
#define TURRET_MQTT_FIELD_COMMAND "command"
#endif

#ifndef TURRET_MQTT_FIELD_TURRET_ID
#define TURRET_MQTT_FIELD_TURRET_ID "turret_id"
#endif

#ifndef TURRET_MQTT_FIELD_TARGET
#define TURRET_MQTT_FIELD_TARGET "target"
#endif

#ifndef TURRET_MQTT_FIELD_TARGET_X
#define TURRET_MQTT_FIELD_TARGET_X "x"
#endif

#ifndef TURRET_MQTT_FIELD_TARGET_Y
#define TURRET_MQTT_FIELD_TARGET_Y "y"
#endif

#ifndef TURRET_MQTT_FIELD_TARGET_Z
#define TURRET_MQTT_FIELD_TARGET_Z "z"
#endif

struct TurretBuildConfig {
  const char* turretId;
  float xTurretCm;
  float yTurretCm;
  float zTurretCm;
  float defaultTargetZCm;
  const char* wifiSsid;
  const char* wifiPassword;
  const char* mqttHost;
  uint16_t mqttPort;
  const char* mqttUsername;
  const char* mqttPassword;
  const char* mqttTopicPrefix;
  const char* mqttClientPrefix;
  bool mqttCoordinatesInMeters;
  bool autoFireOnTarget;
  const char* mqttCommandField;
  const char* mqttTurretIdField;
  const char* mqttTargetField;
  const char* mqttTargetXField;
  const char* mqttTargetYField;
  const char* mqttTargetZField;
};

static inline TurretBuildConfig makeTurretBuildConfig() {
  TurretBuildConfig config = {
    TURRET_ID,
    TURRET_X_CM,
    TURRET_Y_CM,
    TURRET_Z_CM,
    TURRET_DEFAULT_TARGET_Z_CM,
    TURRET_WIFI_SSID,
    TURRET_WIFI_PASSWORD,
    TURRET_MQTT_HOST,
    static_cast<uint16_t>(TURRET_MQTT_PORT),
    TURRET_MQTT_USERNAME,
    TURRET_MQTT_PASSWORD,
    TURRET_MQTT_TOPIC_PREFIX,
    TURRET_MQTT_CLIENT_PREFIX,
    TURRET_MQTT_COORDS_IN_METERS != 0,
    TURRET_AUTO_FIRE_ON_TARGET != 0,
    TURRET_MQTT_FIELD_COMMAND,
    TURRET_MQTT_FIELD_TURRET_ID,
    TURRET_MQTT_FIELD_TARGET,
    TURRET_MQTT_FIELD_TARGET_X,
    TURRET_MQTT_FIELD_TARGET_Y,
    TURRET_MQTT_FIELD_TARGET_Z,
  };

  return config;
}
