#include "mqtt_bus.h"

#include <ArduinoJson.h>
#include <Esp.h>
#include <bb_esp_ota/http_ota.h>
#include <bb_esp_ota/ota_manifest.h>
#include <bb_esp_ota/reboot_marker.h>

#include "station/app/firmware_info.h"
#include "station/mqtt/topics.h"

namespace battlebang {
namespace station {
namespace {
constexpr size_t kPayloadLimit = 8192;
constexpr unsigned long kMqttRetryMs = 5000;
constexpr unsigned long kStatusChangeCheckMs = 200;
constexpr size_t kStatusDocCapacity = 4096;
constexpr uint16_t kMqttSocketTimeoutSeconds = 1;
constexpr const char* kOtaRebootNamespace = "bb_station";
constexpr const char* kOtaRebootKey = "ota_reboot";

battlebang::esp::app::FirmwareIdentity firmwareIdentity() {
  battlebang::esp::app::FirmwareIdentity identity;
  identity.app = BB_STATION_APP_NAME;
  identity.hardware = BB_STATION_HARDWARE;
  identity.version = BB_STATION_VERSION;
  identity.build = BB_STATION_BUILD;
  identity.gitSha = BB_STATION_GIT_SHA;
  identity.releaseRepo = BB_STATION_RELEASE_REPO;
  identity.latestManifestUrl = BB_STATION_LATEST_MANIFEST_URL;
  return identity;
}
}  // namespace

void MqttBus::begin(RuntimeConfig& config, RuntimeConfigStore& store, WifiManager& wifi, StationController& station) {
  config_ = &config;
  store_ = &store;
  wifi_ = &wifi;
  station_ = &station;
  wifiClient_.setTimeout(kMqttSocketTimeoutSeconds);
  client_.setSocketTimeout(kMqttSocketTimeoutSeconds);
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

void MqttBus::loop(bool deferAutomaticStatus) {
  if (config_ == nullptr || wifi_ == nullptr) return;
  if (!wifi_->connected()) return;
  if (connectIfNeeded()) client_.loop();
  if (deferAutomaticStatus) return;
  const unsigned long now = millis();
  if (station_ != nullptr && now - lastStatusChangeCheckMs_ >= kStatusChangeCheckMs) {
    lastStatusChangeCheckMs_ = now;
    const String signature = station_->statusSignature();
    if (signature != lastStatusSignature_) {
      publishStatus("state_changed");
      return;
    }
  }
  const uint32_t heartbeatMs = config_->gameplay.heartbeatIntervalMs < 1000 ? 1000 : config_->gameplay.heartbeatIntervalMs;
  if (now - lastStatusMs_ >= heartbeatMs) publishStatus("heartbeat");
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
      Serial.println("[station][mqtt] missing mqtt.host; provision config first");
    }
    return false;
  }
  const unsigned long now = millis();
  if (lastConnectAttemptMs_ != 0 && now - lastConnectAttemptMs_ < kMqttRetryMs) return false;
  lastConnectAttemptMs_ = now;
  client_.setServer(config_->mqttHost.c_str(), config_->mqttPort);
  const String clientId = String("bb-station-") + config_->stationId;
  Serial.print("[station][mqtt] connecting to ");
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
    Serial.print("[station][mqtt] connect failed state=");
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
    Serial.print("[station][mqtt] ");
    Serial.print(ok ? "subscribed " : "subscribe failed ");
    Serial.println(topic);
  }
  subscribedTopics_ = topics;
  subscriptionsDirty_ = false;
}

void MqttBus::publishStatus(const char* reason) {
  if (config_ == nullptr || wifi_ == nullptr || station_ == nullptr || !client_.connected()) return;
  lastStatusMs_ = millis();
  DynamicJsonDocument doc(kStatusDocCapacity);
  doc["schema_version"] = 1;
  doc["type"] = "status";
  doc["event"] = reason;
  doc["reason"] = reason;
  doc["device_type"] = "station";
  doc["firmware_app"] = BB_STATION_APP_NAME;
  doc["firmware_version"] = BB_STATION_VERSION;
  doc["firmware_build"] = BB_STATION_BUILD;
  doc["firmware_hardware"] = BB_STATION_HARDWARE;
  doc["git_sha"] = BB_STATION_GIT_SHA;
  station_->appendStatus(doc.as<JsonObject>());
  doc["wifi"] = wifi_->connected() ? "UP" : "DOWN";
  doc["ip"] = wifi_->ip();
  doc["rssi"] = wifi_->rssi();
  doc["mqtt_connected"] = client_.connected();
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
  bool ok = topics.stationStatus.length() > 0 && client_.publish(topics.stationStatus.c_str(), payload.c_str(), false);
  if (!ok) {
    Serial.print("[station][mqtt] status publish failed len=");
    Serial.print(payload.length());
    Serial.print(" buffer=");
    Serial.println(kPayloadLimit);
  }
  lastStatusSignature_ = station_->statusSignature();
}

void MqttBus::handleMessage(char* topic, byte* payload, unsigned int length) {
  if (length == 0) {
    Serial.println("[station][mqtt] ignored empty retained payload");
    return;
  }
  if (length >= kPayloadLimit) {
    Serial.println("[station][mqtt] payload too large; dropped");
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
    Serial.print("[station][config] rejected: ");
    Serial.println(error);
    publishStatus("config_rejected");
    return;
  }
  const RuntimeConfig previous = *config_;
  const bool wifiChanged = connectivityConfigChanged(previous, next);
  const bool mqttChanged = mqttIdentityConfigChanged(previous, next);
  const bool resetState = sensorConfigChanged(previous, next) || visualConfigChanged(previous, next) || hardwareProfileChanged(previous, next);
  *config_ = next;
  const bool saved = store_->save(*config_);
  station_->applyConfig(*config_, resetState, "mqtt_config");
  if (wifiChanged && wifi_ != nullptr) wifi_->begin(*config_);
  if (mqttChanged || wifiChanged) {
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
    Serial.print("[station][command] invalid json: ");
    Serial.println(err.c_str());
    publishStatus("command_rejected");
    return;
  }
  const char* command = doc["command"] | doc["type"] | "";
  const char* stationId = doc["station_id"] | "";
  const char* requestId = doc["request_id"] | "";
  if (stationId[0] != '\0' && config_->stationId != stationId) {
    Serial.print("[station][command] ignored station_id=");
    Serial.print(stationId);
    Serial.print(" expected=");
    Serial.println(config_->stationId);
    publishStatus("command_rejected");
    return;
  }
  if (requestId[0] != '\0' && lastRequestId_ == requestId) {
    Serial.print("[station][command] duplicate request_id ignored: ");
    Serial.println(requestId);
    publishStatus("command_duplicate");
    return;
  }
  if (requestId[0] != '\0') lastRequestId_ = requestId;

  if (strcmp(command, "status") == 0) {
    publishStatus("command_status");
    return;
  }
  if (strcmp(command, "reset") == 0 || strcmp(command, "unlock") == 0 || strcmp(command, "clear") == 0) {
    station_->reset("mqtt");
    publishStatus("command_reset");
    return;
  }
  if (strcmp(command, "simulate_hit") == 0 || strcmp(command, "hit") == 0) {
    if (!config_->debugAllowSimulateHit) {
      publishStatus("command_rejected");
      return;
    }
    const uint16_t peak = doc["peak"].is<unsigned int>() ? doc["peak"].as<uint16_t>() : config_->sensor.hitThreshold;
    station_->simulateHit("mqtt", peak);
    publishStatus("command_simulate_hit");
    return;
  }

  Serial.print("[station][command] unsupported command on ");
  Serial.print(topic);
  Serial.print(": ");
  Serial.println(command);
  publishStatus("command_rejected");
}

void MqttBus::handleOtaPayload(const char* payload) {
  battlebang::esp::ota::OtaManifest manifest;
  String error;
  if (!battlebang::esp::ota::parseManifestJson(payload, manifest, error)) {
    Serial.print("[station][ota] rejected manifest: ");
    Serial.println(error);
    publishStatus("ota_manifest_rejected");
    return;
  }

  String reason;
  if (!battlebang::esp::ota::shouldApplyManifest(manifest, firmwareIdentity(), reason)) {
    Serial.print("[station][ota] skipped: ");
    Serial.println(reason);
    publishStatus("ota_skipped");
    return;
  }

  if (config_->otaApplyOnlyInSafeState && station_ != nullptr && !station_->isSafeForOta()) {
    Serial.println("[station][ota] deferred: station is not in a safe OTA state");
    publishStatus("ota_deferred");
    return;
  }

  Serial.print("[station][ota] accepted ");
  Serial.println(battlebang::esp::ota::manifestSummary(manifest));
  publishStatus("ota_downloading");
  if (station_ != nullptr) station_->prepareForOta();
  battlebang::esp::ota::OtaResult result = battlebang::esp::ota::runHttpOta(manifest);
  Serial.print("[station][ota] result ok=");
  Serial.print(result.ok ? "yes" : "no");
  Serial.print(" message=");
  Serial.println(result.message);
  publishStatus(result.ok ? "ota_rebooting" : "ota_failed");
  if (result.ok) {
    battlebang::esp::ota::writeRebootMarker(kOtaRebootNamespace, kOtaRebootKey, true);
    delay(500);
    ESP.restart();
  } else if (station_ != nullptr) {
    station_->recoverFromFailedOta("mqtt_ota_failed");
  }
}

}  // namespace station
}  // namespace battlebang
