#include "mqtt_bus.h"

#include <ArduinoJson.h>
#include <Esp.h>

#include "boss_target/app/firmware_info.h"
#include "boss_target/mqtt/topics.h"
#include "boss_target/ota/http_ota.h"
#include "boss_target/ota/ota_manifest.h"
#include "boss_target/ota/reboot_marker.h"

namespace battlebang {
namespace boss_target {
namespace {
constexpr size_t kPayloadLimit = 8192;
constexpr unsigned long kMqttRetryMs = 5000;
constexpr unsigned long kStatusIntervalMs = 5000;
constexpr unsigned long kStatusChangeCheckMs = 200;
constexpr size_t kStatusDocCapacity = 6144;
}  // namespace

void MqttBus::begin(RuntimeConfig& config, RuntimeConfigStore& store, WifiManager& wifi, BossTargetController& target) {
  config_ = &config;
  store_ = &store;
  wifi_ = &wifi;
  target_ = &target;
  client_.setBufferSize(kPayloadLimit);
  client_.setKeepAlive(30);
  client_.setCallback([this](char* topic, byte* payload, unsigned int length) {
    this->handleMessage(topic, payload, length);
  });
  reconfigure();
}

void MqttBus::reconfigure() {
  subscriptionsDirty_ = true;
  subscribedTopics_.clear();
  if (client_.connected()) client_.disconnect();
  lastConnectAttemptMs_ = 0;
}

bool MqttBus::connected() {
  return client_.connected();
}

void MqttBus::loop() {
  if (config_ == nullptr || wifi_ == nullptr) return;
  if (!wifi_->connected()) return;
  if (connectIfNeeded()) client_.loop();
  const unsigned long now = millis();
  if (target_ != nullptr && now - lastStatusChangeCheckMs_ >= kStatusChangeCheckMs) {
    lastStatusChangeCheckMs_ = now;
    const String signature = target_->statusSignature();
    if (signature != lastStatusSignature_) {
      publishStatus("state_changed");
      return;
    }
  }
  if (now - lastStatusMs_ >= kStatusIntervalMs) publishStatus("heartbeat");
}

bool MqttBus::connectIfNeeded() {
  if (client_.connected()) {
    if (subscriptionsDirty_) subscribeTopics();
    return true;
  }
  if (config_->mqttHost.length() == 0) {
    static bool warned = false;
    if (!warned) {
      warned = true;
      Serial.println("[boss_target][mqtt] missing mqtt.host; provision config first");
    }
    return false;
  }
  const unsigned long now = millis();
  if (lastConnectAttemptMs_ != 0 && now - lastConnectAttemptMs_ < kMqttRetryMs) return false;
  lastConnectAttemptMs_ = now;
  client_.setServer(config_->mqttHost.c_str(), config_->mqttPort);
  const String clientId = String("bb-boss-") + config_->deviceId;
  Serial.print("[boss_target][mqtt] connecting to ");
  Serial.print(config_->mqttHost);
  Serial.print(':');
  Serial.println(config_->mqttPort);
  bool ok = false;
  if (config_->mqttUsername.length() == 0) {
    ok = client_.connect(clientId.c_str());
  } else {
    ok = client_.connect(clientId.c_str(), config_->mqttUsername.c_str(), config_->mqttPassword.c_str());
  }
  if (!ok) {
    Serial.print("[boss_target][mqtt] connect failed state=");
    Serial.println(client_.state());
    return false;
  }
  subscribeTopics();
  publishStatus("connected");
  return true;
}

void MqttBus::subscribeTopics() {
  if (!client_.connected()) return;
  std::vector<String> topics = buildSubscriptionTopics(*config_);
  for (const String& previousTopic : subscribedTopics_) {
    bool stillNeeded = false;
    for (const String& topic : topics) {
      if (topic == previousTopic) {
        stillNeeded = true;
        break;
      }
    }
    if (!stillNeeded) client_.unsubscribe(previousTopic.c_str());
  }
  for (const String& topic : topics) {
    const bool ok = client_.subscribe(topic.c_str());
    Serial.print("[boss_target][mqtt] ");
    Serial.print(ok ? "subscribed " : "subscribe failed ");
    Serial.println(topic);
  }
  subscribedTopics_ = topics;
  subscriptionsDirty_ = false;
}

void MqttBus::publishStatus(const char* reason) {
  if (config_ == nullptr || wifi_ == nullptr || target_ == nullptr || !client_.connected()) return;
  lastStatusMs_ = millis();
  DynamicJsonDocument doc(kStatusDocCapacity);
  doc["type"] = "status";
  doc["reason"] = reason;
  doc["firmware_app"] = BB_BOSS_TARGET_APP_NAME;
  doc["firmware_version"] = BB_BOSS_TARGET_VERSION;
  doc["firmware_build"] = BB_BOSS_TARGET_BUILD;
  doc["firmware_hardware"] = BB_BOSS_TARGET_HARDWARE;
  doc["git_sha"] = BB_BOSS_TARGET_GIT_SHA;
  target_->appendStatus(doc.as<JsonObject>());
  doc["wifi"] = wifi_->connected() ? "UP" : "DOWN";
  doc["ip"] = wifi_->ip();
  doc["rssi"] = wifi_->rssi();
  doc["mqtt_root"] = config_->mqttRoot;
  doc["ota_command_center_controlled"] = config_->otaCommandCenterControlled;
  doc["ota_auto_check_enabled"] = config_->otaAutoCheckEnabled;
  doc["ota_desired_build"] = config_->otaDesiredBuild;
  doc["ota_channel"] = config_->otaChannel;
  doc["ota_manifest_url"] = config_->otaLocalMirrorUrl.length() > 0 ? config_->otaLocalMirrorUrl : config_->otaPublicManifestUrl;
  doc["uptime_ms"] = millis();
  String payload;
  serializeJson(doc, payload);
  TopicSet topics = buildTopics(*config_);
  bool ok = client_.publish(topics.deviceStatus.c_str(), payload.c_str(), false);
  if (topics.bossStatus.length() > 0) ok = client_.publish(topics.bossStatus.c_str(), payload.c_str(), false) && ok;
  if (!ok) {
    Serial.print("[boss_target][mqtt] status publish failed len=");
    Serial.print(payload.length());
    Serial.print(" buffer=");
    Serial.println(kPayloadLimit);
  }
  lastStatusSignature_ = target_->statusSignature();
}

void MqttBus::handleMessage(char* topic, byte* payload, unsigned int length) {
  if (length >= kPayloadLimit) {
    Serial.println("[boss_target][mqtt] payload too large; dropped");
    return;
  }
  String body;
  body.reserve(length + 1);
  for (unsigned int i = 0; i < length; ++i) body += static_cast<char>(payload[i]);
  const String topicStr(topic);
  if (topicStr.endsWith("/config")) {
    handleConfigPayload(body.c_str());
  } else if (topicStr.endsWith("/ota")) {
    handleOtaPayload(body.c_str());
  } else if (topicStr.endsWith("/command")) {
    handleCommandPayload(topic, body.c_str());
  }
}

void MqttBus::handleConfigPayload(const char* payload) {
  String error;
  RuntimeConfig next = *config_;
  if (!applyRuntimeConfigJson(payload, next, error)) {
    Serial.print("[boss_target][config] rejected: ");
    Serial.println(error);
    publishStatus("config_rejected");
    return;
  }
  const RuntimeConfig previous = *config_;
  const bool wifiChanged = next.wifiSsid != config_->wifiSsid || next.wifiPassword != config_->wifiPassword;
  const bool mqttChanged = next.mqttHost != config_->mqttHost || next.mqttPort != config_->mqttPort ||
                           next.mqttUsername != config_->mqttUsername || next.mqttPassword != config_->mqttPassword ||
                           next.mqttRoot != config_->mqttRoot || next.bossId != config_->bossId;
  const bool resetState = gameplayConfigChanged(previous, next) || sensorPinsChanged(previous, next);
  *config_ = next;
  const bool saved = store_->save(*config_);
  target_->applyConfig(*config_, resetState, "mqtt_config");
  if (wifiChanged && wifi_ != nullptr) wifi_->begin(*config_);
  if (mqttChanged) {
    reconfigure();
  } else {
    subscriptionsDirty_ = true;
  }
  publishStatus(saved ? "config_applied" : "config_applied_save_failed");
}

void MqttBus::handleCommandPayload(const char* topic, const char* payload) {
  DynamicJsonDocument doc(1024);
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.print("[boss_target][command] invalid json: ");
    Serial.println(err.c_str());
    publishStatus("command_rejected");
    return;
  }
  const char* command = doc["command"] | doc["type"] | "";
  if (strcmp(command, "status") == 0) {
    publishStatus("command_status");
    return;
  }
  if (strcmp(command, "reset") == 0 || strcmp(command, "init") == 0 || strcmp(command, "initialize") == 0) {
    target_->reset("mqtt");
    publishStatus("command_reset");
    return;
  }
  if (strcmp(command, "start") == 0 || strcmp(command, "arm") == 0 || strcmp(command, "activate") == 0) {
    const bool resetHp = doc["reset_hp"] | true;
    target_->start("mqtt", resetHp);
    publishStatus("command_start");
    return;
  }
  if (strcmp(command, "simulate_hit") == 0) {
    if (!config_->debugAllowSimulateHit) {
      Serial.println("[boss_target][command] simulate_hit rejected; debug_allow_simulate_hit=false");
      publishStatus("command_rejected");
      return;
    }
    int targetIndex = doc["target_index"] | -1;
    target_->simulateHit("mqtt", targetIndex);
    publishStatus("command_simulate_hit");
    return;
  }
  Serial.print("[boss_target][command] unsupported command on ");
  Serial.print(topic);
  Serial.print(": ");
  Serial.println(command);
  publishStatus("command_rejected");
}

void MqttBus::handleOtaPayload(const char* payload) {
  OtaManifest manifest;
  String error;
  if (!parseOtaManifestJson(payload, manifest, error)) {
    Serial.print("[boss_target][ota] rejected manifest: ");
    Serial.println(error);
    publishStatus("ota_manifest_rejected");
    return;
  }
  String reason;
  if (!shouldApplyOtaManifest(manifest, reason)) {
    Serial.print("[boss_target][ota] skipped: ");
    Serial.println(reason);
    publishStatus("ota_skipped");
    return;
  }
  if (config_->otaApplyOnlyInSafeState && target_ != nullptr && !target_->isSafeForOta()) {
    Serial.println("[boss_target][ota] deferred: target is not in a safe OTA state");
    publishStatus("ota_deferred");
    return;
  }
  publishStatus("ota_downloading");
  if (target_ != nullptr) target_->prepareForOta();
  OtaResult result = runHttpOta(manifest);
  publishStatus(result.ok ? "ota_rebooting" : "ota_failed");
  if (result.ok) {
    writeOtaRebootMarker(true);
    delay(500);
    ESP.restart();
  }
}

}  // namespace boss_target
}  // namespace battlebang
