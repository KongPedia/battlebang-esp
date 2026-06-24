#pragma once

#include <Arduino.h>

#include <bb_esp_core/config/common_runtime_config.h>
#include <bb_esp_core/mqtt/topic_utils.h>

namespace battlebang::esp::config {

struct BuildTimeCommonRuntimeConfigDefaults {
  const char* deviceId = "";
  const char* group = "";
  const char* stageId = "";
  const char* location = "";

  const char* wifiSsid = "";
  const char* wifiPassword = "";
  bool networkAutoStart = true;
  uint32_t networkStartDelayMs = 0;

  const char* mqttHost = "";
  uint16_t mqttPort = 1883;
  const char* mqttUsername = "";
  const char* mqttPassword = "";
  const char* mqttRoot = "battlebang";

  bool otaCommandCenterControlled = true;
  bool otaAutoCheckEnabled = false;
  const char* otaChannel = "stable";
  uint32_t otaDesiredBuild = 0;
  const char* otaPublicManifestUrl = "";
  const char* otaLocalMirrorUrl = "";
  uint32_t otaCheckIntervalS = 3600;
  bool otaApplyOnlyInSafeState = true;
};

inline bool hasValue(const char* value) {
  return value != nullptr && value[0] != '\0';
}

inline String safeString(const char* value) {
  return value == nullptr ? String() : String(value);
}

inline CommonRuntimeConfig makeBuildTimeCommonRuntimeConfig(
    const BuildTimeCommonRuntimeConfigDefaults& defaults) {
  CommonRuntimeConfig config;
  config.configured = hasValue(defaults.wifiSsid) && hasValue(defaults.mqttHost);
  config.deviceId = safeString(defaults.deviceId);
  config.group = safeString(defaults.group);
  config.stageId = safeString(defaults.stageId);
  config.location = safeString(defaults.location);

  config.wifiSsid = safeString(defaults.wifiSsid);
  config.wifiPassword = safeString(defaults.wifiPassword);
  config.networkAutoStart = defaults.networkAutoStart;
  config.networkStartDelayMs = defaults.networkStartDelayMs;

  config.mqttHost = safeString(defaults.mqttHost);
  config.mqttPort = defaults.mqttPort;
  config.mqttUsername = safeString(defaults.mqttUsername);
  config.mqttPassword = safeString(defaults.mqttPassword);
  config.mqttRoot = battlebang::esp::mqtt::normalizeRootOrDefault(safeString(defaults.mqttRoot));

  config.otaCommandCenterControlled = defaults.otaCommandCenterControlled;
  config.otaAutoCheckEnabled = defaults.otaAutoCheckEnabled;
  config.otaChannel = safeString(defaults.otaChannel);
  config.otaDesiredBuild = defaults.otaDesiredBuild;
  config.otaPublicManifestUrl = safeString(defaults.otaPublicManifestUrl);
  config.otaLocalMirrorUrl = safeString(defaults.otaLocalMirrorUrl);
  config.otaCheckIntervalS = defaults.otaCheckIntervalS;
  config.otaApplyOnlyInSafeState = defaults.otaApplyOnlyInSafeState;
  return config;
}

}  // namespace battlebang::esp::config
