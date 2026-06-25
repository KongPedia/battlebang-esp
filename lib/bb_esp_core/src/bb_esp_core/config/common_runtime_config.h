#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace battlebang::esp::config {

struct CommonRuntimeConfig {
  uint16_t schema = 1;
  uint32_t configVersion = 0;
  bool configured = false;

  String deviceId;
  String group;
  String stageId;
  String location;

  String wifiSsid;
  String wifiPassword;
  bool networkAutoStart = true;
  uint32_t networkStartDelayMs = 0;

  String mqttHost;
  uint16_t mqttPort = 1883;
  String mqttUsername;
  String mqttPassword;
  String mqttRoot = "battlebang";

  bool otaCommandCenterControlled = true;
  bool otaAutoCheckEnabled = false;
  String otaChannel = "stable";
  uint32_t otaDesiredBuild = 0;
  String otaPublicManifestUrl;
  String otaLocalMirrorUrl;
  uint32_t otaCheckIntervalS = 3600;
  bool otaApplyOnlyInSafeState = true;
};

}  // namespace battlebang::esp::config
