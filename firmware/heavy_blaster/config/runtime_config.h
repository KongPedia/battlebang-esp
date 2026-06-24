#pragma once

#include <Arduino.h>

#include "heavy_blaster/app/identity.h"
#include "heavy_blaster/build_config.h"

namespace battlebang {
namespace heavy_blaster {

struct UnlockConfig {
  uint8_t requiredSlots = ::heavy_blaster::DEFAULT_REQUIRED_SLOTS;
  uint32_t preEffectMs = ::heavy_blaster::PRE_UNLOCK_EFFECT_MS;
  uint32_t fadeOutMs = ::heavy_blaster::FADE_OUT_MS;
  uint16_t blinkBpm = ::heavy_blaster::BLINK_BPM;
  bool relayOnAfterAllSlots = true;
};

struct LedConfig {
  uint8_t slotCount = ::heavy_blaster::kSlotCount;
  uint8_t matrixWidth = ::heavy_blaster::MATRIX_WIDTH;
  uint8_t matrixHeight = ::heavy_blaster::MATRIX_HEIGHT;
  uint16_t matrixNumLeds = ::heavy_blaster::MATRIX_NUM_LEDS;
  uint8_t brightness = ::heavy_blaster::LED_BRIGHTNESS;
  uint16_t maxMa = ::heavy_blaster::LED_MAX_MA;
  uint32_t activeColor = ::heavy_blaster::SLOT_ACTIVE_YELLOW;
};

struct HardwareProfileConfig {
  uint8_t slotCount = ::heavy_blaster::kSlotCount;
  int8_t matrixPins[::heavy_blaster::kSlotCount] = {23, 22, 21, 19};
  int8_t relayPin = ::heavy_blaster::RELAY_PIN;
  bool relayActiveLow = ::heavy_blaster::RELAY_ACTIVE_LOW;
  String relayProfile = "single_channel_active_high";
  String ledType = ::heavy_blaster::LED_TYPE_NAME;
  String colorOrder = ::heavy_blaster::COLOR_ORDER_NAME;
};

struct RuntimeConfig {
  uint16_t schema = 1;
  uint32_t configVersion = 0;
  bool configured = false;

  String deviceId;
  String blasterId;
  String displayName;
  String deviceMac;
  String group;
  String stageId;
  String location;
  bool debugAllowLocalControl = false;

  UnlockConfig unlock;
  LedConfig led;
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
  String otaChannel = "heavy-blaster";
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
bool visualConfigChanged(const RuntimeConfig& before, const RuntimeConfig& after);
bool hardwareProfileChanged(const RuntimeConfig& before, const RuntimeConfig& after);

class RuntimeConfigStore {
 public:
  bool load(RuntimeConfig& config);
  bool save(const RuntimeConfig& config);
  bool clear();
};

}  // namespace heavy_blaster
}  // namespace battlebang
