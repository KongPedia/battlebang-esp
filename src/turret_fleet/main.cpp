#include <Arduino.h>
#include <Preferences.h>
#include <esp32-hal-bt.h>
#include <esp_system.h>

#include "app/firmware_info.h"
#include "app/identity.h"
#include "config/runtime_config.h"
#include "control/turret_control.h"
#include "mqtt/mqtt_bus.h"
#include "net/wifi_manager.h"
#include "ota/http_ota.h"
#include "ota/ota_manifest.h"

using namespace battlebang::turret_fleet;

RuntimeConfig config;
RuntimeConfigStore configStore;
WifiManager wifi;
TurretControl control;
MqttBus mqtt;
bool networkStarted = false;
bool mqttStarted = false;
bool bootInitialTargetDone = false;
esp_reset_reason_t bootResetReason = ESP_RST_UNKNOWN;
bool bootInitialTargetMotionAllowed = true;
bool fireRecoveryRequiredAtBoot = false;
bool recoveryLockoutRequiredAtBoot = false;
bool bootAutoRecoveryAttempted = false;
bool bootAutoRecoverySucceeded = false;
unsigned long lastAutoOtaCheckMs = 0;

namespace {
char serialBuf[4096];
size_t serialLen = 0;
const char* kSafetyPrefsNamespace = "bb_fleet";
const char* kFireRecoveryMarkerKey = "fire_active";
const char* kRecoveryLockoutMarkerKey = "recover_req";

String configuredOtaPollUrl();

bool loadFireRecoveryMarker() {
  Preferences prefs;
  if (!prefs.begin(kSafetyPrefsNamespace, true)) return false;
  const bool active = prefs.getBool(kFireRecoveryMarkerKey, false);
  prefs.end();
  return active;
}

bool loadRecoveryLockoutMarker() {
  Preferences prefs;
  if (!prefs.begin(kSafetyPrefsNamespace, true)) return false;
  const bool active = prefs.getBool(kRecoveryLockoutMarkerKey, false);
  prefs.end();
  return active;
}

const char* resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "POWERON";
    case ESP_RST_EXT:
      return "EXT";
    case ESP_RST_SW:
      return "SW";
    case ESP_RST_PANIC:
      return "PANIC";
    case ESP_RST_INT_WDT:
      return "INT_WDT";
    case ESP_RST_TASK_WDT:
      return "TASK_WDT";
    case ESP_RST_WDT:
      return "WDT";
    case ESP_RST_DEEPSLEEP:
      return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:
      return "BROWNOUT";
    case ESP_RST_SDIO:
      return "SDIO";
    default:
      return "UNKNOWN";
  }
}

const char* bootLockoutReasonName() {
  if (bootResetReason == ESP_RST_BROWNOUT) return "brownout";
  if (fireRecoveryRequiredAtBoot) return "fire-reset-marker";
  if (recoveryLockoutRequiredAtBoot) return "recovery-marker";
  return "none";
}

void startNetwork(const char* reason) {
  if (networkStarted) return;
  Serial.print("[fleet][network] starting reason=");
  Serial.println(reason);
  networkStarted = true;
  wifi.begin(config);
  if (!mqttStarted) {
    mqtt.begin(config, configStore, wifi, control);
    mqttStarted = true;
  } else {
    mqtt.reconfigure();
  }
}

void stopNetwork(const char* reason) {
  Serial.print("[fleet][network] stopping reason=");
  Serial.println(reason);
  networkStarted = false;
  wifi.stop();
  if (mqttStarted) mqtt.reconfigure();
}

void printHelp() {
  Serial.println("[fleet] serial commands:");
  Serial.println("  help");
  Serial.println("  show-config");
  Serial.println("  show-status / debug");
  Serial.println("  clear-config");
  Serial.println("  config {json}");
  Serial.println("  provision {json}");
  Serial.println("  command {json}");
  Serial.println("  start-network");
  Serial.println("  stop-network");
  Serial.println("  check-ota [manifest-url]");
  Serial.print("  default broker: ");
  Serial.print(config.mqttHost);
  Serial.print(":");
  Serial.println(config.mqttPort);
  Serial.print("  default manifest: ");
  Serial.println(BB_TURRET_FLEET_LATEST_MANIFEST_URL);
}


void printStatus(const char* reason) {
  DynamicJsonDocument doc(4096);
  doc["type"] = "status";
  doc["reason"] = reason;
  doc["device_id"] = config.deviceId;
  doc["turret_id"] = config.turretId;
  doc["configured"] = config.configured;
  doc["firmware_app"] = BB_TURRET_FLEET_APP_NAME;
  doc["firmware_version"] = BB_TURRET_FLEET_VERSION;
  doc["firmware_build"] = BB_TURRET_FLEET_BUILD;
  doc["git_sha"] = BB_TURRET_FLEET_GIT_SHA;
  doc["config_version"] = config.configVersion;
  control.appendStatus(doc.as<JsonObject>());
  doc["wifi"] = wifi.connected() ? "UP" : "DOWN";
  doc["ip"] = wifi.ip();
  doc["rssi"] = wifi.rssi();
  doc["mqtt_host"] = config.mqttHost;
  doc["mqtt_port"] = config.mqttPort;
  doc["mqtt_root"] = config.mqttRoot;
  doc["network_started"] = networkStarted;
  doc["network_auto_start"] = config.networkAutoStart;
  doc["network_start_delay_ms"] = config.networkStartDelayMs;
  doc["ota_command_center_controlled"] = config.otaCommandCenterControlled;
  doc["ota_auto_check_enabled"] = config.otaAutoCheckEnabled;
  doc["ota_desired_build"] = config.otaDesiredBuild;
  doc["ota_channel"] = config.otaChannel;
  doc["ota_manifest_url"] = configuredOtaPollUrl();
  doc["reset_reason"] = resetReasonName(bootResetReason);
  doc["fire_recovery_required_at_boot"] = fireRecoveryRequiredAtBoot;
  doc["recovery_lockout_required_at_boot"] = recoveryLockoutRequiredAtBoot;
  doc["boot_auto_recovery_attempted"] = bootAutoRecoveryAttempted;
  doc["boot_auto_recovery_succeeded"] = bootAutoRecoverySucceeded;
  doc["uptime_ms"] = millis();

  String out;
  serializeJson(doc, out);
  Serial.println(out);
}

String configuredOtaPollUrl() {
  if (config.otaLocalMirrorUrl.length() > 0) return config.otaLocalMirrorUrl;
  return config.otaPublicManifestUrl.length() > 0 ? config.otaPublicManifestUrl :
                                                   String(BB_TURRET_FLEET_LATEST_MANIFEST_URL);
}

bool commandCenterApprovesPolledOta(const OtaManifest& manifest, String& reason) {
  if (config.otaChannel.length() > 0 && manifest.channel.length() > 0 &&
      manifest.channel != config.otaChannel) {
    reason = String("channel mismatch expected=") + config.otaChannel + " got=" + manifest.channel;
    return false;
  }

  if (!config.otaCommandCenterControlled) {
    reason = "command-center approval not required by config";
    return true;
  }

  if (config.otaDesiredBuild == 0) {
    reason = "command-center desired_build is 0";
    return false;
  }
  if (manifest.build != config.otaDesiredBuild) {
    reason = String("manifest build ") + manifest.build +
             " does not match command-center desired_build " + config.otaDesiredBuild;
    return false;
  }

  reason = "command-center desired_build approved";
  return true;
}

void publishMqttStatusIfConnected(const char* reason) {
  if (mqttStarted && mqtt.connected()) {
    mqtt.publishStatus(reason);
  }
}

bool checkOtaManifestUrlWithPolicy(const String& url, bool requireCommandCenterApproval, const char* source) {
  if (!wifi.connected()) {
    Serial.println("[fleet][ota] Wi-Fi is not connected; cannot fetch OTA manifest");
    return false;
  }

  String payload;
  String error;
  if (!fetchHttpText(url, 4096, payload, error)) {
    Serial.print("[fleet][ota] manifest fetch failed from ");
    Serial.print(source);
    Serial.print(": ");
    Serial.println(error);
    publishMqttStatusIfConnected("ota_poll_fetch_failed");
    return false;
  }

  OtaManifest manifest;
  if (!parseOtaManifestJson(payload.c_str(), manifest, error)) {
    Serial.print("[fleet][ota] manifest rejected from ");
    Serial.print(source);
    Serial.print(": ");
    Serial.println(error);
    publishMqttStatusIfConnected("ota_poll_manifest_rejected");
    return false;
  }

  String reason;
  if (requireCommandCenterApproval && !commandCenterApprovesPolledOta(manifest, reason)) {
    Serial.print("[fleet][ota] poll skipped: ");
    Serial.println(reason);
    publishMqttStatusIfConnected("ota_poll_not_approved");
    return false;
  }

  if (!shouldApplyOtaManifest(manifest, reason)) {
    Serial.print("[fleet][ota] skipped: ");
    Serial.println(reason);
    publishMqttStatusIfConnected("ota_poll_skipped");
    return false;
  }

  if (config.otaApplyOnlyInSafeState && !control.isSafeForOta()) {
    Serial.println("[fleet][ota] deferred: turret is not in a safe OTA state");
    publishMqttStatusIfConnected("ota_deferred");
    return false;
  }

  Serial.print("[fleet][ota] accepted ");
  Serial.println(otaManifestSummary(manifest));
  publishMqttStatusIfConnected("ota_downloading");
  OtaResult result = runHttpOta(manifest);
  Serial.print("[fleet][ota] result ok=");
  Serial.print(result.ok ? "yes" : "no");
  Serial.print(" message=");
  Serial.println(result.message);
  publishMqttStatusIfConnected(result.ok ? "ota_rebooting" : "ota_failed");
  if (result.ok) {
    delay(500);
    ESP.restart();
  }
  return result.ok;
}

void handleCommandJsonForDebug(const char* json) {
  DynamicJsonDocument doc(1024);
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    Serial.print("[fleet][serial] command json rejected: ");
    Serial.println(err.c_str());
    return;
  }
  control.handleCommandJson(doc, "SERIAL(command)");
  printStatus("serial_command");
}

void applyAndPersistConfig(const char* json) {
  String error;
  RuntimeConfig next = config;
  if (!applyRuntimeConfigJson(json, next, error)) {
    Serial.print("[fleet][serial] config rejected: ");
    Serial.println(error);
    return;
  }

  config = next;
  const bool saved = configStore.save(config);
  Serial.print("[fleet][serial] config applied saved=");
  Serial.println(saved ? "yes" : "no");
  control.applyConfig(config);
  bootInitialTargetDone = false;
  if (networkStarted) {
    wifi.begin(config);
    mqtt.reconfigure();
  }
  if (!networkStarted && config.networkAutoStart && millis() >= config.networkStartDelayMs) {
    startNetwork("config_auto_start");
  }
}

void checkOtaManifestUrl(const String& url) {
  checkOtaManifestUrlWithPolicy(url, false, "serial");
}

void handleSerialLine(String line) {
  line.trim();
  if (line.length() == 0) return;

  if (line == "help") {
    printHelp();
    return;
  }
  if (line == "show-config") {
    Serial.println(runtimeConfigToJson(config, false));
    return;
  }
  if (line == "show-status" || line == "status" || line == "debug") {
    printStatus("serial_debug");
    return;
  }
  if (line == "clear-config") {
    const bool cleared = configStore.clear();
    config = makeDefaultRuntimeConfig(config.deviceId);
    control.applyConfig(config);
    bootInitialTargetDone = false;
    if (networkStarted) {
      wifi.begin(config);
      mqtt.reconfigure();
    }
    Serial.print("[fleet][serial] config cleared=");
    Serial.println(cleared ? "yes" : "no");
    return;
  }
  if (line == "start-network" || line == "wifi" || line == "network-start") {
    startNetwork("serial");
    return;
  }
  if (line == "stop-network" || line == "network-stop") {
    stopNetwork("serial");
    return;
  }
  if (line.startsWith("config ")) {
    applyAndPersistConfig(line.substring(7).c_str());
    return;
  }
  if (line.startsWith("provision ")) {
    applyAndPersistConfig(line.substring(10).c_str());
    return;
  }
  if (line.startsWith("command ")) {
    handleCommandJsonForDebug(line.substring(8).c_str());
    return;
  }
  if (line.startsWith("target ")) {
    String payload = String("{\"command\":\"target\",\"target\":") + line.substring(7) + "}";
    handleCommandJsonForDebug(payload.c_str());
    return;
  }
  if (line == "check-ota" || line == "check-latest") {
    checkOtaManifestUrl(BB_TURRET_FLEET_LATEST_MANIFEST_URL);
    return;
  }
  if (line.startsWith("check-ota ")) {
    String url = line.substring(10);
    url.trim();
    if (url.length() == 0) url = BB_TURRET_FLEET_LATEST_MANIFEST_URL;
    checkOtaManifestUrl(url);
    return;
  }

  Serial.print("[fleet][serial] unsupported command: ");
  Serial.println(line);
  printHelp();
}

void pollSerial() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      if (serialLen > 0) {
        serialBuf[serialLen] = '\0';
        handleSerialLine(String(serialBuf));
        serialLen = 0;
      }
      continue;
    }

    if (serialLen < sizeof(serialBuf) - 1) {
      serialBuf[serialLen++] = c;
    } else {
      serialLen = 0;
      Serial.println("[fleet][serial] buffer overflow; command dropped");
    }
  }
}

void pollConfiguredOta() {
  if (!config.otaAutoCheckEnabled) return;
  if (!wifi.connected()) return;
  if (config.otaCommandCenterControlled &&
      (config.otaDesiredBuild == 0 || config.otaDesiredBuild <= BB_TURRET_FLEET_BUILD)) {
    return;
  }

  const unsigned long now = millis();
  const uint32_t intervalS = config.otaCheckIntervalS < 30 ? 30 : config.otaCheckIntervalS;
  const unsigned long intervalMs = static_cast<unsigned long>(intervalS) * 1000UL;
  if (lastAutoOtaCheckMs == 0) {
    lastAutoOtaCheckMs = now;
    return;
  }
  if (now - lastAutoOtaCheckMs < intervalMs) return;
  lastAutoOtaCheckMs = now;

  const String url = configuredOtaPollUrl();
  if (url.length() == 0) {
    Serial.println("[fleet][ota] auto poll skipped: no manifest URL configured");
    publishMqttStatusIfConnected("ota_poll_no_url");
    return;
  }

  Serial.print("[fleet][ota] auto polling ");
  Serial.println(url);
  checkOtaManifestUrlWithPolicy(url, true, "auto_poll");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(100);
  bootResetReason = esp_reset_reason();
  fireRecoveryRequiredAtBoot = loadFireRecoveryMarker();
  recoveryLockoutRequiredAtBoot = loadRecoveryLockoutMarker();
  bootInitialTargetMotionAllowed = bootResetReason != ESP_RST_BROWNOUT &&
                                   !fireRecoveryRequiredAtBoot &&
                                   !recoveryLockoutRequiredAtBoot;

  Serial.println("[fleet][power] stopping Bluetooth controller");
  btStop();
  Serial.println("[fleet][power] setting CPU frequency to 80 MHz");
  setCpuFrequencyMhz(80);

  config = makeDefaultRuntimeConfig(buildDeviceId());
  configStore.load(config);
  Serial.println("=== BATTLEBANG TURRET FLEET FIRMWARE ===");
  Serial.print("app=");
  Serial.print(BB_TURRET_FLEET_APP_NAME);
  Serial.print(" hardware=");
  Serial.print(BB_TURRET_FLEET_HARDWARE);
  Serial.print(" version=");
  Serial.print(BB_TURRET_FLEET_VERSION);
  Serial.print(" build=");
  Serial.print(BB_TURRET_FLEET_BUILD);
  Serial.print(" device_id=");
  Serial.println(config.deviceId);
  Serial.print("reset_reason=");
  Serial.println(resetReasonName(bootResetReason));
  if (!bootInitialTargetMotionAllowed) {
    Serial.print("[fleet][motion] boot safety lockout; reason=");
    Serial.print(bootLockoutReasonName());
    Serial.println("; boot initial target will be computed but outputs stay detached");
  }
  Serial.print("release_repo=");
  Serial.println(BB_TURRET_FLEET_RELEASE_REPO);
  Serial.print("latest_manifest=");
  Serial.println(BB_TURRET_FLEET_LATEST_MANIFEST_URL);
  control.begin(config);
  control.setBrownoutLockout(!bootInitialTargetMotionAllowed);
  if (!bootInitialTargetMotionAllowed) {
    bootAutoRecoveryAttempted = true;
    bootAutoRecoverySucceeded = control.recoverBrownoutLockoutIfSafe("boot_auto_recover");
    if (bootAutoRecoverySucceeded) {
      Serial.println("[fleet][safety] boot auto-recovery cleared lockout; boot HOME drive remains inhibited until explicit command");
    } else {
      Serial.println("[fleet][safety] boot auto-recovery kept lockout active; use hold/recover after checking hardware");
    }
  }
  Serial.println(runtimeConfigToJson(config, false));
  printStatus("boot");
  printHelp();
  if (!config.networkAutoStart) {
    Serial.println("[fleet][network] auto_start disabled in NVS, but boot auto-network is forced for turret_fleet operation");
  }
  startNetwork("boot_forced");
}

void loop() {
  pollSerial();
  if (!networkStarted) {
    startNetwork("boot_retry");
  }
  if (networkStarted) {
    wifi.loop(config);
    mqtt.loop();
    if (!bootInitialTargetDone && mqttStarted && mqtt.connected() && config.configured) {
      bootInitialTargetDone = true;
      control.enterBootInitialTarget(bootInitialTargetMotionAllowed);
      mqtt.publishStatus(bootInitialTargetMotionAllowed ? "boot_initial_target" :
                                                          "boot_initial_target_brownout_inhibited");
    }
    pollConfiguredOta();
  }
  control.loop();
}
