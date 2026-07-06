#pragma once

#include <Arduino.h>

#include "station/app/identity.h"
#include "station/build_config.h"

namespace battlebang {
namespace station {

struct SensorConfig {
  uint16_t hitThreshold = ::station::PIEZO_AO_THRESHOLD;
  uint16_t releaseThreshold = ::station::PIEZO_AO_RELEASE;
  uint32_t hitCooldownMs = ::station::HIT_COOLDOWN_MS;
  uint32_t sampleIntervalMs = ::station::PIEZO_AO_SAMPLE_PERIOD_MS;
  uint32_t settleUs = ::station::PIEZO_AO_SETTLE_US;
};

struct LedConfig {
  uint16_t numLeds = ::station::LED_NUM_LEDS;
  uint8_t brightness = ::station::LED_BRIGHTNESS;
  uint16_t maxMa = ::station::LED_MAX_MA;
  uint32_t waitingColor = ::station::WAITING_COLOR;
  uint32_t capturedColor = ::station::CAPTURED_COLOR;
  uint32_t hitFlashColor = ::station::HIT_FLASH_COLOR;
  uint16_t waitingBreathBpm = ::station::WAITING_BREATH_BPM;
  uint8_t waitingBreathMin = ::station::WAITING_BREATH_MIN;
  uint8_t waitingBreathMax = ::station::WAITING_BREATH_MAX;
};

struct GameplayConfig {
  bool lockAfterHit = true;
  uint32_t autoResetMs = ::station::AUTO_RESET_MS;
  uint32_t heartbeatIntervalMs = ::station::HEARTBEAT_INTERVAL_MS;
};

struct HardwareProfileConfig {
  int16_t piezoPin = ::station::PIEZO_AO_PIN;
  int16_t ledPin = ::station::LED_PIN;
  String ledType = ::station::LED_TYPE_NAME;
  String colorOrder = ::station::COLOR_ORDER_NAME;
};

struct RuntimeConfig {
  uint16_t schema = 1;
  uint32_t configVersion = 0;
  bool configured = false;

  String deviceId;
  String stationId;
  String displayName;
  String deviceMac;
  String group;
  String stageId;
  String location;
  bool debugAllowSimulateHit = false;

  SensorConfig sensor;
  LedConfig led;
  GameplayConfig gameplay;
  HardwareProfileConfig hardware;

  String wifiSsid;
  String wifiPassword;
  bool networkAutoStart = false;
  uint32_t networkStartDelayMs = 0;

  String mqttHost;
  uint16_t mqttPort = 1883;
  String mqttUsername;
  String mqttPassword;
  String mqttRoot = "battlebang";

  bool otaCommandCenterControlled = true;
  bool otaAutoCheckEnabled = false;
  String otaChannel = "station";
  uint32_t otaDesiredBuild = 0;
  String otaPublicManifestUrl;
  String otaLocalMirrorUrl;
  uint32_t otaCheckIntervalS = 300;
  bool otaApplyOnlyInSafeState = true;
};

RuntimeConfig makeDefaultRuntimeConfig(const DeviceIdentity& identity);
bool applyRuntimeConfigJson(const char* json, RuntimeConfig& config, String& error);
String runtimeConfigToJson(const RuntimeConfig& config, bool includeSecrets = false);
bool connectivityConfigChanged(const RuntimeConfig& before, const RuntimeConfig& after);
bool mqttIdentityConfigChanged(const RuntimeConfig& before, const RuntimeConfig& after);
bool sensorConfigChanged(const RuntimeConfig& before, const RuntimeConfig& after);
bool visualConfigChanged(const RuntimeConfig& before, const RuntimeConfig& after);
bool hardwareProfileChanged(const RuntimeConfig& before, const RuntimeConfig& after);

class RuntimeConfigStore {
 public:
  bool load(RuntimeConfig& config);
  bool save(const RuntimeConfig& config);
  bool clear();
};

}  // namespace station
}  // namespace battlebang
