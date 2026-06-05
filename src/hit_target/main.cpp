#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP.h>

#include "hit_target/app/firmware_info.h"
#include "hit_target/app/identity.h"
#include "hit_target/build_config.h"
#include "hit_target/config/runtime_config.h"
#include "hit_target/mqtt/mqtt_bus.h"
#include "hit_target/net/wifi_manager.h"
#include "hit_target/ota/http_ota.h"
#include "hit_target/ota/ota_manifest.h"
#include "hit_target/ota/reboot_marker.h"
#include "hit_target/target/hit_target_controller.h"

using battlebang::hit_target::DeviceIdentity;
using battlebang::hit_target::HitTargetController;
using battlebang::hit_target::HitTargetEvent;
using battlebang::hit_target::MqttBus;
using battlebang::hit_target::OtaManifest;
using battlebang::hit_target::OtaResult;
using battlebang::hit_target::RuntimeConfig;
using battlebang::hit_target::RuntimeConfigStore;
using battlebang::hit_target::WifiManager;
using battlebang::hit_target::applyRuntimeConfigJson;
using battlebang::hit_target::fetchHttpText;
using battlebang::hit_target::gameplayConfigChanged;
using battlebang::hit_target::ledHardwareChanged;
using battlebang::hit_target::makeDefaultRuntimeConfig;
using battlebang::hit_target::parseOtaManifestJson;
using battlebang::hit_target::runtimeConfigToJson;
using battlebang::hit_target::runHttpOta;
using battlebang::hit_target::sensorPinsChanged;
using battlebang::hit_target::shouldApplyOtaManifest;
using battlebang::hit_target::otaManifestSummary;
using battlebang::hit_target::consumeOtaRebootMarker;
using battlebang::hit_target::writeOtaRebootMarker;

namespace {

DeviceIdentity identity;
RuntimeConfig config;
RuntimeConfigStore configStore;
WifiManager wifi;
HitTargetController target;
MqttBus mqtt;

bool networkStarted = false;
bool mqttStarted = false;
bool postOtaReboot = false;
uint32_t lastSerialHeartbeatMs = 0;
uint32_t lastAutoOtaCheckMs = 0;
String serialLine;

constexpr uint32_t SERIAL_STATUS_PERIOD_MS = 1000;

void publishMqttStatusIfConnected(const char* reason) {
  if (mqttStarted && mqtt.connected()) mqtt.publishStatus(reason);
}

void printStatusJson(const char* reason,
                     const char* eventName = "status",
                     uint16_t peak = 0,
                     const char* source = "state",
                     uint16_t digitalEdges = 0) {
  DynamicJsonDocument doc(4096);
  doc["event"] = eventName;
  doc["reason"] = reason;
  doc["source"] = source;
  doc["peak"] = peak;
  doc["digital_edges"] = digitalEdges;
  doc["firmware_app"] = BB_HIT_TARGET_APP_NAME;
  doc["firmware_version"] = BB_HIT_TARGET_VERSION;
  doc["firmware_build"] = BB_HIT_TARGET_BUILD;
  doc["firmware_hardware"] = BB_HIT_TARGET_HARDWARE;
  doc["git_sha"] = BB_HIT_TARGET_GIT_SHA;
  target.appendStatus(doc.as<JsonObject>());
  doc["wifi"] = wifi.connected() ? "UP" : "DOWN";
  doc["ip"] = wifi.ip();
  doc["rssi"] = wifi.rssi();
  doc["network_started"] = networkStarted;
  doc["network_auto_start"] = config.networkAutoStart;
  doc["network_start_delay_ms"] = config.networkStartDelayMs;
  doc["mqtt_host"] = config.mqttHost;
  doc["mqtt_port"] = config.mqttPort;
  doc["mqtt_root"] = config.mqttRoot;
  doc["ota_command_center_controlled"] = config.otaCommandCenterControlled;
  doc["ota_auto_check_enabled"] = config.otaAutoCheckEnabled;
  doc["ota_desired_build"] = config.otaDesiredBuild;
  doc["ota_channel"] = config.otaChannel;
  doc["ota_manifest_url"] = config.otaLocalMirrorUrl.length() > 0 ? config.otaLocalMirrorUrl : config.otaPublicManifestUrl;
  doc["post_ota_reboot"] = postOtaReboot;
  doc["uptime_ms"] = millis();
  String out;
  serializeJson(doc, out);
  Serial.println(out);
}

void onTargetEvent(const HitTargetEvent& event, void*) {
  printStatusJson(event.name, event.name, event.peak, event.source, event.digitalEdges);
  publishMqttStatusIfConnected(event.name);
}

void startNetwork(const char* reason) {
  if (networkStarted) return;
  Serial.print("[hit_target][network] starting reason=");
  Serial.println(reason);
  networkStarted = true;
  wifi.begin(config);
  if (!mqttStarted) {
    mqtt.begin(config, configStore, wifi, target);
    mqttStarted = true;
  } else {
    mqtt.reconfigure();
  }
}

void stopNetwork(const char* reason) {
  Serial.print("[hit_target][network] stopping reason=");
  Serial.println(reason);
  networkStarted = false;
  wifi.stop();
  if (mqttStarted) mqtt.reconfigure();
}

String configuredOtaPollUrl() {
  if (config.otaLocalMirrorUrl.length() > 0) return config.otaLocalMirrorUrl;
  return config.otaPublicManifestUrl.length() > 0 ? config.otaPublicManifestUrl : String(BB_HIT_TARGET_LATEST_MANIFEST_URL);
}

bool commandCenterApprovesPolledOta(const OtaManifest& manifest, String& reason) {
  if (config.otaChannel.length() > 0 && manifest.channel.length() > 0 && manifest.channel != config.otaChannel) {
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

void checkOtaManifestUrlWithPolicy(const String& url, bool requireCommandCenterApproval, const char* source) {
  if (!wifi.connected()) {
    Serial.println("[hit_target][ota] Wi-Fi is not connected; cannot check OTA");
    publishMqttStatusIfConnected("ota_no_wifi");
    return;
  }
  String body;
  String error;
  if (!fetchHttpText(url, 4096, body, error)) {
    Serial.print("[hit_target][ota] manifest fetch failed: ");
    Serial.println(error);
    publishMqttStatusIfConnected("ota_manifest_fetch_failed");
    return;
  }
  OtaManifest manifest;
  if (!parseOtaManifestJson(body.c_str(), manifest, error)) {
    Serial.print("[hit_target][ota] manifest parse failed: ");
    Serial.println(error);
    publishMqttStatusIfConnected("ota_manifest_rejected");
    return;
  }
  if (requireCommandCenterApproval) {
    String approvalReason;
    if (!commandCenterApprovesPolledOta(manifest, approvalReason)) {
      Serial.print("[hit_target][ota] auto poll not approved: ");
      Serial.println(approvalReason);
      publishMqttStatusIfConnected("ota_not_approved");
      return;
    }
    Serial.print("[hit_target][ota] auto poll approved: ");
    Serial.println(approvalReason);
  }
  String reason;
  if (!shouldApplyOtaManifest(manifest, reason)) {
    Serial.print("[hit_target][ota] skipped: ");
    Serial.println(reason);
    publishMqttStatusIfConnected("ota_skipped");
    return;
  }
  if (config.otaApplyOnlyInSafeState && !target.isSafeForOta()) {
    Serial.println("[hit_target][ota] deferred: target is not in a safe OTA state");
    publishMqttStatusIfConnected("ota_deferred");
    return;
  }
  Serial.print("[hit_target][ota] accepted from ");
  Serial.print(source);
  Serial.print(' ');
  Serial.println(otaManifestSummary(manifest));
  publishMqttStatusIfConnected("ota_downloading");
  target.prepareForOta();
  OtaResult result = runHttpOta(manifest);
  Serial.print("[hit_target][ota] result ok=");
  Serial.print(result.ok ? "yes" : "no");
  Serial.print(" message=");
  Serial.println(result.message);
  publishMqttStatusIfConnected(result.ok ? "ota_rebooting" : "ota_failed");
  if (result.ok) {
    writeOtaRebootMarker(true);
    delay(500);
    ESP.restart();
  }
}

void checkOtaManifestUrl(const String& url) {
  checkOtaManifestUrlWithPolicy(url, false, "serial");
}

void applyAndPersistConfig(const char* json, const char* source) {
  String error;
  RuntimeConfig next = config;
  if (!applyRuntimeConfigJson(json, next, error)) {
    Serial.print("[hit_target][serial] config rejected: ");
    Serial.println(error);
    printStatusJson("config_rejected", "config_rejected", 0, source);
    return;
  }
  const RuntimeConfig previous = config;
  const bool wifiChanged = next.wifiSsid != config.wifiSsid || next.wifiPassword != config.wifiPassword;
  const bool mqttChanged = next.mqttHost != config.mqttHost || next.mqttPort != config.mqttPort ||
                           next.mqttUsername != config.mqttUsername || next.mqttPassword != config.mqttPassword ||
                           next.mqttRoot != config.mqttRoot || next.targetId != config.targetId;
  const bool resetState = gameplayConfigChanged(previous, next) || sensorPinsChanged(previous, next);
  const bool hardwareChanged = ledHardwareChanged(previous, next);
  config = next;
  const bool saved = configStore.save(config);
  target.applyConfig(config, resetState, source);
  if (hardwareChanged) {
    Serial.println("[hit_target][config] LED pin/type/order are hardware-profile values; reboot/rebuild may be required for those fields");
  }
  if (wifiChanged && networkStarted) wifi.begin(config);
  if (mqttChanged && mqttStarted) mqtt.reconfigure();
  if (!networkStarted && config.networkAutoStart && millis() >= config.networkStartDelayMs) startNetwork("config_auto_start");
  Serial.print("[hit_target][serial] config applied saved=");
  Serial.println(saved ? "yes" : "no");
  printStatusJson(saved ? "config_applied" : "config_applied_save_failed",
                  saved ? "config_applied" : "config_applied_save_failed",
                  0,
                  source);
  publishMqttStatusIfConnected(saved ? "config_applied" : "config_applied_save_failed");
}

void printHelp() {
  Serial.println("[CMD] r/2=reset, h=simulate hit, s/status/show-status=JSON status");
  Serial.println("[CMD] show-config, config {json}, provision {json}, clear-config");
  Serial.println("[CMD] start-network, stop-network, check-ota [url], help");
  Serial.println("[MQTT] {root}/devices/{device_id}/status|config|ota");
  Serial.println("[MQTT] {root}/hit_targets/{target_id}/status|config|command|ota and {root}/hit_targets/all/ota");
}

void clearConfig() {
  const bool cleared = configStore.clear();
  config = makeDefaultRuntimeConfig(identity);
  target.applyConfig(config, true, "clear_config");
  if (networkStarted) {
    wifi.begin(config);
    mqtt.reconfigure();
  }
  Serial.print("[hit_target][serial] config cleared=");
  Serial.println(cleared ? "yes" : "no");
  printStatusJson("config_cleared", "config_cleared", 0, "serial");
}

void handleCommandLine(String line) {
  line.trim();
  if (line.length() == 0) return;
  String lower = line;
  lower.toLowerCase();

  if (lower == "help" || lower == "?") {
    printHelp();
    return;
  }
  if (lower == "r" || lower == "2" || lower == "reset" || lower == "init" || lower == "initialize") {
    target.reset("serial");
    return;
  }
  if (lower == "h" || lower == "hit") {
    target.simulateHit("serial");
    return;
  }
  if (lower == "s" || lower == "status" || lower == "show-status" || lower == "debug") {
    printStatusJson("serial_debug", "status", 0, "serial");
    return;
  }
  if (lower == "show-config") {
    Serial.println(runtimeConfigToJson(config, false));
    return;
  }
  if (lower == "show-config-secrets") {
    Serial.println(runtimeConfigToJson(config, true));
    return;
  }
  if (lower == "clear-config") {
    clearConfig();
    return;
  }
  if (lower == "start-network" || lower == "wifi" || lower == "network-start") {
    startNetwork("serial");
    return;
  }
  if (lower == "stop-network" || lower == "network-stop") {
    stopNetwork("serial");
    return;
  }
  if (lower == "check-ota" || lower == "check-latest") {
    checkOtaManifestUrl(configuredOtaPollUrl());
    return;
  }
  if (lower.startsWith("check-ota ")) {
    String url = line.substring(10);
    url.trim();
    if (url.length() == 0) url = configuredOtaPollUrl();
    checkOtaManifestUrl(url);
    return;
  }
  if (lower.startsWith("config ")) {
    applyAndPersistConfig(line.substring(7).c_str(), "serial");
    return;
  }
  if (lower.startsWith("provision ")) {
    applyAndPersistConfig(line.substring(10).c_str(), "serial");
    return;
  }
  Serial.print("[hit_target][serial] unknown command: ");
  Serial.println(line);
  printHelp();
}

void pollSerial() {
  while (Serial.available() > 0) {
    char c = static_cast<char>(Serial.read());
    if ((c == 'r' || c == 'R' || c == 'h' || c == 'H' || c == 's' || c == 'S' || c == '2') && serialLine.length() == 0 && Serial.available() == 0) {
      String single(c);
      handleCommandLine(single);
      continue;
    }
    if (c == '\r' || c == '\n') {
      if (serialLine.length() > 0) {
        handleCommandLine(serialLine);
        serialLine = "";
      }
      continue;
    }
    if (serialLine.length() < 4096) serialLine += c;
  }
}

void publishPeriodicSerialStatus(uint32_t now) {
  if (now - lastSerialHeartbeatMs < SERIAL_STATUS_PERIOD_MS) return;
  lastSerialHeartbeatMs = now;
  printStatusJson("heartbeat", "heartbeat", 0, "state");
}

void pollConfiguredOta() {
  if (!config.otaAutoCheckEnabled) return;
  if (!wifi.connected()) return;
  if (config.otaCommandCenterControlled &&
      (config.otaDesiredBuild == 0 || config.otaDesiredBuild <= BB_HIT_TARGET_BUILD)) {
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
    Serial.println("[hit_target][ota] auto poll skipped: no manifest URL configured");
    publishMqttStatusIfConnected("ota_poll_no_url");
    return;
  }
  Serial.print("[hit_target][ota] auto polling ");
  Serial.println(url);
  checkOtaManifestUrlWithPolicy(url, true, "auto_poll");
}

}  // namespace

void setup() {
  Serial.begin(::hit_target::SERIAL_BAUD);
  delay(200);
  postOtaReboot = consumeOtaRebootMarker();
  identity = battlebang::hit_target::buildDeviceIdentity(::hit_target::TARGET_ID_PREFIX);
  config = makeDefaultRuntimeConfig(identity);
  configStore.load(config);

  Serial.println("=== BATTLEBANG HIT TARGET FIRMWARE ===");
  target.begin(config, onTargetEvent, nullptr);
  target.printBootBanner();
  Serial.print("release_repo=");
  Serial.println(BB_HIT_TARGET_RELEASE_REPO);
  Serial.print("latest_manifest=");
  Serial.println(BB_HIT_TARGET_LATEST_MANIFEST_URL);
  if (postOtaReboot) Serial.println("[hit_target][ota] post-OTA reboot marker consumed");
  Serial.println(runtimeConfigToJson(config, false));
  printHelp();
  target.reset("boot");
  if (config.networkAutoStart && millis() >= config.networkStartDelayMs) startNetwork("boot_auto");
}

void loop() {
  const uint32_t now = millis();
  pollSerial();
  target.loop(now);
  if (!networkStarted && config.networkAutoStart && now >= config.networkStartDelayMs) startNetwork("boot_auto_delay");
  if (networkStarted) {
    wifi.loop(config);
    mqtt.loop();
    pollConfiguredOta();
  }
  publishPeriodicSerialStatus(now);
  delay(1);
}
