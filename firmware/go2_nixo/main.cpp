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
// - This ESP samples three piezo AO channels (ADC), accepts local hits immediately, owns
//   hp_remaining/down state, and renders the 84-LED HP bar without waiting for
//   Command Center per-hit ring_display round trips.
// - Command Center ingests hit_event/status metadata for dashboard/world state.
// - Nixo fire uses Jetson UART as the default path; MQTT remains a secondary command path.
//   The original ring LED is reserved for Nixo ready/firing/inhibited state, never HP state.
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
int lastAcceptedHitTargetId = 3;
String jetsonCommandLine;
String usbCommandLine;
String btCommandLine;
String pendingMqttConfigJson;
String pendingMqttOtaJson;
bool pendingMqttConfig = false;
bool pendingMqttOta = false;
bool pendingHpResetEvent = true;
bool postOtaReboot = false;
uint32_t lastDeviceStatusMs = 0;
uint32_t lastAutoOtaCheckMs = 0;
bool lastMqttConnected = false;
bool hasSeenMqttConnection = false;
bool jetsonFireHoldActive = false;
bool jetsonFireReleaseRequired = false;
uint32_t jetsonFireHoldDeadlineMs = 0;
bool hasPublishedStatusSnapshot = false;
bool hasSentJetsonHpStatus = false;
uint32_t jetsonBootMs = 0;
uint16_t lastPublishedHpRemaining = 0;
uint16_t lastPublishedMaxHits = 0;
uint16_t lastPublishedAcceptedHitCount = 0;
bool lastPublishedDown = false;
uint16_t lastJetsonHpRemaining = 0;
uint16_t lastJetsonMaxHits = 0;
uint16_t lastJetsonAcceptedHitCount = 0;
bool lastJetsonDead = false;
bool lastPublishedNixoFiring = false;
bool lastPublishedFireInhibited = false;
bool lastPublishedJetsonHoldActive = false;
bool lastPublishedJetsonReleaseRequired = false;
const char* lastPublishedNixoState = "";
String lastPublishedNixoActiveSource;
String lastPublishedNixoLastFireSource;
uint32_t fireNetworkQuietUntilMs = 0;

constexpr size_t COMMAND_LINE_MAX = 2048;
constexpr uint32_t DEVICE_STATUS_PERIOD_MS = 5000;
constexpr uint32_t JETSON_FIRE_HOLD_TIMEOUT_MS = 300;
constexpr uint32_t FIRE_NETWORK_QUIET_MS = 250;
constexpr uint32_t JETSON_BOOT_RESET_DELAY_MS = 5000;
constexpr const char* NIXO_TRANSPORT = "jetson_uart+mqtt";
constexpr const char* OTA_REBOOT_NAMESPACE = "bb_go2_nixo";
constexpr const char* OTA_REBOOT_KEY = "ota_reboot";

struct PiezoSample {
  int raw = -1;
  int left = -1;
  int right = -1;
  int front = -1;
  int targetId = 1;
  const char* source = "piezo:left";
};

struct AnalogPiezoState {
  bool armed = true;
  bool captureActive = false;
  uint32_t captureStartedMs = 0;
  int capturePeakRaw = 0;
  int captureTargetId = 1;
  uint32_t lastCandidateMs = 0;
  uint32_t quietStartedMs = 0;

  uint32_t lastDebugMs = 0;
  int lastRaw = 0;
  int lastLeftRaw = 0;
  int lastRightRaw = 0;
  int lastFrontRaw = 0;
  int minRaw = 4095;
  int maxRaw = 0;
  uint32_t sumRaw = 0;
  uint32_t sampleCount = 0;
};

AnalogPiezoState analogPiezo;

static void onBarDisplayUpdate(const BarDisplayUpdate& update);
static void publishDeviceStatusIfConnected(const char* reason);
static bool publishHpResetEventIfConnected(const char* reason);

static bool piezoAoEnabled() {
  return PIEZO_LEFT_AO_PIN >= 0 && PIEZO_RIGHT_AO_PIN >= 0 && PIEZO_FRONT_AO_PIN >= 0;
}

static bool piezoDoDebugEnabled() {
  return PIEZO_DO_PIN >= 0;
}

static PiezoSample readPiezoSample() {
  PiezoSample sample;
  if (!piezoAoEnabled()) return sample;
  sample.left = analogRead(PIEZO_LEFT_AO_PIN);
  sample.right = analogRead(PIEZO_RIGHT_AO_PIN);
  sample.front = analogRead(PIEZO_FRONT_AO_PIN);
  sample.raw = sample.left;
  if (sample.right > sample.raw) {
    sample.raw = sample.right;
    sample.targetId = 2;
    sample.source = "piezo:right";
  }
  if (sample.front > sample.raw) {
    sample.raw = sample.front;
    sample.targetId = 3;
    sample.source = "piezo:front";
  }
  return sample;
}

static int readPiezoDoLevel() {
  if (!piezoDoDebugEnabled()) return -1;
  return digitalRead(PIEZO_DO_PIN);
}

static void resetAnalogStats(const PiezoSample& sample) {
  int raw = sample.raw < 0 ? 0 : sample.raw;
  analogPiezo.lastRaw = raw;
  analogPiezo.lastLeftRaw = sample.left < 0 ? 0 : sample.left;
  analogPiezo.lastRightRaw = sample.right < 0 ? 0 : sample.right;
  analogPiezo.lastFrontRaw = sample.front < 0 ? 0 : sample.front;
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
  analogPiezo.captureTargetId = 1;
  analogPiezo.lastCandidateMs = 0;
  analogPiezo.quietStartedMs = 0;
  resetAnalogStats(readPiezoSample());
}

static void resetLocalHitState() {
  localHitState.maxHits = runtimeConfig.hit.maxHits > 0 ? runtimeConfig.hit.maxHits : MAX_HITS;
  localHitState.hpRemaining = localHitState.maxHits;
  localHitState.acceptedHitCount = 0;
  localHitState.down = false;
  localHitState.lastHitSequence = 0;
  lastAcceptedHitTargetId = 3;
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
    Serial.println("[PIEZO AO] disabled: left/right/front piezo AO pins must be >= 0");
    return;
  }

  pinMode(PIEZO_LEFT_AO_PIN, INPUT);
  pinMode(PIEZO_RIGHT_AO_PIN, INPUT);
  pinMode(PIEZO_FRONT_AO_PIN, INPUT);
  analogReadResolution(12);
#if defined(ADC_11db)
  analogSetPinAttenuation(PIEZO_LEFT_AO_PIN, ADC_11db);
  analogSetPinAttenuation(PIEZO_RIGHT_AO_PIN, ADC_11db);
  analogSetPinAttenuation(PIEZO_FRONT_AO_PIN, ADC_11db);
#endif

  if (piezoDoDebugEnabled()) {
    pinMode(PIEZO_DO_PIN, INPUT_PULLDOWN);
  }

  resetAnalogPiezoState();
  Serial.printf("[PIEZO AO] 3ch ADC threshold mode left=%d right=%d front=%d threshold=%d rearm_raw=%d capture_window_ms=%lu cooldown_ms=%lu debug_period_ms=%lu initial_raw=%d left_raw=%d right_raw=%d front_raw=%d do_pin=%d do=%d\n",
                PIEZO_LEFT_AO_PIN,
                PIEZO_RIGHT_AO_PIN,
                PIEZO_FRONT_AO_PIN,
                runtimeConfig.hit.piezoAoThresholdRaw,
                runtimeConfig.hit.piezoAoRearmRaw,
                (unsigned long)runtimeConfig.hit.piezoAoCaptureWindowMs,
                (unsigned long)runtimeConfig.hit.hitCooldownMs,
                (unsigned long)runtimeConfig.hit.piezoAoDebugPeriodMs,
                analogPiezo.lastRaw,
                analogPiezo.lastLeftRaw,
                analogPiezo.lastRightRaw,
                analogPiezo.lastFrontRaw,
                PIEZO_DO_PIN,
                readPiezoDoLevel());
}

static void publishAdcHitEvent(int targetId, int peakRaw, int thresholdRaw, uint32_t eventTsMs) {
  if (!barDisplay.startupReady(eventTsMs)) {
    Serial.printf("[PIEZO AO] ignored during startup loading target=%d peak=%d threshold=%d ts_ms=%lu\n",
                  targetId,
                  peakRaw,
                  thresholdRaw,
                  (unsigned long)eventTsMs);
    return;
  }
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
  lastAcceptedHitTargetId = targetId;
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

static void updateAnalogDebugStats(const PiezoSample& sample) {
  const int raw = sample.raw < 0 ? 0 : sample.raw;
  analogPiezo.lastRaw = raw;
  analogPiezo.lastLeftRaw = sample.left < 0 ? 0 : sample.left;
  analogPiezo.lastRightRaw = sample.right < 0 ? 0 : sample.right;
  analogPiezo.lastFrontRaw = sample.front < 0 ? 0 : sample.front;
  analogPiezo.minRaw = min(analogPiezo.minRaw, raw);
  analogPiezo.maxRaw = max(analogPiezo.maxRaw, raw);
  analogPiezo.sumRaw += (uint32_t)raw;
  analogPiezo.sampleCount++;
}

static void printAnalogDebugTick(uint32_t now) {
  if (now - analogPiezo.lastDebugMs < runtimeConfig.hit.piezoAoDebugPeriodMs) return;
  analogPiezo.lastDebugMs = now;

  uint32_t avg = analogPiezo.sampleCount > 0 ? analogPiezo.sumRaw / analogPiezo.sampleCount : (uint32_t)analogPiezo.lastRaw;
  Serial.printf("[PIEZO AO] ms=%lu raw=%d left=%d right=%d front=%d min=%d max=%d avg=%lu threshold=%d rearm=%d armed=%s capturing=%s do_pin=%d do=%d mqtt=%s queue=%u\n",
                (unsigned long)now,
                analogPiezo.lastRaw,
                analogPiezo.lastLeftRaw,
                analogPiezo.lastRightRaw,
                analogPiezo.lastFrontRaw,
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

  PiezoSample lastSample;
  lastSample.raw = analogPiezo.lastRaw;
  lastSample.left = analogPiezo.lastLeftRaw;
  lastSample.right = analogPiezo.lastRightRaw;
  lastSample.front = analogPiezo.lastFrontRaw;
  resetAnalogStats(lastSample);
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

  const PiezoSample sample = readPiezoSample();
  const int raw = sample.raw;
  updateAnalogDebugStats(sample);

  if (analogPiezo.captureActive) {
    if (raw > analogPiezo.capturePeakRaw) {
      analogPiezo.capturePeakRaw = raw;
      analogPiezo.captureTargetId = sample.targetId;
    }
    if (now - analogPiezo.captureStartedMs >= runtimeConfig.hit.piezoAoCaptureWindowMs) {
      uint32_t eventTsMs = analogPiezo.captureStartedMs;
      analogPiezo.captureActive = false;
      analogPiezo.lastCandidateMs = now;
      analogPiezo.quietStartedMs = 0;
      publishAdcHitEvent(analogPiezo.captureTargetId,
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
    analogPiezo.captureTargetId = sample.targetId;
    analogPiezo.quietStartedMs = 0;
    Serial.printf("[PIEZO AO] threshold crossed source=%s raw=%d left=%d right=%d front=%d threshold=%d ms=%lu do=%d\n",
                  sample.source,
                  raw,
                  sample.left,
                  sample.right,
                  sample.front,
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
  publishHpResetEventIfConnected(source);
  publishDeviceStatusIfConnected("reset");
}

static bool sourceCanFire(const char* source) {
  if (source == nullptr) return false;
  return strcmp(source, "jetson") == 0 || strcmp(source, "usb") == 0;
}

static void markNetworkQuietForFire(uint32_t now, uint32_t quietMs = FIRE_NETWORK_QUIET_MS) {
  const uint32_t until = now + quietMs;
  if ((int32_t)(until - fireNetworkQuietUntilMs) > 0) fireNetworkQuietUntilMs = until;
}

static void markNetworkQuietForFireStop(uint32_t now) {
  const uint32_t relaySettleMs = NIXO_RELAY2_ENABLED_VALUE ? runtimeConfig.nixo.relayDelay1Ms : 0;
  markNetworkQuietForFire(now, relaySettleMs + FIRE_NETWORK_QUIET_MS);
}

static bool shouldDeferNetworkForFire(uint32_t now) {
  if (jetsonFireHoldActive || jetsonFireReleaseRequired) return true;
  return (int32_t)(fireNetworkQuietUntilMs - now) > 0;
}

static void stopNixoFireCommand(const char* source) {
  markNetworkQuietForFireStop(millis());
  jetsonFireHoldActive = false;
  jetsonFireReleaseRequired = false;
  nixoFire.stopFire(source);
  Serial.printf("[CMD] fire stopped source=%s\n", source);
  if (SerialBT.hasClient()) {
    SerialBT.printf("[CMD] fire stopped source=%s\n", source);
  }
}

static const char* defaultFireSourceForTransport(const char* source) {
  return strcmp(source, "jetson") == 0 ? "jetson_uart" : source;
}

static String parseFireSource(String line, const char* source) {
  line.trim();
  int split = line.indexOf(' ');
  if (split < 0) return String(defaultFireSourceForTransport(source));
  String fireSource = line.substring(split + 1);
  fireSource.trim();
  if (fireSource.startsWith("source=")) fireSource = fireSource.substring(7);
  fireSource.trim();
  return fireSource.length() > 0 ? fireSource : String(defaultFireSourceForTransport(source));
}

static void handleCommandChar(char c, const char* source, const char* fireSourceOverride = nullptr) {
  c = normalizeCommandChar(c);
  if (c == CMD_RESET_HIT_DISPLAY || c == 'r') {
    resetAll(source);
    return;
  }
  if (c == 'h') {
    if (source == nullptr || strcmp(source, "jetson") != 0 ||
        localHitState.down || localHitState.hpRemaining == 0) {
      Serial.printf("[HP] admin damage ignored source=%s hp=%u/%u down=%s\n",
                    source == nullptr ? "unknown" : source,
                    localHitState.hpRemaining,
                    localHitState.maxHits,
                    localHitState.down ? "true" : "false");
      return;
    }
    applyLocalHit(++hitSequence, millis());
    publishDeviceStatusIfConnected("jetson_hp_damage");
    Serial.printf("[HP] admin damage applied source=%s hp=%u/%u\n",
                  source,
                  localHitState.hpRemaining,
                  localHitState.maxHits);
    return;
  }
  if (c == 'x' || c == '0') {
    stopNixoFireCommand(source);
    return;
  }
  if (c == '1' || c == 'f') {
    if (!sourceCanFire(source)) {
      Serial.printf("[FIRE] ignored source=%s reason=jetson_uart_required\n", source);
      if (SerialBT.hasClient() && strcmp(source, "bt") == 0) {
        SerialBT.println("[FIRE] ignored reason=jetson_uart_required");
      }
      return;
    }
    const char* fireSource = (fireSourceOverride != nullptr && fireSourceOverride[0] != '\0')
                                 ? fireSourceOverride
                                 : defaultFireSourceForTransport(source);
    if (strcmp(source, "usb") == 0) {
      const bool accepted = nixoFire.startFire(runtimeConfig.nixo.fireDefaultDurationMs, fireSource, false);
      Serial.printf("[CMD] USB fire %s source=%s duration_ms=%lu\n",
                    accepted ? "started" : "ignored",
                    fireSource,
                    (unsigned long)runtimeConfig.nixo.fireDefaultDurationMs);
      return;
    }
    const uint32_t now = millis();
    const bool wasFiring = nixoFire.isFiring();
    bool accepted = false;
    if (jetsonFireReleaseRequired) {
      jetsonFireHoldDeadlineMs = now + JETSON_FIRE_HOLD_TIMEOUT_MS;
      markNetworkQuietForFire(now, JETSON_FIRE_HOLD_TIMEOUT_MS + FIRE_NETWORK_QUIET_MS);
      Serial.printf("[FIRE] ignored source=%s fire_source=%s reason=release_required\n", source, fireSource);
      return;
    }
    if (wasFiring && !jetsonFireHoldActive) {
      jetsonFireReleaseRequired = true;
      jetsonFireHoldDeadlineMs = now + JETSON_FIRE_HOLD_TIMEOUT_MS;
      markNetworkQuietForFire(now, JETSON_FIRE_HOLD_TIMEOUT_MS + FIRE_NETWORK_QUIET_MS);
      Serial.printf("[FIRE] ignored source=%s fire_source=%s reason=non_jetson_fire_active\n", source, fireSource);
      return;
    }
    if (jetsonFireHoldActive && !wasFiring) {
      jetsonFireReleaseRequired = true;
      jetsonFireHoldDeadlineMs = now + JETSON_FIRE_HOLD_TIMEOUT_MS;
      markNetworkQuietForFire(now, JETSON_FIRE_HOLD_TIMEOUT_MS + FIRE_NETWORK_QUIET_MS);
      Serial.printf("[FIRE] ignored source=%s fire_source=%s reason=release_required_after_duration\n", source, fireSource);
      return;
    }
    if (wasFiring) {
      nixoFire.noteFireSource(fireSource);
      accepted = true;
    } else {
      accepted = nixoFire.startFire(runtimeConfig.nixo.fireMaxDurationMs, fireSource, true);
    }
    if (accepted) {
      jetsonFireHoldActive = true;
      jetsonFireReleaseRequired = false;
      jetsonFireHoldDeadlineMs = now + JETSON_FIRE_HOLD_TIMEOUT_MS;
      markNetworkQuietForFire(now, JETSON_FIRE_HOLD_TIMEOUT_MS + FIRE_NETWORK_QUIET_MS);
    }
    Serial.printf("[CMD] fire %s source=%s fire_source=%s hold_timeout_ms=%lu\n",
                  accepted ? (wasFiring ? "keepalive" : "started") : "ignored",
                  source,
                  fireSource,
                  (unsigned long)JETSON_FIRE_HOLD_TIMEOUT_MS);
    if (SerialBT.hasClient()) {
      SerialBT.printf("[CMD] fire %s source=%s\n",
                      accepted ? (wasFiring ? "keepalive" : "started") : "ignored",
                      source);
    }
  }
}

static void replyToSource(const char* source, const String& line) {
  Serial.println(line);
  if (String(source) == "bt" && SerialBT.hasClient()) SerialBT.println(line);
}

static void addHitTuningStatus(JsonObject doc) {
  doc["hit_cooldown_ms"] = runtimeConfig.hit.hitCooldownMs;
  doc["offline_queue_capacity"] = runtimeConfig.hit.offlineQueueCapacity;
  doc["offline_queue_flush_interval_ms"] = runtimeConfig.hit.offlineQueueFlushIntervalMs;
  doc["led_brightness"] = runtimeConfig.hit.ledBrightness;
  doc["ring_brightness"] = runtimeConfig.hit.ringBrightness;
  doc["piezo_left_pin"] = PIEZO_LEFT_AO_PIN;
  doc["piezo_right_pin"] = PIEZO_RIGHT_AO_PIN;
  doc["piezo_front_pin"] = PIEZO_FRONT_AO_PIN;
  doc["piezo_ao_pin"] = PIEZO_LEFT_AO_PIN;
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

  JsonObject hp = doc.createNestedObject("hp");
  hp["robot_id"] = runtimeConfig.hit.robotId;
  hp["current"] = localHitState.hpRemaining;
  hp["max"] = localHitState.maxHits;
  hp["down"] = localHitState.down;
  hp["accepted_hit_count"] = localHitState.acceptedHitCount;
  hp["ring_fill_ratio"] = localHpFillRatio();
  hp["last_hit_sequence"] = localHitState.lastHitSequence;
}

static void addNixoTuningStatus(JsonObject doc) {
  doc["nixo_fire_default_duration_ms"] = runtimeConfig.nixo.fireDefaultDurationMs;
  doc["nixo_fire_min_duration_ms"] = runtimeConfig.nixo.fireMinDurationMs;
  doc["nixo_fire_max_duration_ms"] = runtimeConfig.nixo.fireMaxDurationMs;
  doc["nixo_fire_cooldown_ms"] = runtimeConfig.nixo.fireCooldownMs;
  doc["nixo_prefire_delay_ms"] = runtimeConfig.nixo.prefireDelayMs;
  doc["nixo_relay_delay1_ms"] = runtimeConfig.nixo.relayDelay1Ms;
  doc["nixo_relay1_pin"] = NIXO_RELAY1_PIN_VALUE;
  doc["nixo_relay2_pin"] = NIXO_RELAY2_PIN_VALUE;
  doc["nixo_relay_on_level"] = NIXO_RELAY_ON_LEVEL_VALUE;
  doc["nixo_relay_off_level"] = NIXO_RELAY_OFF_LEVEL_VALUE;
  doc["nixo_relay1_readback"] = digitalRead(NIXO_RELAY1_PIN_VALUE);
  doc["nixo_relay2_readback"] = NIXO_RELAY2_ENABLED_VALUE ? digitalRead(NIXO_RELAY2_PIN_VALUE) : -1;
  doc["nixo_state"] = nixoFire.fireStateName();
  doc["nixo_fire_source"] = nixoFire.activeFireSource();
  doc["nixo_last_fire_source"] = nixoFire.lastFireSource();
  doc["jetson_fire_hold_active"] = jetsonFireHoldActive;
  doc["jetson_fire_release_required"] = jetsonFireReleaseRequired;
  doc["jetson_fire_hold_timeout_ms"] = JETSON_FIRE_HOLD_TIMEOUT_MS;

  JsonObject nixo = doc.createNestedObject("nixo");
  nixo["id"] = runtimeConfig.nixo.nixoId;
  nixo["state"] = nixoFire.fireStateName();
  nixo["firing"] = nixoFire.isFiring();
  nixo["active_source"] = nixoFire.activeFireSource();
  nixo["last_source"] = nixoFire.lastFireSource();
  nixo["fire_inhibited"] = nixoFire.fireInhibited();
  nixo["cooldown_remaining_ms"] = nixoFire.cooldownRemainingMs(millis());
  nixo["transport"] = NIXO_TRANSPORT;
  nixo["mqtt_connected"] = nixoFire.connected();
  nixo["command_topic"] = nixoFire.commandTopic();
  nixo["relay1_pin"] = NIXO_RELAY1_PIN_VALUE;
  nixo["relay2_pin"] = NIXO_RELAY2_PIN_VALUE;
  nixo["relay_on_level"] = NIXO_RELAY_ON_LEVEL_VALUE;
  nixo["relay_off_level"] = NIXO_RELAY_OFF_LEVEL_VALUE;
  nixo["relay1_readback"] = digitalRead(NIXO_RELAY1_PIN_VALUE);
  nixo["relay2_readback"] = NIXO_RELAY2_ENABLED_VALUE ? digitalRead(NIXO_RELAY2_PIN_VALUE) : -1;
  nixo["jetson_hold_active"] = jetsonFireHoldActive;
  nixo["jetson_release_required"] = jetsonFireReleaseRequired;
  nixo["jetson_hold_timeout_ms"] = JETSON_FIRE_HOLD_TIMEOUT_MS;
}

static void printStatusJson(const char* source, const char* reason) {
  DynamicJsonDocument doc(4096);
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
  doc["nixo_transport"] = NIXO_TRANSPORT;
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

static void rememberPublishedStatusSnapshot() {
  hasPublishedStatusSnapshot = true;
  lastPublishedHpRemaining = localHitState.hpRemaining;
  lastPublishedMaxHits = localHitState.maxHits;
  lastPublishedAcceptedHitCount = localHitState.acceptedHitCount;
  lastPublishedDown = localHitState.down;
  lastPublishedNixoFiring = nixoFire.isFiring();
  lastPublishedFireInhibited = nixoFire.fireInhibited();
  lastPublishedJetsonHoldActive = jetsonFireHoldActive;
  lastPublishedJetsonReleaseRequired = jetsonFireReleaseRequired;
  lastPublishedNixoState = nixoFire.fireStateName();
  lastPublishedNixoActiveSource = nixoFire.activeFireSource();
  lastPublishedNixoLastFireSource = nixoFire.lastFireSource();
}

static bool statusStateChanged() {
  if (!hasPublishedStatusSnapshot) return true;
  return lastPublishedHpRemaining != localHitState.hpRemaining ||
         lastPublishedMaxHits != localHitState.maxHits ||
         lastPublishedAcceptedHitCount != localHitState.acceptedHitCount ||
         lastPublishedDown != localHitState.down ||
         lastPublishedNixoFiring != nixoFire.isFiring() ||
         lastPublishedFireInhibited != nixoFire.fireInhibited() ||
         lastPublishedJetsonHoldActive != jetsonFireHoldActive ||
         lastPublishedJetsonReleaseRequired != jetsonFireReleaseRequired ||
         strcmp(lastPublishedNixoState, nixoFire.fireStateName()) != 0 ||
         lastPublishedNixoActiveSource != nixoFire.activeFireSource() ||
         lastPublishedNixoLastFireSource != nixoFire.lastFireSource();
}

static bool localHitStateDead() {
  return localHitState.down || localHitState.hpRemaining == 0;
}

static void rememberJetsonHpStatusSnapshot() {
  hasSentJetsonHpStatus = true;
  lastJetsonHpRemaining = localHitState.hpRemaining;
  lastJetsonMaxHits = localHitState.maxHits;
  lastJetsonAcceptedHitCount = localHitState.acceptedHitCount;
  lastJetsonDead = localHitStateDead();
}

static bool jetsonHpStatusChanged() {
  if (!hasSentJetsonHpStatus) return true;
  return lastJetsonHpRemaining != localHitState.hpRemaining ||
         lastJetsonMaxHits != localHitState.maxHits ||
         lastJetsonAcceptedHitCount != localHitState.acceptedHitCount ||
         lastJetsonDead != localHitStateDead();
}

static bool jetsonNeedsResync = true;

static void beginJetsonHpLine() {
  // ponytail: Jetson UART is a tiny machine protocol; never mix debug text into this TX path.
  // ESP power/reset can leave a partial byte on Jetson RX; newline once resyncs Dora's line reader.
  if (jetsonNeedsResync) {
    JetsonSerial.write(static_cast<uint8_t>('\n'));
    jetsonNeedsResync = false;
  }
}

static void writeJetsonHpEvent(char event) {
  beginJetsonHpLine();
  JetsonSerial.write(static_cast<uint8_t>(event));
  JetsonSerial.write(static_cast<uint8_t>('\n'));
}

static char targetIdToJetsonDirectionCode(int targetId) {
  switch (targetId) {
  case 1:
    return 'l';
  case 2:
    return 'r';
  case 3:
  default:
    return 'f';
  }
}

static void writeJetsonDirectionalHpEvent(int targetId) {
  beginJetsonHpLine();
  JetsonSerial.write(static_cast<uint8_t>('h'));
  JetsonSerial.write(static_cast<uint8_t>(targetIdToJetsonDirectionCode(targetId)));
  JetsonSerial.write(static_cast<uint8_t>('\n'));
}

static void sendJetsonHpStatus(const char* reason) {
  const bool isDead = localHitStateDead();
  const bool isHit = strcmp(reason, "hit") == 0;
  if (isDead) {
    writeJetsonHpEvent('d');
  } else if (isHit) {
    writeJetsonDirectionalHpEvent(lastAcceptedHitTargetId);
  } else {
    writeJetsonHpEvent('r');
  }
  rememberJetsonHpStatusSnapshot();
}

static void publishJetsonHpStatus() {
  if (!hasSentJetsonHpStatus) {
    if (millis() - jetsonBootMs < JETSON_BOOT_RESET_DELAY_MS) return;
    sendJetsonHpStatus("reset");
    return;
  }
  const bool hpDecreased = localHitState.hpRemaining < lastJetsonHpRemaining;
  const bool hpIncreased = localHitState.hpRemaining > lastJetsonHpRemaining;
  const bool isDead = localHitStateDead();
  const bool becameDead = isDead && !lastJetsonDead;
  if (becameDead) {
    sendJetsonHpStatus("dead");
    return;
  }
  if (hpDecreased) {
    sendJetsonHpStatus("hit");
    return;
  }
  if (!isDead && (hpIncreased || lastJetsonDead)) {
    sendJetsonHpStatus("reset");
    return;
  }
  if (jetsonHpStatusChanged()) {
    rememberJetsonHpStatusSnapshot();
  }
}

static bool publishHpResetEventIfConnected(const char* reason) {
  if (!hitMqtt.connected()) {
    pendingHpResetEvent = true;
    return false;
  }
  const bool ok = hitMqtt.publishHpResetEvent(++hitSequence,
                                             millis(),
                                             reason,
                                             localHitState.acceptedHitCount,
                                             localHitState.hpRemaining,
                                             localHitState.maxHits,
                                             localHitState.down);
  pendingHpResetEvent = !ok;
  return ok;
}

static void publishDeviceStatusIfConnected(const char* reason) {
  if (!hitMqtt.connected()) return;
  DynamicJsonDocument doc(4096);
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
  doc["nixo_transport"] = NIXO_TRANSPORT;
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
  rememberPublishedStatusSnapshot();
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
  if (lower == "1" || lower == "f" || lower == "fire" || lower.startsWith("f ") ||
      lower.startsWith("fire ")) {
    String fireSource = parseFireSource(line, source);
    handleCommandChar('f', source, fireSource.c_str());
    return;
  }
  if (lower == "x" || lower == "0" || lower == "stop-fire" || lower == "fire off") {
    stopNixoFireCommand(source);
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
  return c == CMD_RESET_HIT_DISPLAY || c == 'r' || c == 'h' || c == '1' || c == 'f' || c == 'x' || c == '0';
}

static bool isJetsonBufferedImmediateCommand(Stream& stream, char c, const char* source) {
  if (strcmp(source, "jetson") != 0 || !isImmediateCommandChar(c) || stream.available() == 0) return false;
  const char next = (char)stream.peek();
  return isIgnoredCommandChar(next) || isImmediateCommandChar(next);
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
    if (line.length() == 0 &&
        ((isImmediateCommandChar(c) && stream.available() == 0) ||
         isJetsonBufferedImmediateCommand(stream, c, source))) {
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

static void updateJetsonFireHold(uint32_t now) {
  if (!jetsonFireHoldActive && !jetsonFireReleaseRequired) return;
  if ((int32_t)(now - jetsonFireHoldDeadlineMs) < 0) return;
  jetsonFireHoldActive = false;
  jetsonFireReleaseRequired = false;
  if (nixoFire.isFiring()) {
    markNetworkQuietForFireStop(now);
    nixoFire.stopFire("jetson-hold-timeout");
  }
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
    if (pendingHpResetEvent) publishHpResetEventIfConnected(hasSeenMqttConnection ? "mqtt_reconnected" : "boot");
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

static void publishStateChangeDeviceStatus(uint32_t now) {
  if (!hitMqtt.connected() || !statusStateChanged()) return;
  lastDeviceStatusMs = now;
  publishDeviceStatusIfConnected("state_changed");
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
    publishHpResetEventIfConnected("mqtt_reset");
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
  jetsonBootMs = millis();
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

  Serial.printf("[PIN] UART2 RX=%d TX=%d | HP_BAR_LED=%d count=%d groups=%d leds_per_group=%d | RING_LED=%d count=%d | PIEZO left=%d right=%d front=%d | PIEZO_DO_DEBUG=%d\n",
                UART_RX_PIN,
                UART_TX_PIN,
                HP_BAR_LED_PIN,
                HP_BAR_NUM_LEDS,
                HP_BAR_GROUP_COUNT,
                HP_BAR_LEDS_PER_GROUP,
                RING_LED_PIN,
                RING_NUM_LEDS,
                PIEZO_LEFT_AO_PIN,
                PIEZO_RIGHT_AO_PIN,
                PIEZO_FRONT_AO_PIN,
                PIEZO_DO_PIN);
  Serial.printf("[ADC] threshold=%d rearm_raw=%d capture_window_ms=%lu cooldown_ms=%lu rearm_stable_ms=%lu\n",
                runtimeConfig.hit.piezoAoThresholdRaw,
                runtimeConfig.hit.piezoAoRearmRaw,
                (unsigned long)runtimeConfig.hit.piezoAoCaptureWindowMs,
                (unsigned long)runtimeConfig.hit.hitCooldownMs,
                (unsigned long)runtimeConfig.hit.piezoAoRearmStableMs);
  Serial.printf("USB/BT/Jetson CMD: '%c'=reset ADC hit/display state; Jetson UART 'h'=HP damage, '1'/'f'=Nixo hold-fire, '0'/'x'=stop.\n",
                CMD_RESET_HIT_DISPLAY);
  Serial.println("USB/BT/Jetson line commands: s/status/show-status, x/0/stop-fire/fire off, show-config, provision {json}, config {json}, clear-config, check-ota [manifest-url].");
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
  Serial.printf("[NIXO] transport=%s nixo_id=%s command_topic=%s relay1=%d relay2=%d relay_on=%d relay_off=%d delay1_ms=%lu fire_default_ms=%lu fire_min_ms=%lu fire_max_ms=%lu cooldown_ms=%lu prefire_ms=%lu\n",
                NIXO_TRANSPORT,
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
  nixoFire.tickLocal(now);
  updateJetsonFireHold(now);
  ringDisplay.setCooldownState(nixoFire.isFiring(),
                               nixoFire.cooldownRemainingMs(now),
                               nixoFire.cooldownDurationMs(),
                               nixoFire.fireInhibited());
  barDisplay.tick(now);
  ringDisplay.tick(now);

  publishJetsonHpStatus();
  const bool deferNetworkForFire = shouldDeferNetworkForFire(now);
  if (!deferNetworkForFire) {
    nixoFire.tickNetwork(now);
    hitMqtt.tick(now, barDisplay.remoteDisplayActive(), true);
    publishMqttReconnectStatus(now);
    processPendingMqttManagement();
    publishStateChangeDeviceStatus(now);
    publishPeriodicDeviceStatus(now);
    pollConfiguredOta(now);
  }

  delay(1);
}
