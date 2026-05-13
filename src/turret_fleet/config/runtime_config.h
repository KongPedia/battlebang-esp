#pragma once

#include <Arduino.h>

namespace battlebang {
namespace turret_fleet {

struct RuntimeConfig {
  uint16_t schema = 1;
  uint32_t configVersion = 0;
  bool configured = false;

  String deviceId;
  String turretId;

  float xCm = 0.0f;
  float yCm = 0.0f;
  float zCm = 134.5f;
  float defaultTargetZCm = 70.0f;
  float yawOffsetDeg = 0.0f;
  float pitchOffsetDeg = 0.0f;

  String wifiSsid;
  String wifiPassword;

  String mqttHost;
  uint16_t mqttPort = 1883;
  String mqttUsername;
  String mqttPassword;
  String mqttRoot = "battlebang";
};

RuntimeConfig makeDefaultRuntimeConfig(const String& deviceId);
bool applyRuntimeConfigJson(const char* json, RuntimeConfig& config, String& error);
String runtimeConfigToJson(const RuntimeConfig& config, bool includeSecrets = false);

class RuntimeConfigStore {
 public:
  bool load(RuntimeConfig& config);
  bool save(const RuntimeConfig& config);
  bool clear();
};

}  // namespace turret_fleet
}  // namespace battlebang
