#include "mqtt_bus.h"

#include <ArduinoJson.h>
#include <Esp.h>
#include <bb_esp_ota/http_ota.h>
#include <bb_esp_ota/ota_manifest.h>
#include <bb_esp_ota/reboot_marker.h>

#include "heavy_blaster/app/firmware_info.h"
#include "heavy_blaster/mqtt/topics.h"

namespace battlebang {
namespace heavy_blaster {
namespace {
constexpr size_t kPayloadLimit = 8192;
constexpr unsigned long kMqttRetryMs = 5000;
constexpr unsigned long kStatusIntervalMs = 5000;
constexpr unsigned long kStatusChangeCheckMs = 200;
constexpr size_t kStatusDocCapacity = 4096;
constexpr const char* kOtaRebootNamespace = "bb_heavy_blaster";
constexpr const char* kOtaRebootKey = "ota_reboot";

battlebang::esp::app::FirmwareIdentity firmwareIdentity() {
  battlebang::esp::app::FirmwareIdentity identity;
  identity.app = BB_HEAVY_BLASTER_APP_NAME;
  identity.hardware = BB_HEAVY_BLASTER_HARDWARE;
  identity.version = BB_HEAVY_BLASTER_VERSION;
  identity.build = BB_HEAVY_BLASTER_BUILD;
  identity.gitSha = BB_HEAVY_BLASTER_GIT_SHA;
  identity.releaseRepo = BB_HEAVY_BLASTER_RELEASE_REPO;
  identity.latestManifestUrl = BB_HEAVY_BLASTER_LATEST_MANIFEST_URL;
  return identity;
}
}  // namespace

void MqttBus::begin(RuntimeConfig& config, RuntimeConfigStore& store, WifiManager& wifi, HeavyBlasterController& blaster) {
  config_ = &config;
  store_ = &store;
  wifi_ = &wifi;
  blaster_ = &blaster;
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
  if (blaster_ != nullptr && now - lastStatusChangeCheckMs_ >= kStatusChangeCheckMs) {
    lastStatusChangeCheckMs_ = now;
    const String signature = blaster_->statusSignature();
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
      Serial.println("[heavy-blaster][mqtt] missing mqtt.host; provision config first");
    }
    return false;
  }
  const unsigned long now = millis();
  if (lastConnectAttemptMs_ != 0 && now - lastConnectAttemptMs_ < kMqttRetryMs) return false;
  lastConnectAttemptMs_ = now;
  client_.setServer(config_->mqttHost.c_str(), config_->mqttPort);
  const String clientId = String("bb-heavy-blaster-") + config_->deviceId;
  Serial.print("[heavy-blaster][mqtt] connecting to ");
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
    Serial.print("[heavy-blaster][mqtt] connect failed state=");
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
    Serial.print("[heavy-blaster][mqtt] ");
    Serial.print(ok ? "subscribed " : "subscribe failed ");
    Serial.println(topic);
  }
  subscribedTopics_ = topics;
  subscriptionsDirty_ = false;
}

void MqttBus::publishStatus(const char* reason) {
  if (config_ == nullptr || wifi_ == nullptr || blaster_ == nullptr || !client_.connected()) return;
  lastStatusMs_ = millis();
  DynamicJsonDocument doc(kStatusDocCapacity);
  doc["type"] = "status";
  doc["reason"] = reason;
  doc["firmware_app"] = BB_HEAVY_BLASTER_APP_NAME;
  doc["firmware_version"] = BB_HEAVY_BLASTER_VERSION;
  doc["firmware_build"] = BB_HEAVY_BLASTER_BUILD;
  doc["firmware_hardware"] = BB_HEAVY_BLASTER_HARDWARE;
  doc["git_sha"] = BB_HEAVY_BLASTER_GIT_SHA;
  blaster_->appendStatus(doc.as<JsonObject>());
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
  if (topics.blasterStatus.length() > 0) ok = client_.publish(topics.blasterStatus.c_str(), payload.c_str(), false) && ok;
  if (!ok) {
    Serial.print("[heavy-blaster][mqtt] status publish failed len=");
    Serial.print(payload.length());
    Serial.print(" buffer=");
    Serial.println(kPayloadLimit);
  }
  lastStatusSignature_ = blaster_->statusSignature();
}

void MqttBus::handleMessage(char* topic, byte* payload, unsigned int length) {
  if (length == 0) {
    Serial.println("[heavy-blaster][mqtt] ignored empty retained payload");
    return;
  }
  if (length >= kPayloadLimit) {
    Serial.println("[heavy-blaster][mqtt] payload too large; dropped");
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
    Serial.print("[heavy-blaster][config] rejected: ");
    Serial.println(error);
    publishStatus("config_rejected");
    return;
  }
  const RuntimeConfig previous = *config_;
  const bool wifiChanged = connectivityConfigChanged(previous, next);
  const bool mqttChanged = mqttIdentityConfigChanged(previous, next);
  const bool resetState = visualConfigChanged(previous, next) || hardwareProfileChanged(previous, next);
  *config_ = next;
  const bool saved = store_->save(*config_);
  blaster_->applyConfig(*config_, resetState, "mqtt_config");
  if (wifiChanged && wifi_ != nullptr) wifi_->begin(*config_);
  if (mqttChanged) {
    reconfigure();
  } else {
    subscriptionsDirty_ = true;
  }
  publishStatus(saved ? "config_applied" : "config_applied_save_failed");
}

void MqttBus::handleCommandPayload(const char* topic, const char* payload) {
  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.print("[heavy-blaster][command] invalid json: ");
    Serial.println(err.c_str());
    publishStatus("command_rejected");
    return;
  }
  const char* command = doc["command"] | doc["type"] | "";
  const char* blasterId = doc["blaster_id"] | "";
  const char* requestId = doc["request_id"] | "";
  if (blasterId[0] != '\0' && config_->blasterId != blasterId) {
    Serial.print("[heavy-blaster][command] ignored blaster_id=");
    Serial.print(blasterId);
    Serial.print(" expected=");
    Serial.println(config_->blasterId);
    publishStatus("command_rejected");
    return;
  }
  if (requestId[0] != '\0' && lastRequestId_ == requestId) {
    Serial.print("[heavy-blaster][command] duplicate request_id ignored: ");
    Serial.println(requestId);
    publishStatus("command_duplicate");
    return;
  }
  if (requestId[0] != '\0') lastRequestId_ = requestId;

  if (strcmp(command, "status") == 0) {
    publishStatus("command_status");
    return;
  }
  if (strcmp(command, "reset") == 0 || strcmp(command, "lock") == 0) {
    blaster_->reset("mqtt");
    publishStatus("command_reset");
    return;
  }
  if (strcmp(command, "unlock") == 0 || strcmp(command, "activate") == 0) {
    if (!blaster_->unlock("mqtt")) {
      publishStatus("command_rejected");
      return;
    }
    publishStatus("command_unlock");
    return;
  }
  if (strcmp(command, "set_slot") == 0) {
    const int index = doc["slot_index"] | -1;
    if (index < 0 || index >= ::heavy_blaster::kSlotCount || !doc["active"].is<bool>()) {
      publishStatus("command_rejected");
      return;
    }
    if (!blaster_->setSlot(static_cast<uint8_t>(index), doc["active"].as<bool>(), "mqtt")) {
      publishStatus("command_rejected");
      return;
    }
    publishStatus("command_set_slot");
    return;
  }
  if (strcmp(command, "set_slots") == 0) {
    bool slots[::heavy_blaster::kSlotCount] = {false, false, false, false};
    uint8_t count = 0;
    JsonArrayConst arr = doc["slots"].as<JsonArrayConst>();
    if (!arr.isNull()) {
      for (JsonVariantConst value : arr) {
        if (count >= ::heavy_blaster::kSlotCount) break;
        slots[count++] = value.as<bool>();
      }
    } else if (doc["bitmask"].is<unsigned int>() || doc["mask"].is<unsigned int>()) {
      const uint8_t mask = static_cast<uint8_t>(doc["bitmask"] | doc["mask"] | 0);
      count = ::heavy_blaster::kSlotCount;
      for (uint8_t i = 0; i < ::heavy_blaster::kSlotCount; ++i) slots[i] = (mask & (1U << i)) != 0;
    }
    if (count == 0 || !blaster_->setSlots(slots, count, "mqtt")) {
      publishStatus("command_rejected");
      return;
    }
    publishStatus("command_set_slots");
    return;
  }

  Serial.print("[heavy-blaster][command] unsupported command on ");
  Serial.print(topic);
  Serial.print(": ");
  Serial.println(command);
  publishStatus("command_rejected");
}

void MqttBus::handleOtaPayload(const char* payload) {
  battlebang::esp::ota::OtaManifest manifest;
  String error;
  if (!battlebang::esp::ota::parseManifestJson(payload, manifest, error)) {
    Serial.print("[heavy-blaster][ota] rejected manifest: ");
    Serial.println(error);
    publishStatus("ota_manifest_rejected");
    return;
  }

  String reason;
  if (!battlebang::esp::ota::shouldApplyManifest(manifest, firmwareIdentity(), reason)) {
    Serial.print("[heavy-blaster][ota] skipped: ");
    Serial.println(reason);
    publishStatus("ota_skipped");
    return;
  }

  if (config_->otaApplyOnlyInSafeState && blaster_ != nullptr && !blaster_->isSafeForOta()) {
    Serial.println("[heavy-blaster][ota] deferred: blaster is not in a safe OTA state");
    publishStatus("ota_deferred");
    return;
  }

  Serial.print("[heavy-blaster][ota] accepted ");
  Serial.println(battlebang::esp::ota::manifestSummary(manifest));
  publishStatus("ota_downloading");
  if (blaster_ != nullptr) blaster_->prepareForOta();
  battlebang::esp::ota::OtaResult result = battlebang::esp::ota::runHttpOta(manifest);
  Serial.print("[heavy-blaster][ota] result ok=");
  Serial.print(result.ok ? "yes" : "no");
  Serial.print(" message=");
  Serial.println(result.message);
  publishStatus(result.ok ? "ota_rebooting" : "ota_failed");
  if (result.ok) {
    battlebang::esp::ota::writeRebootMarker(kOtaRebootNamespace, kOtaRebootKey, true);
    delay(500);
    ESP.restart();
  } else if (blaster_ != nullptr) {
    blaster_->recoverFromFailedOta("mqtt_ota_failed");
  }
}

}  // namespace heavy_blaster
}  // namespace battlebang
