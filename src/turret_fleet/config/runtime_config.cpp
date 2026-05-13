#include "runtime_config.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <math.h>

namespace battlebang {
namespace turret_fleet {
namespace {

const char* kNamespace = "bb_fleet";

bool hasKey(JsonObject obj, const char* key) {
  return !obj[key].isNull();
}

String getStringOr(JsonVariant value, const String& fallback) {
  if (value.isNull()) return fallback;
  const char* c = value.as<const char*>();
  return c == nullptr ? fallback : String(c);
}

float getFloatOr(JsonVariant value, float fallback) {
  return value.isNull() ? fallback : value.as<float>();
}

bool isFiniteFloat(float value) {
  return isfinite(value);
}

}  // namespace

RuntimeConfig makeDefaultRuntimeConfig(const String& deviceId) {
  RuntimeConfig config;
  config.deviceId = deviceId;
  return config;
}

bool applyRuntimeConfigJson(const char* json, RuntimeConfig& config, String& error) {
  StaticJsonDocument<1536> doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    error = String("invalid json: ") + err.c_str();
    return false;
  }

  const char* type = doc["type"] | "config";
  if (strcmp(type, "config") != 0) {
    error = String("unsupported type: ") + type;
    return false;
  }

  const uint32_t incomingVersion = doc["config_version"] | config.configVersion;
  if (incomingVersion < config.configVersion) {
    error = "stale config_version";
    return false;
  }

  RuntimeConfig next = config;
  next.schema = doc["schema"] | next.schema;
  next.configVersion = incomingVersion;
  next.configured = doc["configured"] | next.configured;
  next.turretId = getStringOr(doc["turret_id"], next.turretId);

  JsonObject pose = doc["pose"].as<JsonObject>();
  if (pose.isNull()) pose = doc.as<JsonObject>();
  next.xCm = getFloatOr(pose["x_cm"], next.xCm);
  next.yCm = getFloatOr(pose["y_cm"], next.yCm);
  next.zCm = getFloatOr(pose["z_cm"], next.zCm);
  next.defaultTargetZCm = getFloatOr(pose["default_target_z_cm"], next.defaultTargetZCm);

  JsonObject calibration = doc["calibration"].as<JsonObject>();
  if (!calibration.isNull()) {
    next.yawOffsetDeg = getFloatOr(calibration["yaw_offset_deg"], next.yawOffsetDeg);
    next.pitchOffsetDeg = getFloatOr(calibration["pitch_offset_deg"], next.pitchOffsetDeg);
  } else {
    next.yawOffsetDeg = getFloatOr(doc["yaw_offset_deg"], next.yawOffsetDeg);
    next.pitchOffsetDeg = getFloatOr(doc["pitch_offset_deg"], next.pitchOffsetDeg);
  }

  JsonObject wifi = doc["wifi"].as<JsonObject>();
  if (!wifi.isNull()) {
    next.wifiSsid = getStringOr(wifi["ssid"], next.wifiSsid);
    next.wifiPassword = getStringOr(wifi["password"], next.wifiPassword);
  }

  JsonObject mqtt = doc["mqtt"].as<JsonObject>();
  if (!mqtt.isNull()) {
    next.mqttHost = getStringOr(mqtt["host"], next.mqttHost);
    next.mqttPort = mqtt["port"] | next.mqttPort;
    next.mqttUsername = getStringOr(mqtt["username"], next.mqttUsername);
    next.mqttPassword = getStringOr(mqtt["password"], next.mqttPassword);
    next.mqttRoot = getStringOr(mqtt["root"], next.mqttRoot);
  }

  if (next.turretId.length() > 0) next.configured = true;

  if (next.configured && next.turretId.length() == 0) {
    error = "configured=true requires turret_id";
    return false;
  }
  if (!isFiniteFloat(next.xCm) || !isFiniteFloat(next.yCm) || !isFiniteFloat(next.zCm) ||
      !isFiniteFloat(next.defaultTargetZCm) || !isFiniteFloat(next.yawOffsetDeg) ||
      !isFiniteFloat(next.pitchOffsetDeg)) {
    error = "config contains non-finite numeric value";
    return false;
  }
  if (next.mqttRoot.length() == 0) {
    error = "mqtt.root must not be empty";
    return false;
  }

  config = next;
  error = "";
  return true;
}

String runtimeConfigToJson(const RuntimeConfig& config, bool includeSecrets) {
  StaticJsonDocument<1536> doc;
  doc["type"] = "config";
  doc["schema"] = config.schema;
  doc["config_version"] = config.configVersion;
  doc["configured"] = config.configured;
  doc["device_id"] = config.deviceId;
  doc["turret_id"] = config.turretId;

  JsonObject pose = doc.createNestedObject("pose");
  pose["x_cm"] = config.xCm;
  pose["y_cm"] = config.yCm;
  pose["z_cm"] = config.zCm;
  pose["default_target_z_cm"] = config.defaultTargetZCm;

  JsonObject calibration = doc.createNestedObject("calibration");
  calibration["yaw_offset_deg"] = config.yawOffsetDeg;
  calibration["pitch_offset_deg"] = config.pitchOffsetDeg;

  JsonObject wifi = doc.createNestedObject("wifi");
  wifi["ssid"] = config.wifiSsid;
  wifi["password"] = includeSecrets ? config.wifiPassword : "***";

  JsonObject mqtt = doc.createNestedObject("mqtt");
  mqtt["host"] = config.mqttHost;
  mqtt["port"] = config.mqttPort;
  mqtt["username"] = config.mqttUsername;
  mqtt["password"] = includeSecrets ? config.mqttPassword : "***";
  mqtt["root"] = config.mqttRoot;

  String out;
  serializeJson(doc, out);
  return out;
}

bool RuntimeConfigStore::load(RuntimeConfig& config) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return false;

  config.schema = prefs.getUShort("schema", config.schema);
  config.configVersion = prefs.getUInt("cfg_ver", config.configVersion);
  config.configured = prefs.getBool("configured", config.configured);
  config.turretId = prefs.getString("turret_id", config.turretId);
  config.xCm = prefs.getFloat("x_cm", config.xCm);
  config.yCm = prefs.getFloat("y_cm", config.yCm);
  config.zCm = prefs.getFloat("z_cm", config.zCm);
  config.defaultTargetZCm = prefs.getFloat("target_z", config.defaultTargetZCm);
  config.yawOffsetDeg = prefs.getFloat("yaw_off", config.yawOffsetDeg);
  config.pitchOffsetDeg = prefs.getFloat("pitch_off", config.pitchOffsetDeg);
  config.wifiSsid = prefs.getString("wifi_ssid", config.wifiSsid);
  config.wifiPassword = prefs.getString("wifi_pass", config.wifiPassword);
  config.mqttHost = prefs.getString("mqtt_host", config.mqttHost);
  config.mqttPort = prefs.getUShort("mqtt_port", config.mqttPort);
  config.mqttUsername = prefs.getString("mqtt_user", config.mqttUsername);
  config.mqttPassword = prefs.getString("mqtt_pass", config.mqttPassword);
  config.mqttRoot = prefs.getString("mqtt_root", config.mqttRoot);
  prefs.end();
  return true;
}

bool RuntimeConfigStore::save(const RuntimeConfig& config) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return false;

  bool ok = true;
  ok &= prefs.putUShort("schema", config.schema) > 0;
  ok &= prefs.putUInt("cfg_ver", config.configVersion) > 0;
  ok &= prefs.putBool("configured", config.configured) > 0;
  ok &= prefs.putString("turret_id", config.turretId) >= 0;
  ok &= prefs.putFloat("x_cm", config.xCm) > 0;
  ok &= prefs.putFloat("y_cm", config.yCm) > 0;
  ok &= prefs.putFloat("z_cm", config.zCm) > 0;
  ok &= prefs.putFloat("target_z", config.defaultTargetZCm) > 0;
  ok &= prefs.putFloat("yaw_off", config.yawOffsetDeg) > 0;
  ok &= prefs.putFloat("pitch_off", config.pitchOffsetDeg) > 0;
  ok &= prefs.putString("wifi_ssid", config.wifiSsid) >= 0;
  ok &= prefs.putString("wifi_pass", config.wifiPassword) >= 0;
  ok &= prefs.putString("mqtt_host", config.mqttHost) >= 0;
  ok &= prefs.putUShort("mqtt_port", config.mqttPort) > 0;
  ok &= prefs.putString("mqtt_user", config.mqttUsername) >= 0;
  ok &= prefs.putString("mqtt_pass", config.mqttPassword) >= 0;
  ok &= prefs.putString("mqtt_root", config.mqttRoot) >= 0;
  prefs.end();
  return ok;
}

bool RuntimeConfigStore::clear() {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return false;
  const bool ok = prefs.clear();
  prefs.end();
  return ok;
}

}  // namespace turret_fleet
}  // namespace battlebang
