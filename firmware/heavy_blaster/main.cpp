#include <Arduino.h>
#include <ArduinoJson.h>
#include <Esp.h>
#include <bb_esp_ota/http_ota.h>
#include <bb_esp_ota/ota_manifest.h>
#include <bb_esp_ota/reboot_marker.h>

#include "heavy_blaster/app/firmware_info.h"
#include "heavy_blaster/app/identity.h"
#include "heavy_blaster/blaster/heavy_blaster_controller.h"
#include "heavy_blaster/build_config.h"
#include "heavy_blaster/config/runtime_config.h"
#include "heavy_blaster/mqtt/mqtt_bus.h"
#include "heavy_blaster/net/wifi_manager.h"

using battlebang::heavy_blaster::DeviceIdentity;
using battlebang::heavy_blaster::HeavyBlasterController;
using battlebang::heavy_blaster::HeavyBlasterEvent;
using battlebang::heavy_blaster::MqttBus;
using battlebang::heavy_blaster::RuntimeConfig;
using battlebang::heavy_blaster::RuntimeConfigStore;
using battlebang::heavy_blaster::WifiManager;
using battlebang::heavy_blaster::applyRuntimeConfigJson;
using battlebang::heavy_blaster::connectivityConfigChanged;
using battlebang::heavy_blaster::hardwareProfileChanged;
using battlebang::heavy_blaster::makeDefaultRuntimeConfig;
using battlebang::heavy_blaster::mqttIdentityConfigChanged;
using battlebang::heavy_blaster::runtimeConfigToJson;
using battlebang::heavy_blaster::visualConfigChanged;

namespace {

DeviceIdentity identity;
RuntimeConfig config;
RuntimeConfigStore configStore;
WifiManager wifi;
HeavyBlasterController blaster;
MqttBus mqtt;

bool networkStarted = false;
bool mqttStarted = false;
bool postOtaReboot = false;
uint32_t lastSerialHeartbeatMs = 0;
uint32_t lastAutoOtaCheckMs = 0;
String serialLine;

constexpr uint32_t SERIAL_STATUS_PERIOD_MS = 10000;
constexpr const char* OTA_REBOOT_NAMESPACE = "bb_heavy_blaster";
constexpr const char* OTA_REBOOT_KEY = "ota_reboot";

void publishMqttStatusIfConnected(const char* reason) {
  if (mqttStarted && mqtt.connected()) mqtt.publishStatus(reason);
}

void printStatusJson(const char* reason, const char* eventName = "status", const char* source = "state") {
  DynamicJsonDocument doc(4096);
  doc["event"] = eventName;
  doc["reason"] = reason;
  doc["source"] = source;
  doc["firmware_app"] = BB_HEAVY_BLASTER_APP_NAME;
  doc["firmware_version"] = BB_HEAVY_BLASTER_VERSION;
  doc["firmware_build"] = BB_HEAVY_BLASTER_BUILD;
  doc["firmware_hardware"] = BB_HEAVY_BLASTER_HARDWARE;
  doc["git_sha"] = BB_HEAVY_BLASTER_GIT_SHA;
  blaster.appendStatus(doc.as<JsonObject>());
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
  doc["ota_supported"] = true;
  doc["post_ota_reboot"] = postOtaReboot;
  doc["uptime_ms"] = millis();
  String out;
  serializeJson(doc, out);
  Serial.println(out);
}

void onBlasterEvent(const HeavyBlasterEvent& event, void*) {
  printStatusJson(event.name, event.name, event.source);
  publishMqttStatusIfConnected(event.name);
}

void startNetwork(const char* reason) {
  if (networkStarted) return;
  Serial.print("[heavy-blaster][network] starting reason=");
  Serial.println(reason);
  networkStarted = true;
  wifi.begin(config);
  if (!mqttStarted) {
    mqtt.begin(config, configStore, wifi, blaster);
    mqttStarted = true;
  } else {
    mqtt.reconfigure();
  }
}

void stopNetwork(const char* reason) {
  Serial.print("[heavy-blaster][network] stopping reason=");
  Serial.println(reason);
  networkStarted = false;
  wifi.stop();
  if (mqttStarted) mqtt.reconfigure();
}


battlebang::esp::app::FirmwareIdentity firmwareIdentity() {
  battlebang::esp::app::FirmwareIdentity id;
  id.app = BB_HEAVY_BLASTER_APP_NAME;
  id.hardware = BB_HEAVY_BLASTER_HARDWARE;
  id.version = BB_HEAVY_BLASTER_VERSION;
  id.build = BB_HEAVY_BLASTER_BUILD;
  id.gitSha = BB_HEAVY_BLASTER_GIT_SHA;
  id.releaseRepo = BB_HEAVY_BLASTER_RELEASE_REPO;
  id.latestManifestUrl = BB_HEAVY_BLASTER_LATEST_MANIFEST_URL;
  return id;
}

String configuredOtaPollUrl() {
  if (config.otaLocalMirrorUrl.length() > 0) return config.otaLocalMirrorUrl;
  return config.otaPublicManifestUrl.length() > 0 ? config.otaPublicManifestUrl : String(BB_HEAVY_BLASTER_LATEST_MANIFEST_URL);
}

bool commandCenterApprovesPolledOta(const battlebang::esp::ota::OtaManifest& manifest, String& reason) {
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
    reason = String("manifest build ") + String(manifest.build) +
             " does not match command-center desired_build " + String(config.otaDesiredBuild);
    return false;
  }
  reason = "command-center desired_build approved";
  return true;
}

void checkOtaManifestUrlWithPolicy(const String& url, bool requireCommandCenterApproval, const char* source) {
  if (!wifi.connected()) {
    Serial.println("[heavy-blaster][ota] Wi-Fi is not connected; cannot check OTA");
    publishMqttStatusIfConnected("ota_no_wifi");
    return;
  }
  String body;
  String error;
  if (!battlebang::esp::ota::fetchHttpText(url, 4096, body, error)) {
    Serial.print("[heavy-blaster][ota] manifest fetch failed: ");
    Serial.println(error);
    publishMqttStatusIfConnected("ota_manifest_fetch_failed");
    return;
  }
  battlebang::esp::ota::OtaManifest manifest;
  if (!battlebang::esp::ota::parseManifestJson(body.c_str(), manifest, error)) {
    Serial.print("[heavy-blaster][ota] manifest parse failed: ");
    Serial.println(error);
    publishMqttStatusIfConnected("ota_manifest_rejected");
    return;
  }
  if (requireCommandCenterApproval) {
    String approvalReason;
    if (!commandCenterApprovesPolledOta(manifest, approvalReason)) {
      Serial.print("[heavy-blaster][ota] auto poll not approved: ");
      Serial.println(approvalReason);
      publishMqttStatusIfConnected("ota_not_approved");
      return;
    }
    Serial.print("[heavy-blaster][ota] auto poll approved: ");
    Serial.println(approvalReason);
  }
  String reason;
  if (!battlebang::esp::ota::shouldApplyManifest(manifest, firmwareIdentity(), reason)) {
    Serial.print("[heavy-blaster][ota] skipped: ");
    Serial.println(reason);
    publishMqttStatusIfConnected("ota_skipped");
    return;
  }
  if (config.otaApplyOnlyInSafeState && !blaster.isSafeForOta()) {
    Serial.println("[heavy-blaster][ota] deferred: blaster is not in a safe OTA state");
    publishMqttStatusIfConnected("ota_deferred");
    return;
  }
  Serial.print("[heavy-blaster][ota] accepted from ");
  Serial.print(source);
  Serial.print(' ');
  Serial.println(battlebang::esp::ota::manifestSummary(manifest));
  publishMqttStatusIfConnected("ota_downloading");
  blaster.prepareForOta();
  battlebang::esp::ota::OtaResult result = battlebang::esp::ota::runHttpOta(manifest);
  Serial.print("[heavy-blaster][ota] result ok=");
  Serial.print(result.ok ? "yes" : "no");
  Serial.print(" message=");
  Serial.println(result.message);
  publishMqttStatusIfConnected(result.ok ? "ota_rebooting" : "ota_failed");
  if (result.ok) {
    battlebang::esp::ota::writeRebootMarker(OTA_REBOOT_NAMESPACE, OTA_REBOOT_KEY, true);
    delay(500);
    ESP.restart();
  } else {
    blaster.recoverFromFailedOta("ota_failed");
  }
}

void checkOtaManifestUrl(const String& url) {
  checkOtaManifestUrlWithPolicy(url, false, "serial");
}

void applyAndPersistConfig(const char* json, const char* source) {
  String error;
  RuntimeConfig next = config;
  if (!applyRuntimeConfigJson(json, next, error)) {
    Serial.print("[heavy-blaster][serial] config rejected: ");
    Serial.println(error);
    printStatusJson("config_rejected", "config_rejected", source);
    publishMqttStatusIfConnected("config_rejected");
    return;
  }
  const RuntimeConfig previous = config;
  const bool wifiChanged = connectivityConfigChanged(previous, next);
  const bool mqttChanged = mqttIdentityConfigChanged(previous, next);
  const bool resetState = visualConfigChanged(previous, next) || hardwareProfileChanged(previous, next);
  config = next;
  const bool saved = configStore.save(config);
  blaster.applyConfig(config, resetState, source);
  if (wifiChanged && networkStarted) wifi.begin(config);
  if (mqttStarted && mqttChanged) {
    Serial.println("[heavy-blaster][serial] MQTT identity/settings changed; reconnecting/resubscribing");
    mqtt.reconfigure();
  }
  if (!networkStarted && config.networkAutoStart && millis() >= config.networkStartDelayMs) startNetwork("config_auto_start");
  Serial.print("[heavy-blaster][serial] config applied saved=");
  Serial.println(saved ? "yes" : "no");
  printStatusJson(saved ? "config_applied" : "config_applied_save_failed",
                  saved ? "config_applied" : "config_applied_save_failed",
                  source);
  publishMqttStatusIfConnected(saved ? "config_applied" : "config_applied_save_failed");
}

void printHelp() {
  Serial.println("[CMD] status/show-status, reset/lock, unlock, set-slot <0..3> <0|1>, set-slots <bitmask>, show-config");
  Serial.println("[CMD] config {json}, provision {json}, clear-config, check-ota [manifest-url], start-network, stop-network, help");
  Serial.println("[MQTT] {root}/devices/{device_id}/status|config|ota");
  Serial.println("[MQTT] {root}/heavy-blasters/{blaster_id}/status|config|command|ota and {root}/heavy-blasters/all/ota");
  Serial.println("[NOTE] Bluetooth prototype controls are not production commands; use MQTT from Command Center.");
}

void clearConfig() {
  const bool cleared = configStore.clear();
  config = makeDefaultRuntimeConfig(identity);
  blaster.applyConfig(config, true, "clear_config");
  if (networkStarted) {
    wifi.begin(config);
    mqtt.reconfigure();
  }
  Serial.print("[heavy-blaster][serial] config cleared=");
  Serial.println(cleared ? "yes" : "no");
  printStatusJson(cleared ? "config_cleared" : "config_clear_failed", cleared ? "config_cleared" : "config_clear_failed");
  publishMqttStatusIfConnected(cleared ? "config_cleared" : "config_clear_failed");
}

void handleSerialLine(String line) {
  line.trim();
  if (line.length() == 0) return;
  String lower = line;
  lower.toLowerCase();

  if (lower == "help" || lower == "?") {
    printHelp();
    return;
  }
  if (lower == "status" || lower == "show-status" || lower == "s") {
    printStatusJson("serial_status", "status", "serial");
    publishMqttStatusIfConnected("serial_status");
    return;
  }
  if (lower == "show-config") {
    Serial.println(runtimeConfigToJson(config, false));
    return;
  }
  if (lower == "reset" || lower == "lock" || lower == "r") {
    blaster.reset("serial");
    publishMqttStatusIfConnected("serial_reset");
    return;
  }
  if (lower == "unlock" || lower == "activate") {
    if (!config.debugAllowLocalControl) {
      Serial.println("[heavy-blaster][serial] unlock rejected; debug_allow_local_control=false");
      printStatusJson("local_control_rejected", "command_rejected", "serial");
      return;
    }
    blaster.unlock("serial");
    publishMqttStatusIfConnected("serial_unlock");
    return;
  }
  if (lower == "start-network") {
    startNetwork("serial");
    return;
  }
  if (lower == "stop-network") {
    stopNetwork("serial");
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
    clearConfig();
    return;
  }
  if (line.startsWith("config ")) {
    applyAndPersistConfig(line.substring(7).c_str(), "serial_config");
    return;
  }
  if (line.startsWith("provision ")) {
    applyAndPersistConfig(line.substring(10).c_str(), "serial_provision");
    return;
  }
  if (lower.startsWith("set-slot ")) {
    if (!config.debugAllowLocalControl) {
      Serial.println("[heavy-blaster][serial] set-slot rejected; debug_allow_local_control=false");
      printStatusJson("local_control_rejected", "command_rejected", "serial");
      return;
    }
    int firstSpace = line.indexOf(' ');
    int secondSpace = line.indexOf(' ', firstSpace + 1);
    if (secondSpace < 0) {
      Serial.println("[heavy-blaster][serial] usage: set-slot <0..3> <0|1>");
      return;
    }
    const int index = line.substring(firstSpace + 1, secondSpace).toInt();
    const bool active = line.substring(secondSpace + 1).toInt() != 0;
    if (!blaster.setSlot(static_cast<uint8_t>(index), active, "serial")) {
      Serial.println("[heavy-blaster][serial] set-slot rejected");
    }
    publishMqttStatusIfConnected("serial_set_slot");
    return;
  }
  if (lower.startsWith("set-slots ")) {
    if (!config.debugAllowLocalControl) {
      Serial.println("[heavy-blaster][serial] set-slots rejected; debug_allow_local_control=false");
      printStatusJson("local_control_rejected", "command_rejected", "serial");
      return;
    }
    const uint8_t mask = static_cast<uint8_t>(strtoul(line.substring(10).c_str(), nullptr, 0));
    bool slots[::heavy_blaster::kSlotCount] = {false, false, false, false};
    for (uint8_t i = 0; i < ::heavy_blaster::kSlotCount; ++i) slots[i] = (mask & (1U << i)) != 0;
    blaster.setSlots(slots, ::heavy_blaster::kSlotCount, "serial");
    publishMqttStatusIfConnected("serial_set_slots");
    return;
  }

  Serial.print("[heavy-blaster][serial] unknown command: ");
  Serial.println(line);
}


void pollConfiguredOta() {
  if (!config.otaAutoCheckEnabled) return;
  if (!wifi.connected()) return;
  if (config.otaCommandCenterControlled &&
      (config.otaDesiredBuild == 0 || config.otaDesiredBuild <= BB_HEAVY_BLASTER_BUILD)) {
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
    Serial.println("[heavy-blaster][ota] auto poll skipped: no manifest URL configured");
    publishMqttStatusIfConnected("ota_poll_no_url");
    return;
  }
  Serial.print("[heavy-blaster][ota] auto polling ");
  Serial.println(url);
  checkOtaManifestUrlWithPolicy(url, true, "auto_poll");
}

void pollSerial() {
  while (Serial.available() > 0) {
    char ch = static_cast<char>(Serial.read());
    if (ch == '\n' || ch == '\r') {
      handleSerialLine(serialLine);
      serialLine = "";
    } else if (serialLine.length() < 4096) {
      serialLine += ch;
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(::heavy_blaster::SERIAL_BAUD);
  delay(200);
  postOtaReboot = battlebang::esp::ota::consumeRebootMarker(OTA_REBOOT_NAMESPACE, OTA_REBOOT_KEY);
  identity = battlebang::heavy_blaster::buildDeviceIdentity(::heavy_blaster::TARGET_ID_PREFIX);
  config = makeDefaultRuntimeConfig(identity);
  const bool loadedStoredConfig = configStore.load(config);
  Serial.print("[heavy-blaster][config] ");
  Serial.println(loadedStoredConfig ? "loaded stored config" : "no valid stored config; using MAC-derived defaults");
  Serial.print("release_repo=");
  Serial.println(BB_HEAVY_BLASTER_RELEASE_REPO);
  Serial.print("latest_manifest=");
  Serial.println(BB_HEAVY_BLASTER_LATEST_MANIFEST_URL);
  if (postOtaReboot) Serial.println("[heavy-blaster][ota] post-OTA reboot marker consumed");
  blaster.begin(config, onBlasterEvent, nullptr);
  blaster.printBootBanner();
  printHelp();
  printStatusJson("boot", "boot");
  if (config.networkAutoStart && config.networkStartDelayMs == 0) startNetwork("boot_auto_start");
}

void loop() {
  const uint32_t now = millis();
  pollSerial();
  if (!networkStarted && config.networkAutoStart && now >= config.networkStartDelayMs) startNetwork("delayed_auto_start");
  if (networkStarted) {
    wifi.loop(config);
    if (mqttStarted) mqtt.loop();
    pollConfiguredOta();
  }
  blaster.loop(now);
  if (now - lastSerialHeartbeatMs >= SERIAL_STATUS_PERIOD_MS) {
    lastSerialHeartbeatMs = now;
    printStatusJson("serial_heartbeat", "status");
  }
}
