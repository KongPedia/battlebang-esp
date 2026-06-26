#include <WiFi.h>

#include "go2_nixo/mqtt/hit_mqtt_client.h"

#include <bb_esp_core/config/string_buffer.h>
#include <bb_esp_core/mqtt/device_topics.h>
#include <bb_esp_core/mqtt/topic_utils.h>

namespace go2 {

HitMqttClient* HitMqttClient::instance_ = nullptr;

namespace {

uint16_t macSuffix() {
  return static_cast<uint16_t>(ESP.getEfuseMac() & 0xFFFF);
}

void formatMacSuffix(char* buffer, size_t length) {
  snprintf(buffer, length, "%04X", macSuffix());
}

void addSourceMetadata(JsonDocument& doc, const char* clientId) {
  char suffix[5];
  formatMacSuffix(suffix, sizeof(suffix));

  doc["firmware"] = FIRMWARE_NAME;
  doc["firmware_role"] = FIRMWARE_ROLE;
  doc["mac_suffix"] = suffix;
  doc["client_id"] = clientId;

  JsonObject metadata = doc.createNestedObject("metadata");
  metadata["firmware"] = FIRMWARE_NAME;
  metadata["firmware_role"] = FIRMWARE_ROLE;
  metadata["mac_suffix"] = suffix;
  metadata["client_id"] = clientId;
}

void copyStringOrWarn(const char* label, const String& value, char* buffer, size_t length) {
  if (!battlebang::esp::config::copyToFixedBuffer(value, buffer, length)) {
    Serial.printf("[CONFIG] %s truncated length=%u capacity=%u\n",
                  label,
                  static_cast<unsigned int>(value.length()),
                  static_cast<unsigned int>(length));
  }
}

void warnIfFormatTruncated(const char* label, int written, size_t capacity) {
  if (written < 0 || static_cast<size_t>(written) >= capacity) {
    Serial.printf("[CONFIG] %s truncated formatted_length=%d capacity=%u\n",
                  label,
                  written,
                  static_cast<unsigned int>(capacity));
  }
}

}  // namespace

void HitMqttClient::begin(BarDisplayHandler barHandler) {
  begin(runtimeConfigFromBuild(), barHandler);
}

void HitMqttClient::begin(const RuntimeConfig& config, BarDisplayHandler barHandler) {
  if (mqttClient_.connected()) mqttClient_.disconnect();
  barHandler_ = barHandler;
  copyStringOrWarn("robot_id", config.hit.robotId, robotId_, sizeof(robotId_));
  copyStringOrWarn("wifi.ssid", config.common.wifiSsid, wifiSsid_, sizeof(wifiSsid_));
  copyStringOrWarn("wifi.password", config.common.wifiPassword, wifiPassword_, sizeof(wifiPassword_));
  copyStringOrWarn("mqtt.host", config.common.mqttHost, mqttHost_, sizeof(mqttHost_));
  copyStringOrWarn("mqtt.username", config.common.mqttUsername, mqttUsername_, sizeof(mqttUsername_));
  copyStringOrWarn("mqtt.password", config.common.mqttPassword, mqttPassword_, sizeof(mqttPassword_));
  mqttPort_ = config.common.mqttPort;
  networkConfigured_ = config.common.configured;
  offlineQueueCapacity_ = static_cast<uint8_t>(config.hit.offlineQueueCapacity);
  if (offlineQueueCapacity_ == 0 || offlineQueueCapacity_ > OFFLINE_HIT_QUEUE_CAPACITY) {
    offlineQueueCapacity_ = OFFLINE_HIT_QUEUE_CAPACITY;
  }
  offlineQueueFlushIntervalMs_ = config.hit.offlineQueueFlushIntervalMs;
  if (offlineQueueFlushIntervalMs_ == 0) {
    offlineQueueFlushIntervalMs_ = OFFLINE_HIT_QUEUE_FLUSH_INTERVAL_MS;
  }
  if (offlineQueueCount_ > offlineQueueCapacity_) clearOfflineQueue();

  const String eventTopic = battlebang::esp::mqtt::joinTopic(config.hit.hitTopicPrefix, config.hit.robotId, "events");
  const String ringCommandTopic = battlebang::esp::mqtt::joinTopic(
      config.hit.hitTopicPrefix, config.hit.robotId, "ring_display", "command");
  const battlebang::esp::mqtt::DeviceTopics deviceTopics =
      battlebang::esp::mqtt::makeDeviceTopics(config.common.mqttRoot, "go2_nixo", config.common.deviceId);
  copyStringOrWarn("mqtt.event_topic", eventTopic, eventTopic_, sizeof(eventTopic_));
  copyStringOrWarn("mqtt.ring_command_topic", ringCommandTopic, ringCommandTopic_, sizeof(ringCommandTopic_));
  copyStringOrWarn("mqtt.device_status_topic", deviceTopics.status, deviceStatusTopic_, sizeof(deviceStatusTopic_));
  copyStringOrWarn("mqtt.device_config_topic", deviceTopics.config, deviceConfigTopic_, sizeof(deviceConfigTopic_));
  copyStringOrWarn("mqtt.device_ota_topic", deviceTopics.ota, deviceOtaTopic_, sizeof(deviceOtaTopic_));
  char suffix[5];
  formatMacSuffix(suffix, sizeof(suffix));
  int clientIdLength = snprintf(clientId_, sizeof(clientId_), "battlebang-hit-%s-%s-%s", robotId_, FIRMWARE_NAME, suffix);
  warnIfFormatTruncated("mqtt.client_id", clientIdLength, sizeof(clientId_));
  mqttClient_.setBufferSize(MQTT_BUFFER_SIZE);
  mqttClient_.setCallback(HitMqttClient::mqttMessageCallback);
  instance_ = this;

  if (!configured()) return;
  WiFi.persistent(false);
  WiFi.disconnect(false);
  WiFi.mode(WIFI_STA);
  lastWiFiRetryMs_ = millis() - WIFI_RETRY_INTERVAL_MS;
  ensureWiFiConnected(millis());
}

void HitMqttClient::tick(uint32_t now, bool remoteDisplayActive) {
  ensureWiFiConnected(now);
  ensureMqttConnected(now);
  if (!mqttClient_.connected()) return;
  mqttClient_.loop();
  flushOfflineQueue(now);
  publishHeartbeat(now, remoteDisplayActive);
}

void HitMqttClient::setManagementHandlers(ManagementMessageHandler configHandler, ManagementMessageHandler otaHandler) {
  configHandler_ = configHandler;
  otaHandler_ = otaHandler;
}

bool HitMqttClient::configured() const {
  return networkConfigured_;
}

bool HitMqttClient::connected() {
  return mqttClient_.connected();
}

bool HitMqttClient::publishHitEvent(int targetId,
                                     uint32_t sequence,
                                     uint32_t firmwareTsMs,
                                     bool queued,
                                     uint32_t queuedForMs,
                                     uint8_t queueDepth,
                                     int peakRaw,
                                     int thresholdRaw,
                                     uint16_t acceptedHitCount,
                                     uint16_t hpRemaining,
                                     uint16_t maxHits,
                                     bool down) {
  if (!mqttClient_.connected()) return false;
  if (maxHits < 1) maxHits = 1;
  if (hpRemaining > maxHits) hpRemaining = maxHits;

  DynamicJsonDocument doc(MQTT_BUFFER_SIZE);
  doc["schema_version"] = 2;
  doc["event"] = "hit_event";
  doc["robot_id"] = robotId_;
  doc["sensor_id"] = targetIdToSensorId(targetId);
  doc["sequence"] = sequence;
  doc["hit"] = true;
  doc["accepted"] = true;
  doc["accepted_hit_count"] = acceptedHitCount;
  doc["hp_remaining"] = hpRemaining;
  doc["max_hits"] = maxHits;
  doc["down"] = down;
  doc["ring_fill_ratio"] = static_cast<float>(hpRemaining) / static_cast<float>(maxHits);
  doc["firmware_ts_ms"] = firmwareTsMs;
  addSourceMetadata(doc, clientId_);
  JsonObject metadata = doc["metadata"].as<JsonObject>();
  metadata["hit_source"] = "piezo_ao_adc_threshold";
  metadata["decision_owner"] = "esp_local";
  metadata["display_owner"] = "esp_local";
  metadata["hp_current"] = hpRemaining;
  metadata["hp_max"] = maxHits;
  if (peakRaw >= 0) {
    doc["peak"] = peakRaw;
    metadata["adc_peak_raw"] = peakRaw;
  }
  if (thresholdRaw >= 0) {
    doc["threshold"] = thresholdRaw;
    metadata["adc_threshold_raw"] = thresholdRaw;
  }

  if (queued) {
    doc["queued"] = true;
    doc["queued_for_ms"] = queuedForMs;
    metadata["queued"] = true;
    metadata["queued_for_ms"] = queuedForMs;
    metadata["queue_depth"] = queueDepth;
    metadata["queue_dropped"] = offlineQueueDropped_;
  }

  String buffer;
  buffer.reserve(MQTT_BUFFER_SIZE);
  size_t size = serializeJson(doc, buffer);
  bool ok = mqttClient_.publish(eventTopic_, reinterpret_cast<const uint8_t*>(buffer.c_str()), size, false);
  if (ok) {
    Serial.printf("[HIT] published hit_event seq=%lu target=%d hp=%u/%u down=%s peak=%d threshold=%d queued=%s topic=%s\n",
                  (unsigned long)sequence,
                  targetId,
                  hpRemaining,
                  maxHits,
                  down ? "true" : "false",
                  peakRaw,
                  thresholdRaw,
                  queued ? "true" : "false",
                  eventTopic_);
  } else {
    Serial.printf("[HIT] hit_event publish failed seq=%lu target=%d hp=%u/%u down=%s peak=%d threshold=%d queued=%s\n",
                  (unsigned long)sequence,
                  targetId,
                  hpRemaining,
                  maxHits,
                  down ? "true" : "false",
                  peakRaw,
                  thresholdRaw,
                  queued ? "true" : "false");
  }
  return ok;
}

void HitMqttClient::queueHitEvent(int targetId,
                                  uint32_t sequence,
                                  uint32_t firmwareTsMs,
                                  int peakRaw,
                                  int thresholdRaw,
                                  uint16_t acceptedHitCount,
                                  uint16_t hpRemaining,
                                  uint16_t maxHits,
                                  bool down) {
  if (offlineQueueCount_ >= offlineQueueCapacity_) {
    QueuedHitEvent dropped = offlineQueue_[offlineQueueHead_];
    offlineQueueHead_ = (offlineQueueHead_ + 1) % offlineQueueCapacity_;
    offlineQueueCount_--;
    offlineQueueDropped_++;
    Serial.printf("[HIT] offline queue full; dropped oldest seq=%lu target=%d dropped_total=%lu\n",
                  (unsigned long)dropped.sequence,
                  dropped.targetId,
                  (unsigned long)offlineQueueDropped_);
  }

  uint8_t insertIndex = (offlineQueueHead_ + offlineQueueCount_) % offlineQueueCapacity_;
  QueuedHitEvent queued;
  queued.targetId = targetId;
  queued.sequence = sequence;
  queued.firmwareTsMs = firmwareTsMs;
  queued.peakRaw = peakRaw;
  queued.thresholdRaw = thresholdRaw;
  queued.acceptedHitCount = acceptedHitCount;
  queued.hpRemaining = hpRemaining;
  queued.maxHits = maxHits < 1 ? 1 : maxHits;
  queued.down = down;
  offlineQueue_[insertIndex] = queued;
  offlineQueueCount_++;
  Serial.printf("[HIT] queued offline hit_event seq=%lu target=%d hp=%u/%u down=%s peak=%d threshold=%d queue=%u/%u\n",
                (unsigned long)sequence,
                targetId,
                hpRemaining,
                queued.maxHits,
                down ? "true" : "false",
                peakRaw,
                thresholdRaw,
                offlineQueueCount_,
                (unsigned int)offlineQueueCapacity_);
}

void HitMqttClient::clearOfflineQueue() {
  offlineQueueHead_ = 0;
  offlineQueueCount_ = 0;
  offlineQueueDropped_ = 0;
  lastOfflineQueueFlushMs_ = 0;
}

uint8_t HitMqttClient::offlineQueueCount() const {
  return offlineQueueCount_;
}

bool HitMqttClient::publishDeviceStatus(const char* payload) {
  if (!mqttClient_.connected() || deviceStatusTopic_[0] == '\0') return false;
  return mqttClient_.publish(deviceStatusTopic_, payload, false);
}

const char* HitMqttClient::eventTopic() const {
  return eventTopic_;
}

const char* HitMqttClient::ringCommandTopic() const {
  return ringCommandTopic_;
}

const char* HitMqttClient::deviceStatusTopic() const {
  return deviceStatusTopic_;
}

const char* HitMqttClient::deviceConfigTopic() const {
  return deviceConfigTopic_;
}

const char* HitMqttClient::deviceOtaTopic() const {
  return deviceOtaTopic_;
}

void HitMqttClient::mqttMessageCallback(char* topic, byte* payload, unsigned int length) {
  if (instance_ == nullptr) return;
  instance_->handleMqttMessage(topic, payload, length);
}

void HitMqttClient::handleMqttMessage(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, deviceConfigTopic_) == 0) {
    if (configHandler_ != nullptr) configHandler_(topic, payload, length);
    return;
  }
  if (strcmp(topic, deviceOtaTopic_) == 0) {
    if (otaHandler_ != nullptr) otaHandler_(topic, payload, length);
    return;
  }
  if (strcmp(topic, ringCommandTopic_) != 0) return;

  StaticJsonDocument<MQTT_BUFFER_SIZE> doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    Serial.printf("[MQTT] invalid ring JSON: %s\n", error.c_str());
    return;
  }

  const char* command = doc["command"] | "";
  if (strcmp(command, "ring_display") != 0) return;
  const char* robotId = doc["robot_id"] | robotId_;
  if (strcmp(robotId, robotId_) != 0) {
    Serial.printf("[MQTT] ring command ignored for robot_id=%s local=%s\n", robotId, robotId_);
    return;
  }

  BarDisplayUpdate update;
  update.fillRatio = doc["ring_fill_ratio"] | 1.0f;
  update.mode = String(doc["ring_display_mode"] | "idle");
  update.down = doc["down"] | false;
  update.ttlMs = doc["ttl_ms"] | 1000;
  update.resetHitState = doc["reset_hit_state"] | false;
  update.debugOverride = (doc["debug_override"] | false) || (doc["maintenance_override"] | false);

  if (!update.resetHitState && !update.debugOverride) {
    Serial.println("[MQTT] ring command ignored: ESP owns local HP bar; set reset_hit_state or debug_override for maintenance");
    return;
  }
  if (barHandler_ != nullptr) barHandler_(update);

  Serial.printf("[MQTT] ring command mode=%s fill=%.3f down=%s ttl=%lu reset_hit_state=%s debug_override=%s\n",
                update.mode.c_str(),
                constrain(update.fillRatio, 0.0f, 1.0f),
                update.down ? "true" : "false",
                (unsigned long)update.ttlMs,
                update.resetHitState ? "true" : "false",
                update.debugOverride ? "true" : "false");
}

void HitMqttClient::ensureWiFiConnected(uint32_t now) {
  if (!configured()) return;
  if (WiFi.status() == WL_CONNECTED) return;
  if (now - lastWiFiRetryMs_ < WIFI_RETRY_INTERVAL_MS) return;
  lastWiFiRetryMs_ = now;

  Serial.printf("[WIFI] connecting ssid=%s\n", wifiSsid_);
  WiFi.disconnect(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSsid_, wifiPassword_);
}

void HitMqttClient::ensureMqttConnected(uint32_t now) {
  if (!configured()) return;
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqttClient_.connected()) return;
  if (now - lastMqttRetryMs_ < MQTT_RETRY_INTERVAL_MS) return;
  lastMqttRetryMs_ = now;

  mqttClient_.setServer(mqttHost_, mqttPort_);
  const bool useAuth = mqttUsername_[0] != '\0' || mqttPassword_[0] != '\0';
  Serial.printf("[MQTT] connecting host=%s port=%u client_id=%s auth=%s\n",
                mqttHost_,
                mqttPort_,
                clientId_,
                useAuth ? "yes" : "no");
  const bool connected = useAuth
                             ? mqttClient_.connect(clientId_, mqttUsername_, mqttPassword_)
                             : mqttClient_.connect(clientId_);
  if (!connected) {
    Serial.printf("[MQTT] connect failed state=%d\n", mqttClient_.state());
    return;
  }

  bool ok = mqttClient_.subscribe(ringCommandTopic_, 1);
  Serial.printf("[MQTT] %s %s\n", ok ? "subscribed" : "subscribe failed", ringCommandTopic_);
  ok = mqttClient_.subscribe(deviceConfigTopic_, 1);
  Serial.printf("[MQTT] %s %s\n", ok ? "subscribed" : "subscribe failed", deviceConfigTopic_);
  ok = mqttClient_.subscribe(deviceOtaTopic_, 1);
  Serial.printf("[MQTT] %s %s\n", ok ? "subscribed" : "subscribe failed", deviceOtaTopic_);
}

void HitMqttClient::flushOfflineQueue(uint32_t now) {
  if (offlineQueueCount_ == 0) return;
  if (!mqttClient_.connected()) return;
  if (now - lastOfflineQueueFlushMs_ < offlineQueueFlushIntervalMs_) return;

  QueuedHitEvent candidate = offlineQueue_[offlineQueueHead_];
  uint32_t queuedForMs = now - candidate.firmwareTsMs;
  uint8_t queueDepthBeforePublish = offlineQueueCount_;
  if (!publishHitEvent(candidate.targetId,
                       candidate.sequence,
                       candidate.firmwareTsMs,
                       true,
                       queuedForMs,
                       queueDepthBeforePublish,
                       candidate.peakRaw,
                       candidate.thresholdRaw,
                       candidate.acceptedHitCount,
                       candidate.hpRemaining,
                       candidate.maxHits,
                       candidate.down)) {
    return;
  }

  popOfflineQueueHead();
  lastOfflineQueueFlushMs_ = now;
  Serial.printf("[HIT] flushed offline hit_event seq=%lu remaining=%u\n",
                (unsigned long)candidate.sequence,
                offlineQueueCount_);
}

void HitMqttClient::popOfflineQueueHead() {
  if (offlineQueueCount_ == 0) return;
  offlineQueueHead_ = (offlineQueueHead_ + 1) % offlineQueueCapacity_;
  offlineQueueCount_--;
}

void HitMqttClient::publishHeartbeat(uint32_t now, bool remoteDisplayActive) {
  if (now - lastHeartbeatTxMs_ < HEARTBEAT_TX_PERIOD_MS) return;
  lastHeartbeatTxMs_ = now;

  DynamicJsonDocument doc(MQTT_BUFFER_SIZE);
  doc["schema_version"] = 1;
  doc["event"] = "heartbeat";
  doc["robot_id"] = robotId_;
  doc["sensor_id"] = "hit_ring";
  doc["sequence"] = ++heartbeatSequence_;
  doc["firmware_ts_ms"] = now;
  doc["mode"] = heartbeatMode(remoteDisplayActive);
  doc["display_owner"] = remoteDisplayActive ? "debug_override" : "esp_local";
  addSourceMetadata(doc, clientId_);
  JsonObject metadata = doc["metadata"].as<JsonObject>();
  metadata["offline_queue_count"] = offlineQueueCount_;
  metadata["offline_queue_capacity"] = offlineQueueCapacity_;
  metadata["offline_queue_dropped"] = offlineQueueDropped_;

  String buffer;
  buffer.reserve(MQTT_BUFFER_SIZE);
  size_t size = serializeJson(doc, buffer);
  mqttClient_.publish(eventTopic_, reinterpret_cast<const uint8_t*>(buffer.c_str()), size, false);
}

const char* HitMqttClient::heartbeatMode(bool remoteDisplayActive) {
  if (remoteDisplayActive) return "direct";
  if (mqttClient_.connected()) return "mqtt_connected";
  return "mqtt_disconnected";
}

}  // namespace go2
