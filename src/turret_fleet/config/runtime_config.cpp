#include "runtime_config.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <math.h>
#include <string.h>

#include "../app/firmware_info.h"

namespace battlebang {
namespace turret_fleet {
namespace {

const char* kNamespace = "bb_fleet";

String getStringOr(JsonVariantConst value, const String& fallback) {
  if (value.isNull()) return fallback;
  const char* c = value.as<const char*>();
  return c == nullptr ? fallback : String(c);
}

float getFloatOr(JsonVariantConst value, float fallback) {
  return value.isNull() ? fallback : value.as<float>();
}

uint32_t getUIntOr(JsonVariantConst value, uint32_t fallback) {
  return value.isNull() ? fallback : value.as<uint32_t>();
}

bool getBoolOr(JsonVariantConst value, bool fallback) {
  return value.isNull() ? fallback : value.as<bool>();
}

bool isFiniteFloat(float value) {
  return isfinite(value);
}

String normalizedUnit(String unit) {
  unit.trim();
  unit.toLowerCase();
  return unit;
}

bool isValidMqttTargetUnit(const String& unit) {
  const String normalized = normalizedUnit(unit);
  return normalized == "m" || normalized == "meter" || normalized == "meters" ||
         normalized == "cm" || normalized == "centimeter" || normalized == "centimeters";
}

}  // namespace

RuntimeConfig makeDefaultRuntimeConfig(const String& deviceId) {
  RuntimeConfig config;
  config.deviceId = deviceId;
  config.otaPublicManifestUrl = BB_TURRET_FLEET_LATEST_MANIFEST_URL;
  return config;
}

bool mqttTargetsInMeters(const RuntimeConfig& config) {
  const String normalized = normalizedUnit(config.mqttTargetUnit);
  return normalized == "m" || normalized == "meter" || normalized == "meters";
}

bool applyRuntimeConfigJson(const char* json, RuntimeConfig& config, String& error) {
  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    error = String("invalid json: ") + err.c_str();
    return false;
  }

  const char* type = doc["type"] | "config";
  if (strcmp(type, "config") != 0 && strcmp(type, "provision") != 0) {
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
  next.group = getStringOr(doc["group"], next.group);
  next.floor = doc["floor"] | next.floor;
  next.side = getStringOr(doc["side"], next.side);

  JsonObjectConst frame = doc["coordinate_frame"].as<JsonObjectConst>();
  if (!frame.isNull()) {
    next.frameId = getStringOr(frame["frame_id"], next.frameId);
    next.frameUnit = getStringOr(frame["unit"], next.frameUnit);
    next.frameOrigin = getStringOr(frame["origin"], next.frameOrigin);
    next.frameXAxis = getStringOr(frame["x_axis"], next.frameXAxis);
    next.frameYAxis = getStringOr(frame["y_axis"], next.frameYAxis);
    next.frameZAxis = getStringOr(frame["z_axis"], next.frameZAxis);
    next.mqttTargetUnit = getStringOr(frame["mqtt_target_unit"], next.mqttTargetUnit);
  }

  JsonObjectConst pose = doc["pose"].as<JsonObjectConst>();
  if (pose.isNull()) pose = doc.as<JsonObjectConst>();
  next.xCm = getFloatOr(pose["x_cm"], next.xCm);
  next.yCm = getFloatOr(pose["y_cm"], next.yCm);
  next.zCm = getFloatOr(pose["z_cm"], next.zCm);
  next.defaultTargetZCm = getFloatOr(pose["default_target_z_cm"], next.defaultTargetZCm);

  JsonObjectConst calibration = doc["calibration"].as<JsonObjectConst>();
  if (!calibration.isNull()) {
    next.yawZeroReference = getStringOr(calibration["yaw_zero_reference"], next.yawZeroReference);
    next.yawOffsetDeg = getFloatOr(calibration["yaw_offset_deg"], next.yawOffsetDeg);
    next.pitchOffsetDeg = getFloatOr(calibration["pitch_offset_deg"], next.pitchOffsetDeg);
    next.yawAxisOffsetDeg = getFloatOr(calibration["yaw_axis_offset_deg"],
                                       getFloatOr(calibration["yaw_sensor_offset_deg"], next.yawAxisOffsetDeg));
    next.pitchAxisOffsetDeg = getFloatOr(calibration["pitch_axis_offset_deg"],
                                         getFloatOr(calibration["pitch_sensor_offset_deg"], next.pitchAxisOffsetDeg));
    next.homeYawDeg = getFloatOr(calibration["home_yaw_deg"], next.homeYawDeg);
    next.homePitchDeg = getFloatOr(calibration["home_pitch_deg"], next.homePitchDeg);
  } else {
    next.yawZeroReference = getStringOr(doc["yaw_zero_reference"], next.yawZeroReference);
    next.yawOffsetDeg = getFloatOr(doc["yaw_offset_deg"], next.yawOffsetDeg);
    next.pitchOffsetDeg = getFloatOr(doc["pitch_offset_deg"], next.pitchOffsetDeg);
    next.yawAxisOffsetDeg = getFloatOr(doc["yaw_axis_offset_deg"],
                                       getFloatOr(doc["yaw_sensor_offset_deg"], next.yawAxisOffsetDeg));
    next.pitchAxisOffsetDeg = getFloatOr(doc["pitch_axis_offset_deg"],
                                         getFloatOr(doc["pitch_sensor_offset_deg"], next.pitchAxisOffsetDeg));
    next.homeYawDeg = getFloatOr(doc["home_yaw_deg"], next.homeYawDeg);
    next.homePitchDeg = getFloatOr(doc["home_pitch_deg"], next.homePitchDeg);
  }

  JsonObjectConst fire = doc["fire"].as<JsonObjectConst>();
  if (!fire.isNull()) {
    next.fireHardwareEnabled = getBoolOr(fire["hardware_enabled"], next.fireHardwareEnabled);
    next.fireEscRunUs = fire["esc_run_us"] | next.fireEscRunUs;
    next.fireEscStopUs = fire["esc_stop_us"] | next.fireEscStopUs;
    next.fireDefaultHoldMs = getUIntOr(fire["default_hold_ms"], next.fireDefaultHoldMs);
    next.fireMinHoldMs = getUIntOr(fire["min_hold_ms"], next.fireMinHoldMs);
    next.fireMaxHoldMs = getUIntOr(fire["max_hold_ms"], next.fireMaxHoldMs);
    next.fireRelayStepDelayMs = getUIntOr(fire["relay_step_delay_ms"], next.fireRelayStepDelayMs);
  }

  JsonObjectConst motion = doc["motion"].as<JsonObjectConst>();
  if (!motion.isNull()) {
    next.yawMinDeg = getFloatOr(motion["yaw_min_deg"], next.yawMinDeg);
    next.yawMaxDeg = getFloatOr(motion["yaw_max_deg"], next.yawMaxDeg);
    next.pitchMinDeg = getFloatOr(motion["pitch_min_deg"], next.pitchMinDeg);
    next.pitchMaxDeg = getFloatOr(motion["pitch_max_deg"], next.pitchMaxDeg);
    next.yawStopUs = motion["yaw_stop_us"] | next.yawStopUs;
    next.pitchStopUs = motion["pitch_stop_us"] | next.pitchStopUs;
    if (motion["servo_max_delta_us"].is<uint16_t>()) {
      next.servoMaxDeltaUs = motion["servo_max_delta_us"].as<uint16_t>();
      next.yawMaxDeltaUs = next.servoMaxDeltaUs;
      next.pitchMaxDeltaUs = next.servoMaxDeltaUs;
    }
    next.yawMaxDeltaUs = motion["yaw_max_delta_us"] | next.yawMaxDeltaUs;
    next.pitchMaxDeltaUs = motion["pitch_max_delta_us"] | next.pitchMaxDeltaUs;
    next.yawMinDriveUs = motion["yaw_min_drive_us"] | next.yawMinDriveUs;
    next.pitchMinDriveUs = motion["pitch_min_drive_us"] | next.pitchMinDriveUs;
    next.servoAttachSettleMs = motion["servo_attach_settle_ms"] | next.servoAttachSettleMs;
    next.axisSwitchCooldownMs = motion["axis_switch_cooldown_ms"] | next.axisSwitchCooldownMs;
    next.axisDivergenceGuardMs = motion["axis_divergence_guard_ms"] | next.axisDivergenceGuardMs;
    next.axisDivergenceMarginDeg = getFloatOr(motion["axis_divergence_margin_deg"], next.axisDivergenceMarginDeg);
    JsonObjectConst servo = motion["servo"].as<JsonObjectConst>();
    if (!servo.isNull()) {
      next.yawStopUs = servo["yaw_stop_us"] | next.yawStopUs;
      next.pitchStopUs = servo["pitch_stop_us"] | next.pitchStopUs;
      if (servo["max_delta_us"].is<uint16_t>()) {
        next.servoMaxDeltaUs = servo["max_delta_us"].as<uint16_t>();
        next.yawMaxDeltaUs = next.servoMaxDeltaUs;
        next.pitchMaxDeltaUs = next.servoMaxDeltaUs;
      }
      next.yawMaxDeltaUs = servo["yaw_max_delta_us"] | next.yawMaxDeltaUs;
      next.pitchMaxDeltaUs = servo["pitch_max_delta_us"] | next.pitchMaxDeltaUs;
      next.yawMinDriveUs = servo["yaw_min_drive_us"] | next.yawMinDriveUs;
      next.pitchMinDriveUs = servo["pitch_min_drive_us"] | next.pitchMinDriveUs;
      next.servoAttachSettleMs = servo["attach_settle_ms"] | next.servoAttachSettleMs;
      next.axisSwitchCooldownMs = servo["axis_switch_cooldown_ms"] | next.axisSwitchCooldownMs;
      next.axisDivergenceGuardMs = servo["axis_divergence_guard_ms"] | next.axisDivergenceGuardMs;
      next.axisDivergenceMarginDeg = getFloatOr(servo["axis_divergence_margin_deg"], next.axisDivergenceMarginDeg);
    }
    JsonObjectConst limits = motion["limits"].as<JsonObjectConst>();
    if (!limits.isNull()) {
      next.yawMinDeg = getFloatOr(limits["yaw_min_deg"], next.yawMinDeg);
      next.yawMaxDeg = getFloatOr(limits["yaw_max_deg"], next.yawMaxDeg);
      next.pitchMinDeg = getFloatOr(limits["pitch_min_deg"], next.pitchMinDeg);
      next.pitchMaxDeg = getFloatOr(limits["pitch_max_deg"], next.pitchMaxDeg);
    }
    JsonObjectConst home = motion["home"].as<JsonObjectConst>();
    if (!home.isNull()) {
      next.homeYawDeg = getFloatOr(home["yaw_deg"], next.homeYawDeg);
      next.homePitchDeg = getFloatOr(home["pitch_deg"], next.homePitchDeg);
    }
    JsonObjectConst idle = motion["idle"].as<JsonObjectConst>();
    if (!idle.isNull()) {
      next.idleYawMinDeg = getFloatOr(idle["yaw_min_deg"], next.idleYawMinDeg);
      next.idleYawMaxDeg = getFloatOr(idle["yaw_max_deg"], next.idleYawMaxDeg);
      next.idleYawSpeedDegS = getFloatOr(idle["yaw_speed_deg_s"], next.idleYawSpeedDegS);
      next.idlePitchMinDeg = getFloatOr(idle["pitch_min_deg"], next.idlePitchMinDeg);
      next.idlePitchMaxDeg = getFloatOr(idle["pitch_max_deg"], next.idlePitchMaxDeg);
      next.idlePitchSpeedDegS = getFloatOr(idle["pitch_speed_deg_s"], next.idlePitchSpeedDegS);
    }
    JsonObjectConst dead = motion["dead"].as<JsonObjectConst>();
    if (!dead.isNull()) {
      next.deadPitchDeg = getFloatOr(dead["pitch_deg"], next.deadPitchDeg);
    }
  }

  JsonObjectConst wifi = doc["wifi"].as<JsonObjectConst>();
  if (!wifi.isNull()) {
    next.wifiSsid = getStringOr(wifi["ssid"], next.wifiSsid);
    next.wifiPassword = getStringOr(wifi["password"], next.wifiPassword);
  }

  JsonObjectConst network = doc["network"].as<JsonObjectConst>();
  if (!network.isNull()) {
    next.networkAutoStart = getBoolOr(network["auto_start"], next.networkAutoStart);
    next.networkStartDelayMs = getUIntOr(network["start_delay_ms"], next.networkStartDelayMs);
  }

  JsonObjectConst mqtt = doc["mqtt"].as<JsonObjectConst>();
  if (!mqtt.isNull()) {
    next.mqttHost = getStringOr(mqtt["host"], next.mqttHost);
    next.mqttPort = mqtt["port"] | next.mqttPort;
    next.mqttUsername = getStringOr(mqtt["username"], next.mqttUsername);
    next.mqttPassword = getStringOr(mqtt["password"], next.mqttPassword);
    next.mqttRoot = getStringOr(mqtt["root"], next.mqttRoot);
  }

  JsonObjectConst ota = doc["ota"].as<JsonObjectConst>();
  if (!ota.isNull()) {
    next.otaCommandCenterControlled = getBoolOr(ota["command_center_controlled"], next.otaCommandCenterControlled);
    next.otaAutoCheckEnabled = getBoolOr(ota["auto_check_enabled"], next.otaAutoCheckEnabled);
    next.otaChannel = getStringOr(ota["channel"], next.otaChannel);
    next.otaDesiredBuild = getUIntOr(ota["desired_build"], next.otaDesiredBuild);
    next.otaPublicManifestUrl = getStringOr(ota["public_manifest_url"], next.otaPublicManifestUrl);
    next.otaLocalMirrorUrl = getStringOr(ota["local_mirror_url"], next.otaLocalMirrorUrl);
    next.otaCheckIntervalS = getUIntOr(ota["check_interval_s"], next.otaCheckIntervalS);
    next.otaApplyOnlyInSafeState = getBoolOr(ota["apply_only_in_safe_state"], next.otaApplyOnlyInSafeState);
  }

  if (next.turretId.length() > 0) next.configured = true;

  if (next.configured && next.turretId.length() == 0) {
    error = "configured=true requires turret_id";
    return false;
  }
  if (next.frameId.length() == 0) {
    error = "coordinate_frame.frame_id must not be empty";
    return false;
  }
  if (!isValidMqttTargetUnit(next.mqttTargetUnit)) {
    error = "coordinate_frame.mqtt_target_unit must be m or cm";
    return false;
  }
  if (!isFiniteFloat(next.xCm) || !isFiniteFloat(next.yCm) || !isFiniteFloat(next.zCm) ||
      !isFiniteFloat(next.defaultTargetZCm) || !isFiniteFloat(next.yawOffsetDeg) ||
      !isFiniteFloat(next.pitchOffsetDeg) || !isFiniteFloat(next.yawAxisOffsetDeg) ||
      !isFiniteFloat(next.pitchAxisOffsetDeg) || !isFiniteFloat(next.homeYawDeg) ||
      !isFiniteFloat(next.homePitchDeg) || !isFiniteFloat(next.yawMinDeg) ||
      !isFiniteFloat(next.yawMaxDeg) || !isFiniteFloat(next.pitchMinDeg) ||
      !isFiniteFloat(next.pitchMaxDeg) || !isFiniteFloat(next.deadPitchDeg) ||
      !isFiniteFloat(next.idleYawMinDeg) || !isFiniteFloat(next.idleYawMaxDeg) ||
      !isFiniteFloat(next.idleYawSpeedDegS) || !isFiniteFloat(next.idlePitchMinDeg) ||
      !isFiniteFloat(next.idlePitchMaxDeg) || !isFiniteFloat(next.idlePitchSpeedDegS) ||
      !isFiniteFloat(next.axisDivergenceMarginDeg)) {
    error = "config contains non-finite numeric value";
    return false;
  }
  if (next.yawMinDeg < -140.0f || next.yawMaxDeg > 140.0f ||
      next.yawMinDeg > next.yawMaxDeg ||
      next.yawMaxDeg - next.yawMinDeg > 150.0f ||
      next.pitchMinDeg < -90.0f || next.pitchMaxDeg > 90.0f ||
      next.pitchMinDeg > next.pitchMaxDeg ||
      next.pitchMaxDeg - next.pitchMinDeg > 150.0f ||
      next.homeYawDeg < next.yawMinDeg || next.homeYawDeg > next.yawMaxDeg ||
      next.homePitchDeg < next.pitchMinDeg || next.homePitchDeg > next.pitchMaxDeg ||
      next.deadPitchDeg < next.pitchMinDeg || next.deadPitchDeg > next.pitchMaxDeg ||
      next.idleYawMinDeg < next.yawMinDeg || next.idleYawMaxDeg > next.yawMaxDeg ||
      next.idleYawMinDeg > next.idleYawMaxDeg ||
      next.idlePitchMinDeg < next.pitchMinDeg || next.idlePitchMaxDeg > next.pitchMaxDeg ||
      next.idlePitchMinDeg > next.idlePitchMaxDeg ||
      next.idleYawSpeedDegS < 0.0f || next.idleYawSpeedDegS > 120.0f ||
      next.idlePitchSpeedDegS < 0.0f || next.idlePitchSpeedDegS > 60.0f) {
    error = "motion home/limits/idle/dead settings out of safe range";
    return false;
  }
  if (next.yawStopUs < 1400 || next.yawStopUs > 1600 ||
      next.pitchStopUs < 1400 || next.pitchStopUs > 1600) {
    error = "motion servo stop pulse widths must be 1400..1600us";
    return false;
  }
  if (next.servoMaxDeltaUs < 20 || next.servoMaxDeltaUs > 400 ||
      next.yawMaxDeltaUs < 20 || next.yawMaxDeltaUs > 400 ||
      next.pitchMaxDeltaUs < 20 || next.pitchMaxDeltaUs > 400 ||
      next.yawMinDriveUs > next.yawMaxDeltaUs ||
      next.pitchMinDriveUs > next.pitchMaxDeltaUs ||
      next.servoAttachSettleMs > 3000 ||
      next.axisSwitchCooldownMs > 5000 ||
      next.axisDivergenceGuardMs > 10000 ||
      next.axisDivergenceMarginDeg < 1.0f ||
      next.axisDivergenceMarginDeg > 60.0f) {
    error = "motion servo tuning out of safe range";
    return false;
  }
  if (next.fireEscRunUs < 1000 || next.fireEscRunUs > 2000 ||
      next.fireEscStopUs < 900 || next.fireEscStopUs > 1100) {
    error = "fire esc pulse widths out of safe range";
    return false;
  }
  if (next.fireMinHoldMs == 0 || next.fireDefaultHoldMs < next.fireMinHoldMs ||
      next.fireDefaultHoldMs > next.fireMaxHoldMs || next.fireMaxHoldMs > 60000 ||
      next.fireRelayStepDelayMs > 5000) {
    error = "fire timing out of safe range";
    return false;
  }
  if (next.networkStartDelayMs > 300000) {
    error = "network.start_delay_ms must be <= 300000";
    return false;
  }
  if (next.mqttRoot.length() == 0) {
    error = "mqtt.root must not be empty";
    return false;
  }
  if (next.mqttPort == 0) {
    error = "mqtt.port must be > 0";
    return false;
  }

  config = next;
  error = "";
  return true;
}

String runtimeConfigToJson(const RuntimeConfig& config, bool includeSecrets) {
  DynamicJsonDocument doc(4096);
  doc["type"] = "config";
  doc["schema"] = config.schema;
  doc["config_version"] = config.configVersion;
  doc["configured"] = config.configured;
  doc["device_id"] = config.deviceId;
  doc["turret_id"] = config.turretId;
  doc["group"] = config.group;
  doc["floor"] = config.floor;
  doc["side"] = config.side;

  JsonObject frame = doc.createNestedObject("coordinate_frame");
  frame["frame_id"] = config.frameId;
  frame["unit"] = config.frameUnit;
  frame["origin"] = config.frameOrigin;
  frame["x_axis"] = config.frameXAxis;
  frame["y_axis"] = config.frameYAxis;
  frame["z_axis"] = config.frameZAxis;
  frame["mqtt_target_unit"] = config.mqttTargetUnit;

  JsonObject pose = doc.createNestedObject("pose");
  pose["x_cm"] = config.xCm;
  pose["y_cm"] = config.yCm;
  pose["z_cm"] = config.zCm;
  pose["default_target_z_cm"] = config.defaultTargetZCm;

  JsonObject calibration = doc.createNestedObject("calibration");
  calibration["yaw_zero_reference"] = config.yawZeroReference;
  calibration["yaw_offset_deg"] = config.yawOffsetDeg;
  calibration["pitch_offset_deg"] = config.pitchOffsetDeg;
  calibration["yaw_axis_offset_deg"] = config.yawAxisOffsetDeg;
  calibration["pitch_axis_offset_deg"] = config.pitchAxisOffsetDeg;
  calibration["home_yaw_deg"] = config.homeYawDeg;
  calibration["home_pitch_deg"] = config.homePitchDeg;

  JsonObject fire = doc.createNestedObject("fire");
  fire["hardware_enabled"] = config.fireHardwareEnabled;
  fire["esc_run_us"] = config.fireEscRunUs;
  fire["esc_stop_us"] = config.fireEscStopUs;
  fire["default_hold_ms"] = config.fireDefaultHoldMs;
  fire["min_hold_ms"] = config.fireMinHoldMs;
  fire["max_hold_ms"] = config.fireMaxHoldMs;
  fire["relay_step_delay_ms"] = config.fireRelayStepDelayMs;

  JsonObject motion = doc.createNestedObject("motion");
  motion["yaw_stop_us"] = config.yawStopUs;
  motion["pitch_stop_us"] = config.pitchStopUs;
  motion["servo_max_delta_us"] = config.servoMaxDeltaUs;
  motion["yaw_max_delta_us"] = config.yawMaxDeltaUs;
  motion["pitch_max_delta_us"] = config.pitchMaxDeltaUs;
  motion["yaw_min_drive_us"] = config.yawMinDriveUs;
  motion["pitch_min_drive_us"] = config.pitchMinDriveUs;
  motion["servo_attach_settle_ms"] = config.servoAttachSettleMs;
  motion["axis_switch_cooldown_ms"] = config.axisSwitchCooldownMs;
  motion["axis_divergence_guard_ms"] = config.axisDivergenceGuardMs;
  motion["axis_divergence_margin_deg"] = config.axisDivergenceMarginDeg;
  JsonObject limits = motion.createNestedObject("limits");
  limits["yaw_min_deg"] = config.yawMinDeg;
  limits["yaw_max_deg"] = config.yawMaxDeg;
  limits["pitch_min_deg"] = config.pitchMinDeg;
  limits["pitch_max_deg"] = config.pitchMaxDeg;
  JsonObject home = motion.createNestedObject("home");
  home["yaw_deg"] = config.homeYawDeg;
  home["pitch_deg"] = config.homePitchDeg;
  JsonObject idle = motion.createNestedObject("idle");
  idle["yaw_min_deg"] = config.idleYawMinDeg;
  idle["yaw_max_deg"] = config.idleYawMaxDeg;
  idle["yaw_speed_deg_s"] = config.idleYawSpeedDegS;
  idle["pitch_min_deg"] = config.idlePitchMinDeg;
  idle["pitch_max_deg"] = config.idlePitchMaxDeg;
  idle["pitch_speed_deg_s"] = config.idlePitchSpeedDegS;
  JsonObject dead = motion.createNestedObject("dead");
  dead["pitch_deg"] = config.deadPitchDeg;

  JsonObject wifi = doc.createNestedObject("wifi");
  wifi["ssid"] = config.wifiSsid;
  wifi["password"] = includeSecrets ? config.wifiPassword : "***";

  JsonObject network = doc.createNestedObject("network");
  network["auto_start"] = config.networkAutoStart;
  network["start_delay_ms"] = config.networkStartDelayMs;

  JsonObject mqtt = doc.createNestedObject("mqtt");
  mqtt["host"] = config.mqttHost;
  mqtt["port"] = config.mqttPort;
  mqtt["username"] = config.mqttUsername;
  mqtt["password"] = includeSecrets ? config.mqttPassword : "***";
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
  config.group = prefs.getString("group", config.group);
  config.floor = prefs.getInt("floor", config.floor);
  config.side = prefs.getString("side", config.side);
  config.frameId = prefs.getString("frame_id", config.frameId);
  config.frameUnit = prefs.getString("frame_unit", config.frameUnit);
  config.frameOrigin = prefs.getString("origin", config.frameOrigin);
  config.frameXAxis = prefs.getString("x_axis", config.frameXAxis);
  config.frameYAxis = prefs.getString("y_axis", config.frameYAxis);
  config.frameZAxis = prefs.getString("z_axis", config.frameZAxis);
  config.mqttTargetUnit = prefs.getString("target_unit", config.mqttTargetUnit);
  config.xCm = prefs.getFloat("x_cm", config.xCm);
  config.yCm = prefs.getFloat("y_cm", config.yCm);
  config.zCm = prefs.getFloat("z_cm", config.zCm);
  config.defaultTargetZCm = prefs.getFloat("target_z", config.defaultTargetZCm);
  config.yawZeroReference = prefs.getString("yaw_zero_ref", config.yawZeroReference);
  config.yawOffsetDeg = prefs.getFloat("yaw_off", config.yawOffsetDeg);
  config.pitchOffsetDeg = prefs.getFloat("pitch_off", config.pitchOffsetDeg);
  config.yawAxisOffsetDeg = prefs.getFloat("yaw_axis", config.yawAxisOffsetDeg);
  config.pitchAxisOffsetDeg = prefs.getFloat("pitch_axis", config.pitchAxisOffsetDeg);
  config.homeYawDeg = prefs.getFloat("home_yaw", config.homeYawDeg);
  config.homePitchDeg = prefs.getFloat("home_pitch", config.homePitchDeg);
  config.yawMinDeg = prefs.getFloat("yaw_min_deg", config.yawMinDeg);
  config.yawMaxDeg = prefs.getFloat("yaw_max_deg", config.yawMaxDeg);
  config.pitchMinDeg = prefs.getFloat("pit_min_deg", config.pitchMinDeg);
  config.pitchMaxDeg = prefs.getFloat("pit_max_deg", config.pitchMaxDeg);
  config.fireHardwareEnabled = prefs.getBool("fire_hw", config.fireHardwareEnabled);
  config.fireEscRunUs = prefs.getUShort("fire_run", config.fireEscRunUs);
  config.fireEscStopUs = prefs.getUShort("fire_stop", config.fireEscStopUs);
  config.fireDefaultHoldMs = prefs.getUInt("fire_def", config.fireDefaultHoldMs);
  config.fireMinHoldMs = prefs.getUInt("fire_min", config.fireMinHoldMs);
  config.fireMaxHoldMs = prefs.getUInt("fire_max", config.fireMaxHoldMs);
  config.fireRelayStepDelayMs = prefs.getUInt("fire_step", config.fireRelayStepDelayMs);
  config.deadPitchDeg = prefs.getFloat("dead_pitch", config.deadPitchDeg);
  config.idleYawMinDeg = prefs.getFloat("idle_ymin", config.idleYawMinDeg);
  config.idleYawMaxDeg = prefs.getFloat("idle_ymax", config.idleYawMaxDeg);
  config.idleYawSpeedDegS = prefs.getFloat("idle_yspd", config.idleYawSpeedDegS);
  config.idlePitchMinDeg = prefs.getFloat("idle_pmin", config.idlePitchMinDeg);
  config.idlePitchMaxDeg = prefs.getFloat("idle_pmax", config.idlePitchMaxDeg);
  config.idlePitchSpeedDegS = prefs.getFloat("idle_pspd", config.idlePitchSpeedDegS);
  config.yawStopUs = prefs.getUShort("yaw_stop", config.yawStopUs);
  config.pitchStopUs = prefs.getUShort("pitch_stop", config.pitchStopUs);
  config.servoMaxDeltaUs = prefs.getUShort("srv_delta", config.servoMaxDeltaUs);
  config.yawMaxDeltaUs = prefs.getUShort("yaw_max", config.yawMaxDeltaUs);
  config.pitchMaxDeltaUs = prefs.getUShort("pit_max", config.pitchMaxDeltaUs);
  config.yawMinDriveUs = prefs.getUShort("yaw_min_drv", config.yawMinDriveUs);
  config.pitchMinDriveUs = prefs.getUShort("pit_min_drv", config.pitchMinDriveUs);
  config.servoAttachSettleMs = prefs.getUShort("srv_settle", config.servoAttachSettleMs);
  config.axisSwitchCooldownMs = prefs.getUShort("axis_cool", config.axisSwitchCooldownMs);
  config.axisDivergenceGuardMs = prefs.getUShort("axis_guard", config.axisDivergenceGuardMs);
  config.axisDivergenceMarginDeg = prefs.getFloat("axis_margin", config.axisDivergenceMarginDeg);
  config.wifiSsid = prefs.getString("wifi_ssid", config.wifiSsid);
  config.wifiPassword = prefs.getString("wifi_pass", config.wifiPassword);
  config.networkAutoStart = prefs.getBool("net_auto", config.networkAutoStart);
  config.networkStartDelayMs = prefs.getUInt("net_delay", config.networkStartDelayMs);
  config.mqttHost = prefs.getString("mqtt_host", config.mqttHost);
  config.mqttPort = prefs.getUShort("mqtt_port", config.mqttPort);
  config.mqttUsername = prefs.getString("mqtt_user", config.mqttUsername);
  config.mqttPassword = prefs.getString("mqtt_pass", config.mqttPassword);
  config.mqttRoot = prefs.getString("mqtt_root", config.mqttRoot);
  config.otaCommandCenterControlled = prefs.getBool("ota_cc", config.otaCommandCenterControlled);
  config.otaAutoCheckEnabled = prefs.getBool("ota_auto", config.otaAutoCheckEnabled);
  config.otaChannel = prefs.getString("ota_channel", config.otaChannel);
  config.otaDesiredBuild = prefs.getUInt("ota_build", config.otaDesiredBuild);
  config.otaPublicManifestUrl = prefs.getString("ota_pub_url", config.otaPublicManifestUrl);
  config.otaLocalMirrorUrl = prefs.getString("ota_mir_url", config.otaLocalMirrorUrl);
  config.otaCheckIntervalS = prefs.getUInt("ota_int_s", config.otaCheckIntervalS);
  config.otaApplyOnlyInSafeState = prefs.getBool("ota_safe", config.otaApplyOnlyInSafeState);
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
  ok &= prefs.putString("group", config.group) >= 0;
  ok &= prefs.putInt("floor", config.floor) > 0;
  ok &= prefs.putString("side", config.side) >= 0;
  ok &= prefs.putString("frame_id", config.frameId) >= 0;
  ok &= prefs.putString("frame_unit", config.frameUnit) >= 0;
  ok &= prefs.putString("origin", config.frameOrigin) >= 0;
  ok &= prefs.putString("x_axis", config.frameXAxis) >= 0;
  ok &= prefs.putString("y_axis", config.frameYAxis) >= 0;
  ok &= prefs.putString("z_axis", config.frameZAxis) >= 0;
  ok &= prefs.putString("target_unit", config.mqttTargetUnit) >= 0;
  ok &= prefs.putFloat("x_cm", config.xCm) > 0;
  ok &= prefs.putFloat("y_cm", config.yCm) > 0;
  ok &= prefs.putFloat("z_cm", config.zCm) > 0;
  ok &= prefs.putFloat("target_z", config.defaultTargetZCm) > 0;
  ok &= prefs.putString("yaw_zero_ref", config.yawZeroReference) >= 0;
  ok &= prefs.putFloat("yaw_off", config.yawOffsetDeg) > 0;
  ok &= prefs.putFloat("pitch_off", config.pitchOffsetDeg) > 0;
  ok &= prefs.putFloat("yaw_axis", config.yawAxisOffsetDeg) > 0;
  ok &= prefs.putFloat("pitch_axis", config.pitchAxisOffsetDeg) > 0;
  ok &= prefs.putFloat("home_yaw", config.homeYawDeg) > 0;
  ok &= prefs.putFloat("home_pitch", config.homePitchDeg) > 0;
  ok &= prefs.putFloat("yaw_min_deg", config.yawMinDeg) > 0;
  ok &= prefs.putFloat("yaw_max_deg", config.yawMaxDeg) > 0;
  ok &= prefs.putFloat("pit_min_deg", config.pitchMinDeg) > 0;
  ok &= prefs.putFloat("pit_max_deg", config.pitchMaxDeg) > 0;
  ok &= prefs.putBool("fire_hw", config.fireHardwareEnabled) > 0;
  ok &= prefs.putUShort("fire_run", config.fireEscRunUs) > 0;
  ok &= prefs.putUShort("fire_stop", config.fireEscStopUs) > 0;
  ok &= prefs.putUInt("fire_def", config.fireDefaultHoldMs) > 0;
  ok &= prefs.putUInt("fire_min", config.fireMinHoldMs) > 0;
  ok &= prefs.putUInt("fire_max", config.fireMaxHoldMs) > 0;
  ok &= prefs.putUInt("fire_step", config.fireRelayStepDelayMs) > 0;
  ok &= prefs.putFloat("dead_pitch", config.deadPitchDeg) > 0;
  ok &= prefs.putFloat("idle_ymin", config.idleYawMinDeg) > 0;
  ok &= prefs.putFloat("idle_ymax", config.idleYawMaxDeg) > 0;
  ok &= prefs.putFloat("idle_yspd", config.idleYawSpeedDegS) > 0;
  ok &= prefs.putFloat("idle_pmin", config.idlePitchMinDeg) > 0;
  ok &= prefs.putFloat("idle_pmax", config.idlePitchMaxDeg) > 0;
  ok &= prefs.putFloat("idle_pspd", config.idlePitchSpeedDegS) > 0;
  ok &= prefs.putUShort("yaw_stop", config.yawStopUs) > 0;
  ok &= prefs.putUShort("pitch_stop", config.pitchStopUs) > 0;
  ok &= prefs.putUShort("srv_delta", config.servoMaxDeltaUs) > 0;
  ok &= prefs.putUShort("yaw_max", config.yawMaxDeltaUs) > 0;
  ok &= prefs.putUShort("pit_max", config.pitchMaxDeltaUs) > 0;
  ok &= prefs.putUShort("yaw_min_drv", config.yawMinDriveUs) > 0;
  ok &= prefs.putUShort("pit_min_drv", config.pitchMinDriveUs) > 0;
  ok &= prefs.putUShort("srv_settle", config.servoAttachSettleMs) > 0;
  ok &= prefs.putUShort("axis_cool", config.axisSwitchCooldownMs) > 0;
  ok &= prefs.putUShort("axis_guard", config.axisDivergenceGuardMs) > 0;
  ok &= prefs.putFloat("axis_margin", config.axisDivergenceMarginDeg) > 0;
  ok &= prefs.putString("wifi_ssid", config.wifiSsid) >= 0;
  ok &= prefs.putString("wifi_pass", config.wifiPassword) >= 0;
  ok &= prefs.putBool("net_auto", config.networkAutoStart) > 0;
  ok &= prefs.putUInt("net_delay", config.networkStartDelayMs) > 0;
  ok &= prefs.putString("mqtt_host", config.mqttHost) >= 0;
  ok &= prefs.putUShort("mqtt_port", config.mqttPort) > 0;
  ok &= prefs.putString("mqtt_user", config.mqttUsername) >= 0;
  ok &= prefs.putString("mqtt_pass", config.mqttPassword) >= 0;
  ok &= prefs.putString("mqtt_root", config.mqttRoot) >= 0;
  ok &= prefs.putBool("ota_cc", config.otaCommandCenterControlled) > 0;
  ok &= prefs.putBool("ota_auto", config.otaAutoCheckEnabled) > 0;
  ok &= prefs.putString("ota_channel", config.otaChannel) >= 0;
  ok &= prefs.putUInt("ota_build", config.otaDesiredBuild) > 0;
  ok &= prefs.putString("ota_pub_url", config.otaPublicManifestUrl) >= 0;
  ok &= prefs.putString("ota_mir_url", config.otaLocalMirrorUrl) >= 0;
  ok &= prefs.putUInt("ota_int_s", config.otaCheckIntervalS) > 0;
  ok &= prefs.putBool("ota_safe", config.otaApplyOnlyInSafeState) > 0;
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
