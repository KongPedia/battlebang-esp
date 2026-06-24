#include <bb_esp_nvs/common_runtime_config_store.h>

namespace battlebang::esp::nvs {
namespace {

bool hasKey(const char* key) {
  return key != nullptr && key[0] != '\0';
}

void loadString(Preferences& prefs, const char* key, String& value) {
  if (hasKey(key)) value = prefs.getString(key, value);
}

void loadBool(Preferences& prefs, const char* key, bool& value) {
  if (hasKey(key)) value = prefs.getBool(key, value);
}

void loadUInt(Preferences& prefs, const char* key, uint32_t& value) {
  if (hasKey(key)) value = prefs.getUInt(key, value);
}

void loadUShort(Preferences& prefs, const char* key, uint16_t& value) {
  if (hasKey(key)) value = prefs.getUShort(key, value);
}

bool saveString(Preferences& prefs, const char* key, const String& value, bool requireNonEmpty) {
  if (!hasKey(key)) return true;
  if (requireNonEmpty && value.length() == 0) return false;
  const size_t written = prefs.putString(key, value);
  return written > 0 || value.length() == 0;
}

bool saveBool(Preferences& prefs, const char* key, bool value) {
  return !hasKey(key) || prefs.putBool(key, value) > 0;
}

bool saveUInt(Preferences& prefs, const char* key, uint32_t value) {
  return !hasKey(key) || prefs.putUInt(key, value) > 0;
}

bool saveUShort(Preferences& prefs, const char* key, uint16_t value) {
  return !hasKey(key) || prefs.putUShort(key, value) > 0;
}

}  // namespace

ScopedPreferences::~ScopedPreferences() {
  end();
}

bool ScopedPreferences::begin(const char* nvsNamespace, bool readOnly) {
  end();
  opened_ = preferences_.begin(nvsNamespace, readOnly);
  return opened_;
}

void ScopedPreferences::end() {
  if (!opened_) return;
  preferences_.end();
  opened_ = false;
}

void loadCommonRuntimeConfig(Preferences& prefs,
                             battlebang::esp::config::CommonRuntimeConfig& config,
                             const CommonRuntimeConfigKeys& keys) {
  loadUShort(prefs, keys.schema, config.schema);
  loadUInt(prefs, keys.configVersion, config.configVersion);
  loadBool(prefs, keys.configured, config.configured);

  loadString(prefs, keys.deviceId, config.deviceId);
  loadString(prefs, keys.group, config.group);
  loadString(prefs, keys.stageId, config.stageId);
  loadString(prefs, keys.location, config.location);

  loadString(prefs, keys.wifiSsid, config.wifiSsid);
  loadString(prefs, keys.wifiPassword, config.wifiPassword);
  loadBool(prefs, keys.networkAutoStart, config.networkAutoStart);
  loadUInt(prefs, keys.networkStartDelayMs, config.networkStartDelayMs);

  loadString(prefs, keys.mqttHost, config.mqttHost);
  loadUShort(prefs, keys.mqttPort, config.mqttPort);
  loadString(prefs, keys.mqttUsername, config.mqttUsername);
  loadString(prefs, keys.mqttPassword, config.mqttPassword);
  loadString(prefs, keys.mqttRoot, config.mqttRoot);

  loadBool(prefs, keys.otaCommandCenterControlled, config.otaCommandCenterControlled);
  loadBool(prefs, keys.otaAutoCheckEnabled, config.otaAutoCheckEnabled);
  loadString(prefs, keys.otaChannel, config.otaChannel);
  loadUInt(prefs, keys.otaDesiredBuild, config.otaDesiredBuild);
  loadString(prefs, keys.otaPublicManifestUrl, config.otaPublicManifestUrl);
  loadString(prefs, keys.otaLocalMirrorUrl, config.otaLocalMirrorUrl);
  loadUInt(prefs, keys.otaCheckIntervalS, config.otaCheckIntervalS);
  loadBool(prefs, keys.otaApplyOnlyInSafeState, config.otaApplyOnlyInSafeState);
}

bool saveCommonRuntimeConfig(Preferences& prefs,
                             const battlebang::esp::config::CommonRuntimeConfig& config,
                             const CommonRuntimeConfigKeys& keys,
                             const CommonRuntimeConfigSavePolicy& policy) {
  bool ok = true;
  ok &= saveUShort(prefs, keys.schema, config.schema);
  ok &= saveUInt(prefs, keys.configVersion, config.configVersion);
  ok &= saveBool(prefs, keys.configured, config.configured);

  ok &= saveString(prefs, keys.deviceId, config.deviceId, false);
  ok &= saveString(prefs, keys.group, config.group, false);
  ok &= saveString(prefs, keys.stageId, config.stageId, false);
  ok &= saveString(prefs, keys.location, config.location, false);

  ok &= saveString(prefs, keys.wifiSsid, config.wifiSsid, false);
  ok &= saveString(prefs, keys.wifiPassword, config.wifiPassword, false);
  ok &= saveBool(prefs, keys.networkAutoStart, config.networkAutoStart);
  ok &= saveUInt(prefs, keys.networkStartDelayMs, config.networkStartDelayMs);

  ok &= saveString(prefs, keys.mqttHost, config.mqttHost, false);
  ok &= saveUShort(prefs, keys.mqttPort, config.mqttPort);
  ok &= saveString(prefs, keys.mqttUsername, config.mqttUsername, false);
  ok &= saveString(prefs, keys.mqttPassword, config.mqttPassword, false);
  ok &= saveString(prefs, keys.mqttRoot, config.mqttRoot, policy.requireMqttRoot);

  ok &= saveBool(prefs, keys.otaCommandCenterControlled, config.otaCommandCenterControlled);
  ok &= saveBool(prefs, keys.otaAutoCheckEnabled, config.otaAutoCheckEnabled);
  ok &= saveString(prefs, keys.otaChannel, config.otaChannel, policy.requireOtaChannel);
  ok &= saveUInt(prefs, keys.otaDesiredBuild, config.otaDesiredBuild);
  ok &= saveString(prefs, keys.otaPublicManifestUrl, config.otaPublicManifestUrl, policy.requireOtaPublicManifestUrl);
  ok &= saveString(prefs, keys.otaLocalMirrorUrl, config.otaLocalMirrorUrl, false);
  ok &= saveUInt(prefs, keys.otaCheckIntervalS, config.otaCheckIntervalS);
  ok &= saveBool(prefs, keys.otaApplyOnlyInSafeState, config.otaApplyOnlyInSafeState);
  return ok;
}

bool clearNamespace(const char* nvsNamespace) {
  ScopedPreferences prefs;
  if (!prefs.begin(nvsNamespace, false)) return false;
  return prefs.preferences().clear();
}

}  // namespace battlebang::esp::nvs
