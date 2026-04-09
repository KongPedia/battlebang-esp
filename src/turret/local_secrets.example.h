#pragma once

// Copy this file to:
//   src/turret/local_secrets.h
//
// That file is gitignored and can safely hold local Wi-Fi/MQTT secrets.
// Values here override the defaults in src/turret/build_config.h at compile time.

#define TURRET_WIFI_SSID "YOUR_WIFI_SSID"
#define TURRET_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

#define TURRET_MQTT_HOST "192.168.0.10"
#define TURRET_MQTT_PORT 1883
#define TURRET_MQTT_USERNAME ""
#define TURRET_MQTT_PASSWORD ""

// 1: incoming MQTT target coords are meters and firmware converts to cm
// 0: incoming MQTT target coords are already cm
#define TURRET_MQTT_COORDS_IN_METERS 1

// 1: fire automatically after aim reached on target command
// 0: only aim on target command, require fire command separately
#define TURRET_AUTO_FIRE_ON_TARGET 0
