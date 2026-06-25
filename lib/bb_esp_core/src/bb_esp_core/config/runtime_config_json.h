#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <stdint.h>

#include <bb_esp_core/config/common_runtime_config.h>
#include <bb_esp_core/config/json_getters.h>
#include <bb_esp_core/mqtt/topic_utils.h>

namespace battlebang::esp::config {

inline bool hasJsonField(JsonObjectConst object, const char* key) {
  return !object[key].isNull();
}

inline JsonVariantConst jsonFieldOrAlias(JsonObjectConst object,
                                         const char* primary,
                                         const char* alias) {
  JsonVariantConst value = object[primary];
  return value.isNull() ? object[alias] : value;
}

inline void readStringField(JsonObjectConst object, const char* key, String& value) {
  JsonVariantConst field = object[key];
  if (!field.isNull()) value = getStringOr(field, value);
}

inline void readBoolField(JsonObjectConst object, const char* key, bool& value) {
  JsonVariantConst field = object[key];
  if (!field.isNull()) value = getBoolOr(field, value);
}

inline void readUInt32Field(JsonObjectConst object, const char* key, uint32_t& value) {
  JsonVariantConst field = object[key];
  if (!field.isNull()) value = getUInt32Or(field, value);
}

inline void readUInt16Field(JsonObjectConst object, const char* key, uint16_t& value) {
  JsonVariantConst field = object[key];
  if (!field.isNull()) value = static_cast<uint16_t>(getUInt32Or(field, value));
}

inline void normalizeCommonRuntimeConfig(CommonRuntimeConfig& config) {
  config.deviceId.trim();
  config.group.trim();
  config.stageId.trim();
  config.location.trim();
  config.wifiSsid.trim();
  config.mqttHost.trim();
  config.mqttUsername.trim();
  config.mqttRoot = battlebang::esp::mqtt::trimSlashes(config.mqttRoot);
  config.otaChannel.trim();
  config.otaPublicManifestUrl.trim();
  config.otaLocalMirrorUrl.trim();
  if (config.mqttRoot.length() == 0) config.mqttRoot = "battlebang";
  if (config.otaChannel.length() == 0) config.otaChannel = "stable";
}

inline bool otaUrlHasHost(const String& url, const char* host) {
  const String http = String("http://") + host;
  const String https = String("https://") + host;
  return url == http || url == https || url.startsWith(http + "/") || url.startsWith(https + "/");
}

inline bool isExamplePlaceholderUrl(String url) {
  url.trim();
  url.toLowerCase();
  return otaUrlHasHost(url, "example.invalid") ||
         otaUrlHasHost(url, "example.com") ||
         otaUrlHasHost(url, "example.org") ||
         otaUrlHasHost(url, "example.net");
}

inline bool validateOtaManifestUrl(const String& url, const char* fieldName, String& error) {
  if (!isExamplePlaceholderUrl(url)) return true;
  error = String(fieldName) + " must use a real release manifest URL, not an example placeholder URL";
  return false;
}

inline bool validateCommonRuntimeConfig(CommonRuntimeConfig& config, String& error) {
  normalizeCommonRuntimeConfig(config);
  if (config.schema == 0) {
    error = "schema must be positive";
    return false;
  }
  if (config.deviceId.length() > 0 && !battlebang::esp::mqtt::isSafeTopicSegment(config.deviceId)) {
    error = "device_id must use only A-Z, a-z, 0-9, '_', '-', or '.'";
    return false;
  }
  if (config.stageId.length() > 0 && !battlebang::esp::mqtt::isSafeTopicSegment(config.stageId)) {
    error = "stage_id must use only A-Z, a-z, 0-9, '_', '-', or '.'";
    return false;
  }
  if (config.mqttPort == 0) {
    error = "mqtt.port must be positive";
    return false;
  }
  if (!battlebang::esp::mqtt::normalizeConfiguredRoot(config.mqttRoot, error)) return false;
  if (!battlebang::esp::mqtt::isSafeTopicSegment(config.otaChannel)) {
    error = "ota.channel must be a safe topic segment";
    return false;
  }
  if (!validateOtaManifestUrl(config.otaPublicManifestUrl, "ota.public_manifest_url", error)) {
    return false;
  }
  if (!validateOtaManifestUrl(config.otaLocalMirrorUrl, "ota.local_mirror_url", error)) {
    return false;
  }
  if (config.configured && config.wifiSsid.length() == 0) {
    error = "wifi.ssid is required when configured=true";
    return false;
  }
  if (config.configured && config.mqttHost.length() == 0) {
    error = "mqtt.host is required when configured=true";
    return false;
  }
  return true;
}

inline bool applyCommonRuntimeConfigJson(JsonObjectConst doc,
                                         CommonRuntimeConfig& config,
                                         String& error) {
  if (doc.isNull()) {
    error = "config JSON must be an object";
    return false;
  }

  const String type = getStringOr(doc["type"], String());
  if (type.length() > 0 && type != "config" && type != "provision") {
    error = "type must be 'config' or 'provision'";
    return false;
  }

  JsonVariantConst configVersionField = jsonFieldOrAlias(doc, "config_version", "cfg_ver");
  if (configVersionField.isNull()) {
    error = "config_version is required";
    return false;
  }
  const uint32_t incomingVersion = getUInt32Or(configVersionField, 0);
  if (incomingVersion == 0) {
    error = "config_version must be positive";
    return false;
  }
  if (config.configVersion != 0 && incomingVersion < config.configVersion) {
    error = "config_version must not go backwards";
    return false;
  }

  CommonRuntimeConfig next = config;
  next.configVersion = incomingVersion;
  readUInt16Field(doc, "schema", next.schema);
  readBoolField(doc, "configured", next.configured);
  if (type == "provision") next.configured = true;

  readStringField(doc, "device_id", next.deviceId);
  readStringField(doc, "group", next.group);
  readStringField(doc, "stage_id", next.stageId);
  readStringField(doc, "location", next.location);

  JsonObjectConst wifi = doc["wifi"].as<JsonObjectConst>();
  if (!wifi.isNull()) {
    readStringField(wifi, "ssid", next.wifiSsid);
    readStringField(wifi, "password", next.wifiPassword);
    readBoolField(wifi, "auto_start", next.networkAutoStart);
    readUInt32Field(wifi, "start_delay_ms", next.networkStartDelayMs);
  }
  JsonObjectConst network = doc["network"].as<JsonObjectConst>();
  if (!network.isNull()) {
    readBoolField(network, "auto_start", next.networkAutoStart);
    readUInt32Field(network, "start_delay_ms", next.networkStartDelayMs);
  }

  JsonObjectConst mqtt = doc["mqtt"].as<JsonObjectConst>();
  if (!mqtt.isNull()) {
    readStringField(mqtt, "host", next.mqttHost);
    readUInt16Field(mqtt, "port", next.mqttPort);
    readStringField(mqtt, "username", next.mqttUsername);
    readStringField(mqtt, "password", next.mqttPassword);
    readStringField(mqtt, "root", next.mqttRoot);
  }

  JsonObjectConst ota = doc["ota"].as<JsonObjectConst>();
  if (!ota.isNull()) {
    readBoolField(ota, "command_center_controlled", next.otaCommandCenterControlled);
    readBoolField(ota, "auto_check_enabled", next.otaAutoCheckEnabled);
    readStringField(ota, "channel", next.otaChannel);
    readUInt32Field(ota, "desired_build", next.otaDesiredBuild);
    readStringField(ota, "public_manifest_url", next.otaPublicManifestUrl);
    readStringField(ota, "local_mirror_url", next.otaLocalMirrorUrl);
    readUInt32Field(ota, "check_interval_s", next.otaCheckIntervalS);
    readBoolField(ota, "apply_only_in_safe_state", next.otaApplyOnlyInSafeState);
  }

  if (!validateCommonRuntimeConfig(next, error)) return false;
  config = next;
  return true;
}

inline String maskedSecret(const String& value, bool includeSecrets) {
  if (includeSecrets) return value;
  return value.length() == 0 ? String() : String("********");
}

inline void writeCommonRuntimeConfigJson(JsonObject doc,
                                         const CommonRuntimeConfig& config,
                                         bool includeSecrets) {
  doc["schema"] = config.schema;
  doc["config_version"] = config.configVersion;
  doc["configured"] = config.configured;
  doc["device_id"] = config.deviceId;
  doc["group"] = config.group;
  doc["stage_id"] = config.stageId;
  doc["location"] = config.location;

  JsonObject wifi = doc.createNestedObject("wifi");
  wifi["ssid"] = config.wifiSsid;
  wifi["password"] = maskedSecret(config.wifiPassword, includeSecrets);
  wifi["auto_start"] = config.networkAutoStart;
  wifi["start_delay_ms"] = config.networkStartDelayMs;

  JsonObject mqtt = doc.createNestedObject("mqtt");
  mqtt["host"] = config.mqttHost;
  mqtt["port"] = config.mqttPort;
  mqtt["username"] = config.mqttUsername;
  mqtt["password"] = maskedSecret(config.mqttPassword, includeSecrets);
  mqtt["root"] = config.mqttRoot;

  JsonObject ota = doc.createNestedObject("ota");
  ota["command_center_controlled"] = config.otaCommandCenterControlled;
  ota["auto_check_enabled"] = config.otaAutoCheckEnabled;
  ota["channel"] = config.otaChannel;
  ota["desired_build"] = config.otaDesiredBuild;
  ota["public_manifest_url"] = config.otaPublicManifestUrl;
  ota["local_mirror_url"] = config.otaLocalMirrorUrl;
  ota["check_interval_s"] = config.otaCheckIntervalS;
  ota["apply_only_in_safe_state"] = config.otaApplyOnlyInSafeState;
}

}  // namespace battlebang::esp::config
