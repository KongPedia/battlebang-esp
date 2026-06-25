#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include <bb_esp_core/config/common_runtime_config.h>

namespace battlebang::esp::nvs {

class ScopedPreferences {
 public:
  ScopedPreferences() = default;
  ~ScopedPreferences();

  ScopedPreferences(const ScopedPreferences&) = delete;
  ScopedPreferences& operator=(const ScopedPreferences&) = delete;

  bool begin(const char* nvsNamespace, bool readOnly);
  void end();
  bool opened() const { return opened_; }

  Preferences& preferences() { return preferences_; }
  const Preferences& preferences() const { return preferences_; }

 private:
  Preferences preferences_;
  bool opened_ = false;
};

struct CommonRuntimeConfigKeys {
  const char* schema = nullptr;
  const char* configVersion = nullptr;
  const char* configured = nullptr;

  const char* deviceId = nullptr;
  const char* group = nullptr;
  const char* stageId = nullptr;
  const char* location = nullptr;

  const char* wifiSsid = nullptr;
  const char* wifiPassword = nullptr;
  const char* networkAutoStart = nullptr;
  const char* networkStartDelayMs = nullptr;

  const char* mqttHost = nullptr;
  const char* mqttPort = nullptr;
  const char* mqttUsername = nullptr;
  const char* mqttPassword = nullptr;
  const char* mqttRoot = nullptr;

  const char* otaCommandCenterControlled = nullptr;
  const char* otaAutoCheckEnabled = nullptr;
  const char* otaChannel = nullptr;
  const char* otaDesiredBuild = nullptr;
  const char* otaPublicManifestUrl = nullptr;
  const char* otaLocalMirrorUrl = nullptr;
  const char* otaCheckIntervalS = nullptr;
  const char* otaApplyOnlyInSafeState = nullptr;
};

inline CommonRuntimeConfigKeys standardCommonRuntimeConfigKeys() {
  CommonRuntimeConfigKeys keys;
  keys.schema = "schema";
  keys.configVersion = "cfg_ver";
  keys.configured = "configured";
  keys.deviceId = "device_id";
  keys.group = "group";
  keys.stageId = "stage_id";
  keys.location = "location";
  keys.wifiSsid = "wifi_ssid";
  keys.wifiPassword = "wifi_pass";
  keys.networkAutoStart = "net_auto";
  keys.networkStartDelayMs = "net_delay";
  keys.mqttHost = "mqtt_host";
  keys.mqttPort = "mqtt_port";
  keys.mqttUsername = "mqtt_user";
  keys.mqttPassword = "mqtt_pass";
  keys.mqttRoot = "mqtt_root";
  keys.otaCommandCenterControlled = "ota_cc";
  keys.otaAutoCheckEnabled = "ota_auto";
  keys.otaChannel = "ota_channel";
  keys.otaDesiredBuild = "ota_build";
  keys.otaPublicManifestUrl = "ota_pub_url";
  keys.otaLocalMirrorUrl = "ota_mirror_url";
  keys.otaCheckIntervalS = "ota_int_s";
  keys.otaApplyOnlyInSafeState = "ota_safe";
  return keys;
}

struct CommonRuntimeConfigSavePolicy {
  bool requireMqttRoot = true;
  bool requireOtaChannel = true;
  bool requireOtaPublicManifestUrl = false;
};

void loadCommonRuntimeConfig(Preferences& prefs,
                             battlebang::esp::config::CommonRuntimeConfig& config,
                             const CommonRuntimeConfigKeys& keys);

bool saveCommonRuntimeConfig(Preferences& prefs,
                             const battlebang::esp::config::CommonRuntimeConfig& config,
                             const CommonRuntimeConfigKeys& keys,
                             const CommonRuntimeConfigSavePolicy& policy = CommonRuntimeConfigSavePolicy{});

bool clearNamespace(const char* nvsNamespace);

}  // namespace battlebang::esp::nvs
