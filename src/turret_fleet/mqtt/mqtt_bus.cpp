#include "mqtt_bus.h"

#include <ArduinoJson.h>
#include <Esp.h>

#include "../app/firmware_info.h"
#include "../ota/http_ota.h"
#include "../ota/ota_manifest.h"

namespace battlebang {
namespace turret_fleet {
namespace {
const unsigned long kMqttRetryMs = 5000;
const unsigned long kStatusIntervalMs = 10000;
const size_t kPayloadLimit = 2048;
}

void MqttBus::begin(RuntimeConfig& config, RuntimeConfigStore& store, WifiManager& wifi, TurretControl& control) {
  config_ = &config;
  store_ = &store;
  wifi_ = &wifi;
  control_ = &control;
  client_.setBufferSize(kPayloadLimit);
  client_.setKeepAlive(30);
  client_.setCallback([this](char* topic, byte* payload, unsigned int length) {
    this->handleMessage(topic, payload, length);
  });
  reconfigure();
}

void MqttBus::reconfigure() {
  subscriptionsDirty_ = true;
  if (client_.connected()) client_.disconnect();
  lastConnectAttemptMs_ = 0;
}

void MqttBus::loop() {
  if (config_ == nullptr || wifi_ == nullptr) return;
  if (!wifi_->connected()) return;

  if (connectIfNeeded()) {
    client_.loop();
  }

  const unsigned long now = millis();
  if (now - lastStatusMs_ >= kStatusIntervalMs) {
    lastStatusMs_ = now;
    publishStatus("heartbeat");
  }
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
      Serial.println("[fleet][mqtt] missing mqtt.host; provision config first");
    }
    return false;
  }

  const unsigned long now = millis();
  if (lastConnectAttemptMs_ != 0 && now - lastConnectAttemptMs_ < kMqttRetryMs) return false;
  lastConnectAttemptMs_ = now;

  client_.setServer(config_->mqttHost.c_str(), config_->mqttPort);
  const String clientId = String("bb-") + config_->deviceId;

  Serial.print("[fleet][mqtt] connecting to ");
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
    Serial.print("[fleet][mqtt] connect failed state=");
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
  for (const String& topic : topics) {
    const bool ok = client_.subscribe(topic.c_str());
    Serial.print("[fleet][mqtt] ");
    Serial.print(ok ? "subscribed " : "subscribe failed ");
    Serial.println(topic);
  }
  subscriptionsDirty_ = false;
}

void MqttBus::publishStatus(const char* reason) {
  if (config_ == nullptr || wifi_ == nullptr || control_ == nullptr || !client_.connected()) return;

  StaticJsonDocument<1024> doc;
  doc["type"] = "status";
  doc["reason"] = reason;
  doc["device_id"] = config_->deviceId;
  doc["turret_id"] = config_->turretId;
  doc["configured"] = config_->configured;
  doc["firmware_app"] = BB_TURRET_FLEET_APP_NAME;
  doc["firmware_version"] = BB_TURRET_FLEET_VERSION;
  doc["firmware_build"] = BB_TURRET_FLEET_BUILD;
  doc["git_sha"] = BB_TURRET_FLEET_GIT_SHA;
  doc["config_version"] = config_->configVersion;
  doc["mode"] = control_->mode();
  doc["wifi"] = wifi_->connected() ? "UP" : "DOWN";
  doc["ip"] = wifi_->ip();
  doc["rssi"] = wifi_->rssi();
  doc["uptime_ms"] = millis();

  String payload;
  serializeJson(doc, payload);
  TopicSet topics = buildTopics(*config_);
  client_.publish(topics.deviceStatus.c_str(), payload.c_str(), false);
  if (topics.turretStatus.length() > 0) {
    client_.publish(topics.turretStatus.c_str(), payload.c_str(), false);
  }
}

void MqttBus::handleMessage(char* topic, byte* payload, unsigned int length) {
  if (length >= kPayloadLimit) {
    Serial.println("[fleet][mqtt] payload too large; dropped");
    return;
  }

  String body;
  body.reserve(length + 1);
  for (unsigned int i = 0; i < length; ++i) body += static_cast<char>(payload[i]);

  const String topicStr(topic);
  Serial.print("[fleet][mqtt] topic=");
  Serial.print(topicStr);
  Serial.print(" payload=");
  Serial.println(body);

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
    Serial.print("[fleet][config] rejected: ");
    Serial.println(error);
    publishStatus("config_rejected");
    return;
  }

  *config_ = next;
  const bool saved = store_->save(*config_);
  control_->applyConfig(*config_);
  subscriptionsDirty_ = true;
  Serial.print("[fleet][config] applied config_version=");
  Serial.print(config_->configVersion);
  Serial.print(" saved=");
  Serial.println(saved ? "yes" : "no");
  publishStatus(saved ? "config_applied" : "config_applied_save_failed");
}

void MqttBus::handleOtaPayload(const char* payload) {
  OtaManifest manifest;
  String error;
  if (!parseOtaManifestJson(payload, manifest, error)) {
    Serial.print("[fleet][ota] rejected manifest: ");
    Serial.println(error);
    publishStatus("ota_manifest_rejected");
    return;
  }

  String reason;
  if (!shouldApplyOtaManifest(manifest, reason)) {
    Serial.print("[fleet][ota] skipped: ");
    Serial.println(reason);
    publishStatus("ota_skipped");
    return;
  }

  Serial.print("[fleet][ota] accepted ");
  Serial.println(otaManifestSummary(manifest));
  publishStatus("ota_downloading");
  OtaResult result = runHttpOta(manifest);
  Serial.print("[fleet][ota] result ok=");
  Serial.print(result.ok ? "yes" : "no");
  Serial.print(" message=");
  Serial.println(result.message);
  publishStatus(result.ok ? "ota_rebooting" : "ota_failed");
  if (result.ok) {
    delay(500);
    ESP.restart();
  }
}

void MqttBus::handleCommandPayload(const char* topic, const char* payload) {
  StaticJsonDocument<768> doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.print("[fleet][command] invalid json: ");
    Serial.println(err.c_str());
    return;
  }
  control_->handleCommandJson(doc, topic);
  publishStatus("command_applied");
}

}  // namespace turret_fleet
}  // namespace battlebang
