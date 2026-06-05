#pragma once

#include <Arduino.h>

#include "hit_target/app/identity.h"

namespace battlebang {
namespace hit_target {

static constexpr uint8_t kMaxHpPhases = 8;
static constexpr uint16_t kMaxRuntimeLeds = 240;

struct HpConfig {
  uint8_t phaseCount = 3;
  uint16_t hitsPerPhase = 5;
  uint32_t palette[kMaxHpPhases] = {0x009600, 0xBE8200, 0xBE0000, 0, 0, 0, 0, 0};
};

struct VisualConfig {
  uint32_t orbitStepMs = 20;
  uint8_t orbitTailLeds = 6;
  uint32_t cooldownBlinkMs = 60;
  uint32_t hitFlashMs = 50;
  uint32_t damageChipMs = 580;
  uint8_t phaseBackfillGapLeds = 1;
  uint8_t phaseBackfillScale = 96;
  uint32_t hpHitPulseMs = 180;
  uint32_t defeatBlackoutMs = 90;
  uint32_t defeatRainbowMs = 900;
  uint8_t defeatRainbowSpins = 2;
};

struct SensorConfig {
  int8_t piezoDoPin = 27;
  int8_t piezoAoPin = -1;
  uint16_t hitThreshold = 1400;
  uint16_t hitRearmThreshold = 800;
  uint32_t hitCooldownMs = 200;
  uint32_t hitRearmStableMs = 80;
  uint32_t hitRearmCheckMs = 15;
  uint16_t digitalHitMinEdges = 2;
  uint32_t digitalIsrDebounceUs = 5000;
  uint32_t captureWindowMs = 80;
};

struct LedConfig {
  int8_t pin = 18;
  uint16_t numLeds = 60;
  String ledType = "WS2812B";
  String colorOrder = "GRB";
  uint8_t brightness = 80;
  uint16_t maxMa = 1500;
};

struct ResetConfig {
  int8_t buttonPin = 0;
  uint32_t buttonHoldMs = 1200;
};

struct RuntimeConfig {
  uint16_t schema = 1;
  uint32_t configVersion = 0;
  bool configured = false;

  String deviceId;
  String targetId;
  String deviceMac;
  String group;
  String location;

  HpConfig hp;
  VisualConfig visual;
  SensorConfig sensor;
  LedConfig led;
  ResetConfig reset;

  String wifiSsid;
  String wifiPassword;
  bool networkAutoStart = false;
  uint32_t networkStartDelayMs = 0;

  String mqttHost;
  uint16_t mqttPort = 1883;
  String mqttUsername;
  String mqttPassword;
  String mqttRoot = "battlebang";
  bool debugAllowSimulateHit = false;

  bool otaCommandCenterControlled = true;
  bool otaAutoCheckEnabled = false;
  String otaChannel = "hit-target";
  uint32_t otaDesiredBuild = 0;
  String otaPublicManifestUrl;
  String otaLocalMirrorUrl;
  uint32_t otaCheckIntervalS = 300;
  bool otaApplyOnlyInSafeState = true;
};

RuntimeConfig makeDefaultRuntimeConfig(const DeviceIdentity& identity);
bool applyRuntimeConfigJson(const char* json, RuntimeConfig& config, String& error);
String runtimeConfigToJson(const RuntimeConfig& config, bool includeSecrets = false);
uint16_t totalHits(const RuntimeConfig& config);
uint8_t activePhaseCount(const RuntimeConfig& config);
uint32_t phaseColorRgb(const RuntimeConfig& config, uint8_t phaseIndex);
bool gameplayConfigChanged(const RuntimeConfig& before, const RuntimeConfig& after);
bool sensorPinsChanged(const RuntimeConfig& before, const RuntimeConfig& after);
bool ledHardwareChanged(const RuntimeConfig& before, const RuntimeConfig& after);

class RuntimeConfigStore {
 public:
  bool load(RuntimeConfig& config);
  bool save(const RuntimeConfig& config);
  bool clear();
};

}  // namespace hit_target
}  // namespace battlebang
