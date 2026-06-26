#include <Arduino.h>
#include <ArduinoJson.h>
#include <Esp.h>
#include <WiFi.h>
#include <bb_esp_ota/http_ota.h>
#include <bb_esp_ota/ota_manifest.h>
#include <bb_esp_ota/reboot_marker.h>
#include "BluetoothSerial.h"

#include "go2_nixo/app/firmware_info.h"
#include "go2_nixo/mqtt/hit_mqtt_client.h"
#include "go2_nixo/build_config.h"
#include "go2_nixo/nixo/nixo_fire_client.h"
#include "go2_nixo/display/bar_display.h"
#include "go2_nixo/display/ring_display.h"

using namespace go2;

// Integrated Go2 hit/LED + Nixo fallback firmware:
// - This ESP samples piezo AO (ADC), accepts local hits immediately, owns
//   hp_remaining/down state, and renders the 84-LED HP bar without waiting for
//   Command Center per-hit ring_display round trips.
// - Command Center ingests hit_event/status metadata for dashboard/world state.
// - Nixo fire is handled on the same ESP through MQTT relay commands. The original
//   ring LED is reserved for Nixo ready/firing/cooldown state, never HP state.
// - Piezo D0 is not used for hit judgment; it is read only for debug logs.

BluetoothSerial SerialBT;
HardwareSerial& JetsonSerial = Serial2;

BarDisplay barDisplay;
RingDisplay ringDisplay;
HitMqttClient hitMqtt;
NixoFireClient nixoFire;
RuntimeConfig runtimeConfig;

uint32_t hitSequence = 0;

struct LocalHitState {
  uint16_t maxHits = MAX_HITS;
  uint16_t hpRemaining = MAX_HITS;
  uint16_t acceptedHitCount = 0;
  bool down = false;
  uint32_t lastHitSequence = 0;
};

LocalHitState localHitState;
String jetsonCommandLine;
String usbCommandLine;
String btCommandLine;
String pendingMqttConfigJson;
String pendingMqttOtaJson;
bool pendingMqttConfig = false;
bool pendingMqttOta = false;
bool postOtaReboot = false;
uint32_t lastDeviceStatusMs = 0;
uint32_t lastAutoOtaCheckMs = 0;
bool lastMqttConnected = false;
bool hasSeenMqttConnection = false;
uint32_t lastJetsonHpTxMs = 0;

constexpr size_t COMMAND_LINE_MAX = 2048;
constexpr uint32_t DEVICE_STATUS_PERIOD_MS = 5000;
constexpr uint32_t JETSON_HP_TX_PERIOD_MS = 100;
constexpr const char* OTA_REBOOT_NAMESPACE = "bb_go2_nixo";
constexpr const char* OTA_REBOOT_KEY = "ota_reboot";

struct AnalogPiezoState {
  bool armed = true;
  bool captureActive = false;
  uint32_t captureStartedMs = 0;
  int capturePeakRaw = 0;
  uint32_t lastCandidateMs = 0;
  uint32_t quietStartedMs = 0;

  uint32_t lastDebugMs = 0;
  int lastRaw = 0;
  int minRaw = 4095;
  int maxRaw = 0;
  uint32_t sumRaw = 0;
  uint32_t sampleCount = 0;
};

AnalogPiezoState analogPiezo;

static void onBarDisplayUpdate(const BarDisplayUpdate& update);
static void publishDeviceStatusIfConnected(const char* reason);

static bool piezoAoEnabled() {
  return PIEZO_AO_PIN >= 0;
}

static bool piezoDoDebugEnabled() {
  return PIEZO_DO_PIN >= 0;
}

static int readPiezoAoRaw() {
  if (!piezoAoEnabled()) return -1;
  return analogRead(PIEZO_AO_PIN);
}

static int readPiezoDoLevel() {
  if (!piezoDoDebugEnabled()) return -1;
  return digitalRead(PIEZO_DO_PIN);
}

static void resetAnalogStats(int raw) {
  if (raw < 0) raw = 0;
  analogPiezo.lastRaw = raw;
  analogPiezo.minRaw = raw;
  analogPiezo.maxRaw = raw;
  analogPiezo.sumRaw = 0;
  analogPiezo.sampleCount = 0;
}

static void resetAnalogPiezoState() {
  analogPiezo.armed = true;
  analogPiezo.captureActive = false;
  analogPiezo.captureStartedMs = 0;
  analogPiezo.capturePeakRaw = 0;
  analogPiezo.lastCandidateMs = 0;
  analogPiezo.quietStartedMs = 0;
  resetAnalogStats(readPiezoAoRaw());
}

static void resetLocalHitState() {
  localHitState.maxHits = runtimeConfig.hit.maxHits > 0 ? runtimeConfig.hit.maxHits : MAX_HITS;
  localHitState.hpRemaining = localHitState.maxHits;
  localHitState.acceptedHitCount = 0;
  localHitState.down = false;
  localHitState.lastHitSequence = 0;
  nixoFire.setFireInhibited(false);
  barDisplay.resetLocalHpState(localHitState.maxHits);
}

static float localHpFillRatio() {
  if (localHitState.maxHits == 0) return 1.0f;
  return constrain(static_cast<float>(localHitState.hpRemaining) / static_cast<float>(localHitState.maxHits), 0.0f, 1.0f);
}

static void syncLocalHitStateWithRuntimeConfig() {
  uint16_t nextMaxHits = runtimeConfig.hit.maxHits > 0 ? runtimeConfig.hit.maxHits : MAX_HITS;
  if (localHitState.maxHits == nextMaxHits) {
    barDisplay.setLocalHpState(localHitState.hpRemaining, localHitState.maxHits, localHitState.down, 0, millis());
    nixoFire.setFireInhibited(localHitState.down);
    return;
  }

  localHitState.maxHits = nextMaxHits;
  if (localHitState.acceptedHitCount == 0) {
    localHitState.hpRemaining = nextMaxHits;
  } else {
    localHitState.hpRemaining = localHitState.acceptedHitCount >= nextMaxHits
                                    ? 0
                                    : nextMaxHits - localHitState.acceptedHitCount;
  }
  localHitState.down = localHitState.hpRemaining == 0;
  nixoFire.setFireInhibited(localHitState.down);
  barDisplay.setLocalHpState(localHitState.hpRemaining, localHitState.maxHits, localHitState.down, 0, millis());
}

static bool applyLocalHit(uint32_t sequence, uint32_t now) {
  localHitState.maxHits = runtimeConfig.hit.maxHits > 0 ? runtimeConfig.hit.maxHits : MAX_HITS;
  if (localHitState.hpRemaining > localHitState.maxHits) localHitState.hpRemaining = localHitState.maxHits;
  if (!localHitState.down && localHitState.hpRemaining > 0) {
    localHitState.hpRemaining--;
    localHitState.acceptedHitCount++;
    localHitState.down = localHitState.hpRemaining == 0;
  }
  localHitState.lastHitSequence = sequence;
  nixoFire.setFireInhibited(localHitState.down);
  barDisplay.setLocalHpState(localHitState.hpRemaining,
                             localHitState.maxHits,
                             localHitState.down,
                             runtimeConfig.hit.hitFlashMs,
                             now);
  return localHitState.down;
}

static void beginAnalogPiezo() {
  if (!piezoAoEnabled()) {
    Serial.println("[PIEZO AO] disabled: PIEZO_AO_PIN < 0");
    return;
  }

  pinMode(PIEZO_AO_PIN, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(PIEZO_AO_PIN, ADC_11db);

  if (piezoDoDebugEnabled()) {
    pinMode(PIEZO_DO_PIN, INPUT_PULLDOWN);
  }

  resetAnalogPiezoState();
  Serial.printf("[PIEZO AO] ADC threshold mode pin=%d threshold=%d rearm_raw=%d capture_window_ms=%lu cooldown_ms=%lu debug_period_ms=%lu initial_raw=%d do_pin=%d do=%d\n",
                PIEZO_AO_PIN,
                runtimeConfig.hit.piezoAoThresholdRaw,
                runtimeConfig.hit.piezoAoRearmRaw,
                (unsigned long)runtimeConfig.hit.piezoAoCaptureWindowMs,
                (unsigned long)runtimeConfig.hit.hitCooldownMs,
                (unsigned long)runtimeConfig.hit.piezoAoDebugPeriodMs,
                analogPiezo.lastRaw,
                PIEZO_DO_PIN,
                readPiezoDoLevel());
}

static void publishAdcHitEvent(int targetId, int peakRaw, int thresholdRaw, uint32_t eventTsMs) {
  if (localHitState.down || localHitState.hpRemaining == 0) {
    Serial.printf("[PIEZO AO] ignored after down target=%d peak=%d threshold=%d hp=%u/%u ts_ms=%lu mqtt_connected=%s queue=%u\n",
                  targetId,
                  peakRaw,
                  thresholdRaw,
                  localHitState.hpRemaining,
                  localHitState.maxHits,
                  (unsigned long)eventTsMs,
                  hitMqtt.connected() ? "true" : "false",
                  hitMqtt.offlineQueueCount());
    if (SerialBT.hasClient()) {
      SerialBT.printf("[PIEZO AO] ignored after down peak=%d threshold=%d hp=%u/%u\n",
                      peakRaw,
                      thresholdRaw,
                      localHitState.hpRemaining,
                      localHitState.maxHits);
    }
    publishDeviceStatusIfConnected("local_hit_ignored_down");
    return;
  }

  uint32_t sequence = ++hitSequence;
  const bool downNow = applyLocalHit(sequence, millis());

  Serial.printf("[PIEZO AO] local hit_event seq=%lu target=%d peak=%d threshold=%d hp=%u/%u down=%s ts_ms=%lu mqtt_connected=%s queue=%u\n",
                (unsigned long)sequence,
                targetId,
                peakRaw,
                thresholdRaw,
                localHitState.hpRemaining,
                localHitState.maxHits,
                downNow ? "true" : "false",
                (unsigned long)eventTsMs,
                hitMqtt.connected() ? "true" : "false",
                hitMqtt.offlineQueueCount());

  if (SerialBT.hasClient()) {
    SerialBT.printf("[PIEZO AO] local hit seq=%lu peak=%d threshold=%d hp=%u/%u down=%s\n",
                    (unsigned long)sequence,
                    peakRaw,
                    thresholdRaw,
                    localHitState.hpRemaining,
                    localHitState.maxHits,
                    downNow ? "true" : "false");
  }

  if (hitMqtt.offlineQueueCount() > 0) {
    hitMqtt.queueHitEvent(targetId,
                          sequence,
                          eventTsMs,
                          peakRaw,
                          thresholdRaw,
                          localHitState.acceptedHitCount,
                          localHitState.hpRemaining,
                          localHitState.maxHits,
                          localHitState.down);
    publishDeviceStatusIfConnected(localHitState.down ? "local_hit_down" : "local_hit");
    return;
  }
  if (!hitMqtt.publishHitEvent(targetId,
                               sequence,
                               eventTsMs,
                               false,
                               0,
                               0,
                               peakRaw,
                               thresholdRaw,
                               localHitState.acceptedHitCount,
                               localHitState.hpRemaining,
                               localHitState.maxHits,
                               localHitState.down)) {
    hitMqtt.queueHitEvent(targetId,
                          sequence,
                          eventTsMs,
                          peakRaw,
                          thresholdRaw,
                          localHitState.acceptedHitCount,
                          localHitState.hpRemaining,
                          localHitState.maxHits,
                          localHitState.down);
  }
  publishDeviceStatusIfConnected(localHitState.down ? "local_hit_down" : "local_hit");
}

static void updateAnalogDebugStats(int raw) {
  analogPiezo.lastRaw = raw;
  analogPiezo.minRaw = min(analogPiezo.minRaw, raw);
  analogPiezo.maxRaw = max(analogPiezo.maxRaw, raw);
  analogPiezo.sumRaw += (uint32_t)raw;
  analogPiezo.sampleCount++;
}

static void printAnalogDebugTick(uint32_t now) {
  if (now - analogPiezo.lastDebugMs < runtimeConfig.hit.piezoAoDebugPeriodMs) return;
  analogPiezo.lastDebugMs = now;

  uint32_t avg = analogPiezo.sampleCount > 0 ? analogPiezo.sumRaw / analogPiezo.sampleCount : (uint32_t)analogPiezo.lastRaw;
  Serial.printf("[PIEZO AO] ms=%lu raw=%d min=%d max=%d avg=%lu threshold=%d rearm=%d armed=%s capturing=%s do_pin=%d do=%d mqtt=%s queue=%u\n",
                (unsigned long)now,
                analogPiezo.lastRaw,
                analogPiezo.minRaw,
                analogPiezo.maxRaw,
                (unsigned long)avg,
                runtimeConfig.hit.piezoAoThresholdRaw,
                runtimeConfig.hit.piezoAoRearmRaw,
                analogPiezo.armed ? "true" : "false",
                analogPiezo.captureActive ? "true" : "false",
                PIEZO_DO_PIN,
                readPiezoDoLevel(),
                hitMqtt.connected() ? "connected" : "disconnected",
                hitMqtt.offlineQueueCount());

  resetAnalogStats(analogPiezo.lastRaw);
}

static void rearmAnalogPiezoWhenQuiet(uint32_t now, int raw) {
  if (analogPiezo.armed) return;
  if (analogPiezo.captureActive) return;
  if (now - analogPiezo.lastCandidateMs < runtimeConfig.hit.hitCooldownMs) return;

  if (raw > runtimeConfig.hit.piezoAoRearmRaw) {
    analogPiezo.quietStartedMs = 0;
    return;
  }

  if (analogPiezo.quietStartedMs == 0) {
    analogPiezo.quietStartedMs = now;
    return;
  }

  if (now - analogPiezo.quietStartedMs >= runtimeConfig.hit.piezoAoRearmStableMs) {
    analogPiezo.armed = true;
    analogPiezo.quietStartedMs = 0;
    Serial.printf("[PIEZO AO] rearmed raw=%d quiet_ms=%lu\n",
                  raw,
                  (unsigned long)runtimeConfig.hit.piezoAoRearmStableMs);
  }
}

static void pollAnalogPiezo(uint32_t now) {
  if (!piezoAoEnabled()) return;

  int raw = readPiezoAoRaw();
  updateAnalogDebugStats(raw);

  if (analogPiezo.captureActive) {
    analogPiezo.capturePeakRaw = max(analogPiezo.capturePeakRaw, raw);
    if (now - analogPiezo.captureStartedMs >= runtimeConfig.hit.piezoAoCaptureWindowMs) {
      uint32_t eventTsMs = analogPiezo.captureStartedMs;
      analogPiezo.captureActive = false;
      analogPiezo.lastCandidateMs = now;
      analogPiezo.quietStartedMs = 0;
      publishAdcHitEvent(1,
                             analogPiezo.capturePeakRaw,
                             runtimeConfig.hit.piezoAoThresholdRaw,
                             eventTsMs);
    }
    printAnalogDebugTick(now);
    return;
  }

  rearmAnalogPiezoWhenQuiet(now, raw);

  if (analogPiezo.armed && raw >= runtimeConfig.hit.piezoAoThresholdRaw) {
    analogPiezo.armed = false;
    analogPiezo.captureActive = true;
    analogPiezo.captureStartedMs = now;
    analogPiezo.capturePeakRaw = raw;
    analogPiezo.quietStartedMs = 0;
    Serial.printf("[PIEZO AO] threshold crossed raw=%d threshold=%d ms=%lu do=%d\n",
                  raw,
                  runtimeConfig.hit.piezoAoThresholdRaw,
                  (unsigned long)now,
                  readPiezoDoLevel());
  }

  printAnalogDebugTick(now);
}

static char normalizeCommandChar(char c) {
  if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
  return c;
}

static bool isIgnoredCommandChar(char c) {
  return c == '\r' || c == '\n' || c == ' ' || c == '\t';
}


battlebang::esp::app::FirmwareIdentity firmwareIdentity() {
  battlebang::esp::app::FirmwareIdentity id;
  id.app = BB_GO2_NIXO_APP_NAME;
  id.hardware = BB_GO2_NIXO_HARDWARE;
  id.version = BB_GO2_NIXO_VERSION;
  id.build = BB_GO2_NIXO_BUILD;
  id.gitSha = BB_GO2_NIXO_GIT_SHA;
  id.releaseRepo = BB_GO2_NIXO_RELEASE_REPO;
  id.latestManifestUrl = BB_GO2_NIXO_LATEST_MANIFEST_URL;
  return id;
}

String configuredOtaPollUrl() {
  if (runtimeConfig.common.otaLocalMirrorUrl.length() > 0) return runtimeConfig.common.otaLocalMirrorUrl;
  return runtimeConfig.common.otaPublicManifestUrl.length() > 0
             ? runtimeConfig.common.otaPublicManifestUrl
             : String(BB_GO2_NIXO_LATEST_MANIFEST_URL);
}

bool commandCenterApprovesPolledOta(const battlebang::esp::ota::OtaManifest& manifest, String& reason) {
  if (runtimeConfig.common.otaChannel.length() > 0 && manifest.channel.length() > 0 &&
      manifest.channel != runtimeConfig.common.otaChannel) {
    reason = String("channel mismatch expected=") + runtimeConfig.common.otaChannel + " got=" + manifest.channel;
    return false;
  }
  if (!runtimeConfig.common.otaCommandCenterControlled) {
    reason = "command-center approval not required by config";
    return true;
  }
  if (runtimeConfig.common.otaDesiredBuild == 0) {
    reason = "command-center desired_build is 0";
    return false;
  }
  if (manifest.build != runtimeConfig.common.otaDesiredBuild) {
    reason = String("manifest build ") + String(manifest.build) +
             " does not match command-center desired_build " + String(runtimeConfig.common.otaDesiredBuild);
    return false;
  }
  reason = "command-center desired_build approved";
  return true;
}

String copyPayloadToString(const byte* payload, unsigned int length) {
  String out;
  out.reserve(length + 1);
  for (unsigned int i = 0; i < length; ++i) out += static_cast<char>(payload[i]);
  return out;
}

void onMqttConfigMessage(const char*, const byte* payload, unsigned int length) {
  if (length > COMMAND_LINE_MAX) {
    Serial.println("[MQTT] config payload too large; dropped");
    return;
  }
  pendingMqttConfigJson = copyPayloadToString(payload, length);
  pendingMqttConfig = true;
}

void onMqttOtaMessage(const char*, const byte* payload, unsigned int length) {
  if (length > COMMAND_LINE_MAX) {
    Serial.println("[MQTT] OTA manifest payload too large; dropped");
    return;
  }
  pendingMqttOtaJson = copyPayloadToString(payload, length);
  pendingMqttOta = true;
}

static void resetAll(const char* source = "serial") {
  resetAnalogPiezoState();
  hitMqtt.clearOfflineQueue();
  nixoFire.stopFire("reset");
  resetLocalHitState();
  barDisplay.clearRemoteDisplay();
  barDisplay.markDirty();
  ringDisplay.clearCooldown();
  ringDisplay.markDirty();
  Serial.printf("[RESET] source=%s ADC/local HP/display state cleared\n", source);
  if (SerialBT.hasClient()) SerialBT.println("[RESET] ADC/local HP/display state cleared");
  publishDeviceStatusIfConnected("reset");
}

static void handleCommandChar(char c, const char* source) {
  c = normalizeCommandChar(c);
  if (c == CMD_RESET_HIT_DISPLAY || c == 'r') {
    resetAll(source);
    return;
  }
  if (c == '1' || c == 'f') {
    bool started = nixoFire.startFire(runtimeConfig.nixo.fireDefaultDurationMs, source);
    Serial.printf("[CMD] fire %s source=%s\n", started ? "started" : "ignored", source);
    if (SerialBT.hasClient()) {
      SerialBT.printf("[CMD] fire %s source=%s\n", started ? "started" : "ignored", source);
    }
  }
}

static void replyToSource(const char* source, const String& line) {
  Serial.println(line);
  if (String(source) == "bt" && SerialBT.hasClient()) SerialBT.println(line);
  if (String(source) == "jetson") JetsonSerial.println(line);
}

static void sendHpToJetson(uint32_t now, bool force = false) {
  if (!force && now - lastJetsonHpTxMs < JETSON_HP_TX_PERIOD_MS) return;
  lastJetsonHpTxMs = now;
  JetsonSerial.println(localHitState.hpRemaining);
}

static void addHitTuningStatus(JsonObject doc) {
  doc["hit_cooldown_ms"] = runtimeConfig.hit.hitCooldownMs;
  doc["offline_queue_capacity"] = runtimeConfig.hit.offlineQueueCapacity;
  doc["offline_queue_flush_interval_ms"] = runtimeConfig.hit.offlineQueueFlushIntervalMs;
  doc["led_brightness"] = runtimeConfig.hit.ledBrightness;
  doc["ring_brightness"] = runtimeConfig.hit.ringBrightness;
  doc["piezo_ao_threshold_raw"] = runtimeConfig.hit.piezoAoThresholdRaw;
  doc["piezo_ao_rearm_raw"] = runtimeConfig.hit.piezoAoRearmRaw;
  doc["piezo_ao_capture_window_ms"] = runtimeConfig.hit.piezoAoCaptureWindowMs;
  doc["piezo_ao_debug_period_ms"] = runtimeConfig.hit.piezoAoDebugPeriodMs;
  doc["piezo_ao_rearm_stable_ms"] = runtimeConfig.hit.piezoAoRearmStableMs;
  doc["max_hits"] = runtimeConfig.hit.maxHits;
  doc["hit_flash_ms"] = runtimeConfig.hit.hitFlashMs;
}

static void addLocalHitStatus(JsonObject doc) {
  doc["accepted_hit_count"] = localHitState.acceptedHitCount;
  doc["hp_remaining"] = localHitState.hpRemaining;
  doc["max_hits"] = localHitState.maxHits;
  doc["down"] = localHitState.down;
  doc["ring_fill_ratio"] = localHpFillRatio();
  doc["last_hit_sequence"] = localHitState.lastHitSequence;

  JsonObject combat = doc.createNestedObject("combat");
  combat["accepted_hit_count"] = localHitState.acceptedHitCount;
  combat["hp"] = localHitState.hpRemaining;
  combat["hp_current"] = localHitState.hpRemaining;
  combat["max_hp"] = localHitState.maxHits;
  combat["hp_max"] = localHitState.maxHits;
  combat["down"] = localHitState.down;
  combat["ring_fill_ratio"] = localHpFillRatio();
  combat["last_hit_sequence"] = localHitState.lastHitSequence;
}

static void addNixoTuningStatus(JsonObject doc) {
  doc["nixo_fire_default_duration_ms"] = runtimeConfig.nixo.fireDefaultDurationMs;
  doc["nixo_fire_min_duration_ms"] = runtimeConfig.nixo.fireMinDurationMs;
  doc["nixo_fire_max_duration_ms"] = runtimeConfig.nixo.fireMaxDurationMs;
  doc["nixo_fire_cooldown_ms"] = runtimeConfig.nixo.fireCooldownMs;
  doc["nixo_prefire_delay_ms"] = runtimeConfig.nixo.prefireDelayMs;
  doc["nixo_relay_delay1_ms"] = runtimeConfig.nixo.relayDelay1Ms;
}

static void printStatusJson(const char* source, const char* reason) {
  DynamicJsonDocument doc(3072);
  doc["event"] = "status";
  doc["reason"] = reason;
  doc["firmware"] = FIRMWARE_NAME;
  doc["firmware_role"] = FIRMWARE_ROLE;
  doc["configured"] = runtimeConfig.common.configured;
  doc["device_id"] = runtimeConfig.common.deviceId;
  doc["robot_id"] = runtimeConfig.hit.robotId;
  doc["nixo_id"] = runtimeConfig.nixo.nixoId;
  doc["hp_remaining"] = localHitState.hpRemaining;
  doc["max_hits"] = localHitState.maxHits;
  doc["down"] = localHitState.down;
  doc["group"] = runtimeConfig.common.group;
  doc["stage_id"] = runtimeConfig.common.stageId;
  doc["location"] = runtimeConfig.common.location;
  doc["mqtt_configured"] = hitMqtt.configured();
  doc["mqtt_connected"] = hitMqtt.connected();
  doc["nixo_mqtt_configured"] = nixoFire.configured();
  doc["nixo_mqtt_connected"] = nixoFire.connected();
  doc["mqtt_auth_configured"] =
      runtimeConfig.common.mqttUsername.length() > 0 || runtimeConfig.common.mqttPassword.length() > 0;
  doc["mqtt_host"] = runtimeConfig.common.mqttHost;
  doc["mqtt_port"] = runtimeConfig.common.mqttPort;
  doc["mqtt_root"] = runtimeConfig.common.mqttRoot;
  doc["event_topic"] = hitMqtt.eventTopic();
  doc["hp_bar_topic"] = hitMqtt.ringCommandTopic();
  doc["nixo_command_topic"] = nixoFire.commandTopic();
  doc["device_status_topic"] = hitMqtt.deviceStatusTopic();
  doc["device_config_topic"] = hitMqtt.deviceConfigTopic();
  doc["device_ota_topic"] = hitMqtt.deviceOtaTopic();
  doc["ota_supported"] = true;
  doc["ota_command_center_controlled"] = runtimeConfig.common.otaCommandCenterControlled;
  doc["ota_auto_check_enabled"] = runtimeConfig.common.otaAutoCheckEnabled;
  doc["ota_desired_build"] = runtimeConfig.common.otaDesiredBuild;
  doc["ota_channel"] = runtimeConfig.common.otaChannel;
  doc["ota_manifest_url"] = configuredOtaPollUrl();
  doc["post_ota_reboot"] = postOtaReboot;
  doc["offline_queue"] = hitMqtt.offlineQueueCount();
  doc["firing"] = nixoFire.isFiring();
  doc["fire_inhibited"] = nixoFire.fireInhibited();
  doc["cooldown_remaining_ms"] = nixoFire.cooldownRemainingMs(millis());
  doc["hit_sequence"] = hitSequence;
  addHitTuningStatus(doc.as<JsonObject>());
  addLocalHitStatus(doc.as<JsonObject>());
  addNixoTuningStatus(doc.as<JsonObject>());
  doc["uptime_ms"] = millis();
  String out;
  serializeJson(doc, out);
  replyToSource(source, out);
}


static void publishDeviceStatusIfConnected(const char* reason) {
  if (!hitMqtt.connected()) return;
  DynamicJsonDocument doc(3072);
  doc["type"] = "status";
  doc["reason"] = reason;
  doc["firmware_app"] = BB_GO2_NIXO_APP_NAME;
  doc["firmware_version"] = BB_GO2_NIXO_VERSION;
  doc["firmware_build"] = BB_GO2_NIXO_BUILD;
  doc["firmware_hardware"] = BB_GO2_NIXO_HARDWARE;
  doc["git_sha"] = BB_GO2_NIXO_GIT_SHA;
  doc["configured"] = runtimeConfig.common.configured;
  doc["device_id"] = runtimeConfig.common.deviceId;
  doc["robot_id"] = runtimeConfig.hit.robotId;
  doc["nixo_id"] = runtimeConfig.nixo.nixoId;
  doc["hp_remaining"] = localHitState.hpRemaining;
  doc["max_hits"] = localHitState.maxHits;
  doc["down"] = localHitState.down;
  doc["group"] = runtimeConfig.common.group;
  doc["stage_id"] = runtimeConfig.common.stageId;
  doc["location"] = runtimeConfig.common.location;
  doc["wifi"] = WiFi.status() == WL_CONNECTED ? "UP" : "DOWN";
  doc["ip"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String();
  doc["mqtt_connected"] = hitMqtt.connected();
  doc["nixo_mqtt_connected"] = nixoFire.connected();
  doc["mqtt_root"] = runtimeConfig.common.mqttRoot;
  doc["ota_supported"] = true;
  doc["ota_command_center_controlled"] = runtimeConfig.common.otaCommandCenterControlled;
  doc["ota_auto_check_enabled"] = runtimeConfig.common.otaAutoCheckEnabled;
  doc["ota_desired_build"] = runtimeConfig.common.otaDesiredBuild;
  doc["ota_channel"] = runtimeConfig.common.otaChannel;
  doc["ota_manifest_url"] = configuredOtaPollUrl();
  doc["post_ota_reboot"] = postOtaReboot;
  doc["offline_queue"] = hitMqtt.offlineQueueCount();
  doc["firing"] = nixoFire.isFiring();
  doc["fire_inhibited"] = nixoFire.fireInhibited();
  doc["hit_sequence"] = hitSequence;
  addHitTuningStatus(doc.as<JsonObject>());
  addLocalHitStatus(doc.as<JsonObject>());
  addNixoTuningStatus(doc.as<JsonObject>());
  doc["uptime_ms"] = millis();
  String out;
  serializeJson(doc, out);
  if (!hitMqtt.publishDeviceStatus(out.c_str())) {
    Serial.printf("[MQTT] device status publish failed len=%u topic=%s\n",
                  static_cast<unsigned int>(out.length()),
                  hitMqtt.deviceStatusTopic());
  }
}

static void reapplyRuntimeConfig(const char* reason) {
  hitMqtt.clearOfflineQueue();
  nixoFire.stopFire("config-reapply");
  hitMqtt.setManagementHandlers(onMqttConfigMessage, onMqttOtaMessage);
  hitMqtt.begin(runtimeConfig, onBarDisplayUpdate);
  nixoFire.begin(runtimeConfig);
  barDisplay.setBrightness(runtimeConfig.hit.ledBrightness);
  ringDisplay.setBrightness(runtimeConfig.hit.ringBrightness);
  syncLocalHitStateWithRuntimeConfig();
  resetAnalogPiezoState();
  Serial.printf("[CONFIG] runtime config reapplied reason=%s configured=%s robot_id=%s nixo_id=%s broker=%s:%u event_topic=%s nixo_topic=%s\n",
                reason,
                runtimeConfig.common.configured ? "true" : "false",
                runtimeConfig.hit.robotId.c_str(),
                runtimeConfig.nixo.nixoId.c_str(),
                runtimeConfig.common.mqttHost.c_str(),
                runtimeConfig.common.mqttPort,
                hitMqtt.eventTopic(),
                nixoFire.commandTopic());
}


static void checkOtaManifestJson(const String& json, bool requireCommandCenterApproval, const char* source) {
  battlebang::esp::ota::OtaManifest manifest;
  String error;
  if (!battlebang::esp::ota::parseManifestJson(json.c_str(), manifest, error)) {
    Serial.print("[OTA] manifest rejected: ");
    Serial.println(error);
    publishDeviceStatusIfConnected("ota_manifest_rejected");
    return;
  }
  if (requireCommandCenterApproval) {
    String approvalReason;
    if (!commandCenterApprovesPolledOta(manifest, approvalReason)) {
      Serial.print("[OTA] auto poll not approved: ");
      Serial.println(approvalReason);
      publishDeviceStatusIfConnected("ota_not_approved");
      return;
    }
    Serial.print("[OTA] auto poll approved: ");
    Serial.println(approvalReason);
  }
  String reason;
  if (!battlebang::esp::ota::shouldApplyManifest(manifest, firmwareIdentity(), reason)) {
    Serial.print("[OTA] skipped: ");
    Serial.println(reason);
    publishDeviceStatusIfConnected("ota_skipped");
    return;
  }
  if (runtimeConfig.common.otaApplyOnlyInSafeState && nixoFire.isFiring()) {
    Serial.println("[OTA] deferred: Nixo relay is firing");
    publishDeviceStatusIfConnected("ota_deferred");
    return;
  }
  Serial.print("[OTA] accepted from ");
  Serial.print(source);
  Serial.print(' ');
  Serial.println(battlebang::esp::ota::manifestSummary(manifest));
  publishDeviceStatusIfConnected("ota_downloading");
  resetAll("ota");
  battlebang::esp::ota::OtaResult result = battlebang::esp::ota::runHttpOta(manifest);
  Serial.print("[OTA] result ok=");
  Serial.print(result.ok ? "yes" : "no");
  Serial.print(" message=");
  Serial.println(result.message);
  publishDeviceStatusIfConnected(result.ok ? "ota_rebooting" : "ota_failed");
  if (result.ok) {
    battlebang::esp::ota::writeRebootMarker(OTA_REBOOT_NAMESPACE, OTA_REBOOT_KEY, true);
    delay(500);
    ESP.restart();
  }
}

static void checkOtaManifestUrlWithPolicy(const String& url, bool requireCommandCenterApproval, const char* source) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[OTA] Wi-Fi is not connected; cannot fetch manifest");
    publishDeviceStatusIfConnected("ota_no_wifi");
    return;
  }
  String body;
  String error;
  if (!battlebang::esp::ota::fetchHttpText(url, 4096, body, error)) {
    Serial.print("[OTA] manifest fetch failed: ");
    Serial.println(error);
    publishDeviceStatusIfConnected("ota_manifest_fetch_failed");
    return;
  }
  checkOtaManifestJson(body, requireCommandCenterApproval, source);
}

static void checkOtaManifestUrl(const String& url) {
  checkOtaManifestUrlWithPolicy(url, false, "serial");
}

static void applyAndPersistConfig(const String& json, const char* source) {
  String error;
  RuntimeConfig next = runtimeConfig;
  if (!applyRuntimeConfigJson(json.c_str(), next, error)) {
    replyToSource(source, String("{\"event\":\"config_rejected\",\"error\":\"") + error + "\"}");
    return;
  }

  const bool saved = saveRuntimeConfigToNvs(next);
  if (saved) {
    runtimeConfig = next;
    reapplyRuntimeConfig("serial_config");
  }

  DynamicJsonDocument doc(640);
  doc["event"] = saved ? "config_applied" : "config_save_failed";
  doc["saved"] = saved;
  doc["configured"] = runtimeConfig.common.configured;
  doc["config_version"] = runtimeConfig.common.configVersion;
  doc["robot_id"] = runtimeConfig.hit.robotId;
  doc["nixo_id"] = runtimeConfig.nixo.nixoId;
  String out;
  serializeJson(doc, out);
  replyToSource(source, out);
}

static void handleCommandLine(String line, const char* source) {
  line.trim();
  if (line.length() == 0) return;

  String lower = line;
  lower.toLowerCase();

  if (lower == "r" || lower == "reset") {
    resetAll(source);
    return;
  }
  if (lower == "1" || lower == "f" || lower == "fire") {
    handleCommandChar('f', source);
    return;
  }
  if (lower == "s" || lower == "status" || lower == "show-status") {
    printStatusJson(source, "serial");
    return;
  }
  if (lower == "show-config") {
    replyToSource(source, runtimeConfigToJson(runtimeConfig, false));
    return;
  }
  if (lower == "check-ota") {
    checkOtaManifestUrl(configuredOtaPollUrl());
    return;
  }
  if (lower.startsWith("check-ota ")) {
    String url = line.substring(10);
    url.trim();
    checkOtaManifestUrl(url);
    return;
  }
  if (lower == "clear-config") {
    const bool cleared = clearRuntimeConfigNvs();
    runtimeConfig = runtimeConfigFromBuild();
    reapplyRuntimeConfig("clear-config");
    replyToSource(source, String("{\"event\":\"config_cleared\",\"cleared\":") +
                              (cleared ? "true" : "false") + "}");
    return;
  }
  if (lower.startsWith("provision ")) {
    String payload = line.substring(10);
    payload.trim();
    applyAndPersistConfig(payload, source);
    return;
  }
  if (lower.startsWith("config ")) {
    String payload = line.substring(7);
    payload.trim();
    applyAndPersistConfig(payload, source);
    return;
  }
  if (line.length() == 1) {
    handleCommandChar(line[0], source);
    return;
  }
  replyToSource(source, String("{\"event\":\"command_ignored\",\"source\":\"") + source + "\"}");
}

static bool isImmediateCommandChar(char c) {
  c = normalizeCommandChar(c);
  return c == CMD_RESET_HIT_DISPLAY || c == 'r' || c == '1' || c == 'f';
}

static void pollCommandStream(Stream& stream, String& line, const char* source) {
  while (stream.available() > 0) {
    char c = (char)stream.read();
    if (c == '\r') continue;
    if (c == '\n') {
      handleCommandLine(line, source);
      line = "";
      continue;
    }
    if (line.length() == 0 && isIgnoredCommandChar(c)) continue;
    if (line.length() == 0 && isImmediateCommandChar(c) && stream.available() == 0) {
      handleCommandChar(c, source);
      continue;
    }
    if (line.length() >= COMMAND_LINE_MAX) {
      line = "";
      replyToSource(source, "{\"event\":\"command_rejected\",\"error\":\"line too long\"}");
      continue;
    }
    line += c;
  }
}

static void pollCommands() {
  pollCommandStream(JetsonSerial, jetsonCommandLine, "jetson");
  pollCommandStream(Serial, usbCommandLine, "usb");
  pollCommandStream(SerialBT, btCommandLine, "bt");
}


static void processPendingMqttManagement() {
  if (pendingMqttConfig) {
    pendingMqttConfig = false;
    const String payload = pendingMqttConfigJson;
    pendingMqttConfigJson = "";
    applyAndPersistConfig(payload, "mqtt");
    publishDeviceStatusIfConnected("mqtt_config");
  }
  if (pendingMqttOta) {
    pendingMqttOta = false;
    const String payload = pendingMqttOtaJson;
    pendingMqttOtaJson = "";
    checkOtaManifestJson(payload, false, "mqtt");
  }
}

static void publishMqttReconnectStatus(uint32_t now) {
  const bool connected = hitMqtt.connected();
  if (connected && !lastMqttConnected) {
    lastDeviceStatusMs = now;
    publishDeviceStatusIfConnected(hasSeenMqttConnection ? "mqtt_reconnected" : "mqtt_connected");
    hasSeenMqttConnection = true;
  }
  lastMqttConnected = connected;
}

static void publishPeriodicDeviceStatus(uint32_t now) {
  if (DEVICE_STATUS_PERIOD_MS == 0) return;
  if (now - lastDeviceStatusMs < DEVICE_STATUS_PERIOD_MS) return;
  lastDeviceStatusMs = now;
  publishDeviceStatusIfConnected("heartbeat");
}

static void pollConfiguredOta(uint32_t now) {
  if (!runtimeConfig.common.otaAutoCheckEnabled) return;
  if (WiFi.status() != WL_CONNECTED) return;
  if (runtimeConfig.common.otaCommandCenterControlled &&
      (runtimeConfig.common.otaDesiredBuild == 0 || runtimeConfig.common.otaDesiredBuild <= BB_GO2_NIXO_BUILD)) {
    return;
  }
  const uint32_t intervalS = runtimeConfig.common.otaCheckIntervalS < 30 ? 30 : runtimeConfig.common.otaCheckIntervalS;
  const unsigned long intervalMs = static_cast<unsigned long>(intervalS) * 1000UL;
  if (lastAutoOtaCheckMs == 0) {
    lastAutoOtaCheckMs = now;
    return;
  }
  if (now - lastAutoOtaCheckMs < intervalMs) return;
  lastAutoOtaCheckMs = now;
  const String url = configuredOtaPollUrl();
  if (url.length() == 0) {
    Serial.println("[OTA] auto poll skipped: no manifest URL configured");
    publishDeviceStatusIfConnected("ota_poll_no_url");
    return;
  }
  Serial.print("[OTA] auto polling ");
  Serial.println(url);
  checkOtaManifestUrlWithPolicy(url, true, "auto_poll");
}

static void onBarDisplayUpdate(const BarDisplayUpdate& update) {
  uint32_t now = millis();
  if (update.resetHitState) {
    resetAnalogPiezoState();
    hitMqtt.clearOfflineQueue();
    resetLocalHitState();
    barDisplay.clearRemoteDisplay();
    Serial.println("[RESET] MQTT ADC/local HP state reset");
    if (SerialBT.hasClient()) SerialBT.println("[RESET] MQTT ADC/local HP state reset");
    publishDeviceStatusIfConnected("mqtt_reset");
    return;
  }
  if (!update.debugOverride) return;
  barDisplay.setRemoteDisplay(update.fillRatio, update.mode, update.down, update.ttlMs, now);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  JetsonSerial.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  SerialBT.begin(BT_NAME);

  postOtaReboot = battlebang::esp::ota::consumeRebootMarker(OTA_REBOOT_NAMESPACE, OTA_REBOOT_KEY);
  runtimeConfig = runtimeConfigFromNvsOrBuild();

  barDisplay.begin(runtimeConfig.hit.ledBrightness);
  ringDisplay.begin(runtimeConfig.hit.ringBrightness);
  beginAnalogPiezo();
  hitMqtt.setManagementHandlers(onMqttConfigMessage, onMqttOtaMessage);
  hitMqtt.begin(runtimeConfig, onBarDisplayUpdate);
  nixoFire.begin(runtimeConfig);
  resetLocalHitState();

  barDisplay.markDirty();
  ringDisplay.markDirty();
  barDisplay.tick(millis());
  ringDisplay.tick(millis());
  sendHpToJetson(millis(), true);

  Serial.printf("[PIN] UART2 RX=%d TX=%d baud=%lu | HP_BAR_LED=%d count=%d groups=%d leds_per_group=%d | RING_LED=%d count=%d | PIEZO_AO=%d | PIEZO_DO_DEBUG=%d\n",
                UART_RX_PIN,
                UART_TX_PIN,
                (unsigned long)UART_BAUD,
                HP_BAR_LED_PIN,
                HP_BAR_NUM_LEDS,
                HP_BAR_GROUP_COUNT,
                HP_BAR_LEDS_PER_GROUP,
                RING_LED_PIN,
                RING_NUM_LEDS,
                PIEZO_AO_PIN,
                PIEZO_DO_PIN);
  Serial.printf("[ADC] threshold=%d rearm_raw=%d capture_window_ms=%lu cooldown_ms=%lu rearm_stable_ms=%lu\n",
                runtimeConfig.hit.piezoAoThresholdRaw,
                runtimeConfig.hit.piezoAoRearmRaw,
                (unsigned long)runtimeConfig.hit.piezoAoCaptureWindowMs,
                (unsigned long)runtimeConfig.hit.hitCooldownMs,
                (unsigned long)runtimeConfig.hit.piezoAoRearmStableMs);
  Serial.printf("USB/BT/Jetson CMD: '%c'=reset ADC hit/display state, '1'/'f'=Nixo fire.\n",
                CMD_RESET_HIT_DISPLAY);
  Serial.printf("[UART] Jetson HP stream: Serial2.println(hp_remaining) every %lu ms.\n",
                (unsigned long)JETSON_HP_TX_PERIOD_MS);
  Serial.println("USB/BT/Jetson line commands: s/status/show-status, show-config, provision {json}, config {json}, clear-config, check-ota [manifest-url].");
  Serial.print("release_repo=");
  Serial.println(BB_GO2_NIXO_RELEASE_REPO);
  Serial.print("latest_manifest=");
  Serial.println(BB_GO2_NIXO_LATEST_MANIFEST_URL);
  if (postOtaReboot) Serial.println("[OTA] post-OTA reboot marker consumed");
  Serial.print("Bluetooth name: ");
  Serial.println(BT_NAME);
  Serial.printf("[CC] robot_id=%s mqtt=%s broker=%s:%u event_topic=%s ring_topic=%s\n",
                runtimeConfig.common.deviceId.c_str(),
                hitMqtt.configured() ? "enabled" : "disabled",
                runtimeConfig.common.mqttHost.c_str(),
                runtimeConfig.common.mqttPort,
                hitMqtt.eventTopic(),
                hitMqtt.ringCommandTopic());
  printStatusJson("usb", "boot");
  Serial.printf("[NIXO] mqtt=%s nixo_id=%s command_topic=%s relay1=%d relay2=%d relay_on=%d relay_off=%d delay1_ms=%lu fire_default_ms=%lu fire_min_ms=%lu fire_max_ms=%lu cooldown_ms=%lu prefire_ms=%lu\n",
                nixoFire.configured() ? "enabled" : "disabled",
                runtimeConfig.nixo.nixoId.c_str(),
                nixoFire.commandTopic(),
                NIXO_RELAY1_PIN_VALUE,
                NIXO_RELAY2_PIN_VALUE,
                NIXO_RELAY_ON_LEVEL_VALUE,
                NIXO_RELAY_OFF_LEVEL_VALUE,
                (unsigned long)runtimeConfig.nixo.relayDelay1Ms,
                (unsigned long)runtimeConfig.nixo.fireDefaultDurationMs,
                (unsigned long)runtimeConfig.nixo.fireMinDurationMs,
                (unsigned long)runtimeConfig.nixo.fireMaxDurationMs,
                (unsigned long)runtimeConfig.nixo.fireCooldownMs,
                (unsigned long)runtimeConfig.nixo.prefireDelayMs);
}

void loop() {
  uint32_t now = millis();

  // Local hit judgment and HP/ring rendering must keep working even when
  // Command Center/MQTT is offline, so service local paths before network IO.
  pollCommands();
  pollAnalogPiezo(now);
  nixoFire.tick(now);
  ringDisplay.setCooldownState(nixoFire.isFiring(),
                               nixoFire.cooldownRemainingMs(now),
                               nixoFire.cooldownDurationMs(),
                               nixoFire.fireInhibited());
  barDisplay.tick(now);
  ringDisplay.tick(now);
  sendHpToJetson(now);

  hitMqtt.tick(now, barDisplay.remoteDisplayActive());
  publishMqttReconnectStatus(now);
  processPendingMqttManagement();
  publishPeriodicDeviceStatus(now);
  pollConfiguredOta(now);

  delay(1);
}
