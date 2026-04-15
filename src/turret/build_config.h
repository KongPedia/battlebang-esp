#pragma once

#include <stdint.h>

// Optional local secrets. This file is gitignored and is the recommended local
// place for Wi-Fi/MQTT values when using PlatformIO on the operator laptop.
#if __has_include("local_secrets.h") && !defined(TURRET_SKIP_LOCAL_SECRETS)
#include "local_secrets.h"
#endif

// Overrides injected by scripts/turret_config.py from shell environment.
// These intentionally apply after local_secrets.h so command-line/env builds can
// override a local file without editing it.
#ifdef TURRET_BUILD_WIFI_SSID
#undef TURRET_WIFI_SSID
#define TURRET_WIFI_SSID TURRET_BUILD_WIFI_SSID
#endif

#ifdef TURRET_BUILD_WIFI_PASSWORD
#undef TURRET_WIFI_PASSWORD
#define TURRET_WIFI_PASSWORD TURRET_BUILD_WIFI_PASSWORD
#endif

#ifdef TURRET_BUILD_MQTT_HOST
#undef TURRET_MQTT_HOST
#define TURRET_MQTT_HOST TURRET_BUILD_MQTT_HOST
#endif

#ifdef TURRET_BUILD_MQTT_PORT
#undef TURRET_MQTT_PORT
#define TURRET_MQTT_PORT TURRET_BUILD_MQTT_PORT
#endif

#ifdef TURRET_BUILD_MQTT_USERNAME
#undef TURRET_MQTT_USERNAME
#define TURRET_MQTT_USERNAME TURRET_BUILD_MQTT_USERNAME
#endif

#ifdef TURRET_BUILD_MQTT_PASSWORD
#undef TURRET_MQTT_PASSWORD
#define TURRET_MQTT_PASSWORD TURRET_BUILD_MQTT_PASSWORD
#endif

#ifdef TURRET_BUILD_MQTT_COORDS_IN_METERS
#undef TURRET_MQTT_COORDS_IN_METERS
#define TURRET_MQTT_COORDS_IN_METERS TURRET_BUILD_MQTT_COORDS_IN_METERS
#endif

// PlatformIO/test build flag override. This wins even if local_secrets.h defines
// TURRET_AUTO_FIRE_ON_TARGET=1.
#ifdef TURRET_FORCE_AUTO_FIRE_ON_TARGET
#ifdef TURRET_AUTO_FIRE_ON_TARGET
#undef TURRET_AUTO_FIRE_ON_TARGET
#endif
#define TURRET_AUTO_FIRE_ON_TARGET TURRET_FORCE_AUTO_FIRE_ON_TARGET
#endif

#ifndef TURRET_ID
#define TURRET_ID "turret_5"
#endif

#ifndef TURRET_X_CM
#define TURRET_X_CM -170.0f
#endif

#ifndef TURRET_Y_CM
#define TURRET_Y_CM 190.0f
#endif

#ifndef TURRET_Z_CM
#define TURRET_Z_CM 134.5f
#endif

#ifndef TURRET_DEFAULT_TARGET_Z_CM
#define TURRET_DEFAULT_TARGET_Z_CM 70.0f
#endif

#ifndef TURRET_YAW_OFFSET_DEG
#define TURRET_YAW_OFFSET_DEG 0.0f
#endif

#ifndef TURRET_PITCH_OFFSET_DEG
#define TURRET_PITCH_OFFSET_DEG 0.0f
#endif

#ifndef TURRET_WIFI_SSID
#define TURRET_WIFI_SSID "YOUR_WIFI_SSID"
#endif

#ifndef TURRET_WIFI_PASSWORD
#define TURRET_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#endif

#ifndef TURRET_MQTT_HOST
#define TURRET_MQTT_HOST "YOUR_MQTT_HOST"
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
#define TURRET_AUTO_FIRE_ON_TARGET 0
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

#ifndef TURRET_WIFI_REDUCED_TX_POWER
#define TURRET_WIFI_REDUCED_TX_POWER 0
#endif

#ifndef TURRET_LAZY_ATTACH_SERVOS
#define TURRET_LAZY_ATTACH_SERVOS 1
#endif

#ifndef TURRET_LAZY_ARM_ESC
#define TURRET_LAZY_ARM_ESC 1
#endif

#ifndef TURRET_ESC_STOP_SIGNAL_AT_BOOT
#define TURRET_ESC_STOP_SIGNAL_AT_BOOT 1
#endif

#ifndef TURRET_LAZY_RELAY_OUTPUTS
#define TURRET_LAZY_RELAY_OUTPUTS 1
#endif

#ifndef TURRET_WIFI_CONNECT_IN_LOOP
#define TURRET_WIFI_CONNECT_IN_LOOP 1
#endif

#ifndef TURRET_WIFI_BOOT_DELAY_MS
#define TURRET_WIFI_BOOT_DELAY_MS 0
#endif

#ifndef TURRET_WIFI_START_ON_SERIAL
#define TURRET_WIFI_START_ON_SERIAL 0
#endif

#ifndef TURRET_WIFI_PRE_MODE_DELAY_MS
#define TURRET_WIFI_PRE_MODE_DELAY_MS 0
#endif

#ifndef TURRET_WIFI_STAGE_DELAY_MS
#define TURRET_WIFI_STAGE_DELAY_MS 0
#endif

#ifndef TURRET_DISABLE_BT_AT_BOOT
#define TURRET_DISABLE_BT_AT_BOOT 1
#endif

#ifndef TURRET_WIFI_MODEM_SLEEP
#define TURRET_WIFI_MODEM_SLEEP 0
#endif

#ifndef TURRET_ESC_ARM_DELAY_MS
#define TURRET_ESC_ARM_DELAY_MS 3000
#endif

#ifndef TURRET_CPU_FREQ_MHZ
#define TURRET_CPU_FREQ_MHZ 80
#endif

#ifndef TURRET_DISABLE_BROWNOUT_DETECTOR
#define TURRET_DISABLE_BROWNOUT_DETECTOR 0
#endif

#ifndef TURRET_SERIAL_PRINT_INTERVAL_MS
#define TURRET_SERIAL_PRINT_INTERVAL_MS 1000
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
