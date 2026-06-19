#pragma once

#include <Arduino.h>

#include "boss_target/app/identity.h"
#include "boss_target/build_config.h"

namespace battlebang {
namespace boss_target {

static constexpr uint8_t kMaxHpPhases = 8;

struct GameplayConfig {
  uint16_t hpMax = ::boss_target::HP_MAX;
  uint16_t damagePerHit = ::boss_target::DAMAGE_PER_HIT;
  uint8_t phaseCount = ::boss_target::HP_PHASE_COUNT;
  bool startResetsHp = true;
  uint32_t targetDurationMs = ::boss_target::TARGET_DURATION_MS;
  uint32_t hitCooldownMs = ::boss_target::HIT_COOLDOWN_MS;
  uint32_t digitalIsrDebounceUs = ::boss_target::DIGITAL_ISR_DEBOUNCE_US;
};

struct TargetConfig {
  uint8_t count = ::boss_target::DEFAULT_TARGET_COUNT;
  uint16_t ringNumLeds = ::boss_target::RING_NUM_LEDS;
  uint32_t activeColor = ::boss_target::TARGET_ACTIVE_BLUE;
  uint32_t hitFlashColor = ::boss_target::TARGET_HIT_FLASH_WHITE;
};

struct HpBarConfig {
  uint16_t numLeds = ::boss_target::HP_BAR_NUM_LEDS;
  uint8_t brightness = ::boss_target::LED_BRIGHTNESS;
  uint16_t maxMa = ::boss_target::LED_MAX_MA;
  uint32_t palette[kMaxHpPhases] = {::boss_target::HP_GREEN,
                                    ::boss_target::HP_YELLOW,
                                    ::boss_target::HP_RED,
                                    0, 0, 0, 0, 0};
  uint32_t deadBlinkMs = ::boss_target::DEAD_BLINK_MS;
};

struct HardwareProfileConfig {
  uint8_t maxTargets = ::boss_target::kMaxTargets;
  int8_t ringPins[::boss_target::kMaxTargets] = {23, 21, 18, 17};
  int8_t piezoDoPins[::boss_target::kMaxTargets] = {34, 35, 32, 33};
  int8_t hpBarPin = ::boss_target::HP_BAR_PIN;
  String ledType = ::boss_target::LED_TYPE_NAME;
  String colorOrder = ::boss_target::COLOR_ORDER_NAME;
};

struct RuntimeConfig {
  uint16_t schema = 1;
  uint32_t configVersion = 0;
  bool configured = false;

  String deviceId;
  String bossId;
  String targetId;  // Compatibility alias for existing hit-target consumers.
  String displayName;
  String deviceMac;
  String group;
  String location;

  GameplayConfig gameplay;
  TargetConfig target;
  HpBarConfig hpBar;
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
  bool debugAllowSimulateHit = false;

  bool otaCommandCenterControlled = true;
  bool otaAutoCheckEnabled = false;
  String otaChannel = "boss-target";
  uint32_t otaDesiredBuild = 0;
  String otaPublicManifestUrl;
  String otaLocalMirrorUrl;
  uint32_t otaCheckIntervalS = 300;
  bool otaApplyOnlyInSafeState = true;
};

RuntimeConfig makeDefaultRuntimeConfig(const DeviceIdentity& identity);
bool applyRuntimeConfigJson(const char* json, RuntimeConfig& config, String& error);
String runtimeConfigToJson(const RuntimeConfig& config, bool includeSecrets = false);
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

}  // namespace boss_target
}  // namespace battlebang
