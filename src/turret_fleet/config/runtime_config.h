#pragma once

#include <Arduino.h>

namespace battlebang {
namespace turret_fleet {

struct RuntimeConfig {
  uint16_t schema = 2;
  uint32_t configVersion = 0;
  bool configured = false;

  String deviceId;
  String turretId;
  String group;
  int floor = 0;
  String side;

  String frameId = "boss_stage_v1";
  String frameUnit = "cm";
  String frameOrigin = "boss_stage_center_floor";
  String frameXAxis = "stage_right";
  String frameYAxis = "stage_forward";
  String frameZAxis = "up";
  String mqttTargetUnit = "m";

  float xCm = 0.0f;
  float yCm = 0.0f;
  float zCm = 134.5f;
  float defaultTargetZCm = 70.0f;
  String yawZeroReference = "faces_frame_origin";
  float yawOffsetDeg = 0.0f;
  float pitchOffsetDeg = 0.0f;
  // Axis offsets correct the local sensor-derived yaw/pitch zero. Unlike
  // yawOffsetDeg/pitchOffsetDeg, these affect direct aim and closed-loop
  // feedback, so a turret can be centered in software after USB install.
  float yawAxisOffsetDeg = 0.0f;
  float pitchAxisOffsetDeg = 0.0f;
  // Local software home and command envelope. The current 360-degree yaw
  // feedback has a non-linear/deadzone region near the ADC rail, so production
  // tracking stays inside a 150-degree total envelope around the calibrated
  // local origin unless the operator widens it explicitly.
  float homeYawDeg = 0.0f;
  float homePitchDeg = 0.0f;
  float yawMinDeg = -75.0f;
  float yawMaxDeg = 75.0f;
  float pitchMinDeg = -75.0f;
  float pitchMaxDeg = 75.0f;
  float deadPitchDeg = 12.0f;
  float idleYawMinDeg = -15.0f;
  float idleYawMaxDeg = 15.0f;
  float idleYawSpeedDegS = 8.0f;
  float idlePitchMinDeg = -4.0f;
  float idlePitchMaxDeg = 0.0f;
  float idlePitchSpeedDegS = 2.0f;
  // Continuous servos are not guaranteed to stop at exactly 1500us. Keep
  // per-axis stop PWM in NVS so each USB-provisioned turret can be neutralized
  // in software without mechanically trimming the servo.
  uint16_t yawStopUs = 1500;
  uint16_t pitchStopUs = 1500;
  // Closed-loop motion tuning is runtime-configurable because each ESP/servo
  // stack has slightly different stop PWM, supply sag, backlash, and sensor
  // direction. These values are persisted in NVS and can be patched over MQTT.
  uint16_t servoMaxDeltaUs = 220;
  uint16_t yawMaxDeltaUs = 20;
  uint16_t pitchMaxDeltaUs = 20;
  uint16_t yawMinDriveUs = 20;
  uint16_t pitchMinDriveUs = 20;
  uint16_t servoAttachSettleMs = 350;
  uint16_t axisSwitchCooldownMs = 800;
  uint16_t axisDivergenceGuardMs = 3000;
  float axisDivergenceMarginDeg = 20.0f;

  bool fireHardwareEnabled = true;
  uint16_t fireEscRunUs = 1700;
  uint16_t fireEscStopUs = 1000;
  uint32_t fireDefaultHoldMs = 500;
  uint32_t fireMinHoldMs = 100;
  uint32_t fireMaxHoldMs = 60000;
  uint32_t fireRelayStepDelayMs = 250;

  String wifiSsid;
  String wifiPassword;

  bool networkAutoStart = true;
  uint32_t networkStartDelayMs = 10000;

  String mqttHost = "";
  uint16_t mqttPort = 1883;
  String mqttUsername;
  String mqttPassword;
  String mqttRoot = "battlebang";

  bool otaCommandCenterControlled = true;
  bool otaAutoCheckEnabled = false;
  String otaChannel = "boss-demo";
  uint32_t otaDesiredBuild = 0;
  String otaPublicManifestUrl;
  String otaLocalMirrorUrl;
  uint32_t otaCheckIntervalS = 300;
  bool otaApplyOnlyInSafeState = true;
};

RuntimeConfig makeDefaultRuntimeConfig(const String& deviceId);
bool applyRuntimeConfigJson(const char* json, RuntimeConfig& config, String& error);
String runtimeConfigToJson(const RuntimeConfig& config, bool includeSecrets = false);
bool mqttTargetsInMeters(const RuntimeConfig& config);

class RuntimeConfigStore {
 public:
  bool load(RuntimeConfig& config);
  bool save(const RuntimeConfig& config);
  bool clear();
};

}  // namespace turret_fleet
}  // namespace battlebang
