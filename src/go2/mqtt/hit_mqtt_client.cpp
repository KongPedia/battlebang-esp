#include "go2/mqtt/hit_mqtt_client.h"

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

}  // namespace

void HitMqttClient::begin(BarDisplayHandler barHandler, NixoFireMirrorHandler nixoFireMirrorHandler) {
  barHandler_ = barHandler;
  nixoFireMirrorHandler_ = nixoFireMirrorHandler;
  snprintf(eventTopic_, sizeof(eventTopic_), "%s/%s/events", MQTT_TOPIC_PREFIX, ROBOT_ID);
  snprintf(ringCommandTopic_, sizeof(ringCommandTopic_), "%s/%s/ring_display/command", MQTT_TOPIC_PREFIX, ROBOT_ID);
  snprintf(nixoCommandTopic_, sizeof(nixoCommandTopic_), "%s/%s/command", NIXO_MQTT_TOPIC_PREFIX_VALUE, NIXO_ID_VALUE);
  char suffix[5];
  formatMacSuffix(suffix, sizeof(suffix));
  snprintf(clientId_, sizeof(clientId_), "battlebang-hit-%s-%s-%s", ROBOT_ID, FIRMWARE_NAME, suffix);
  mqttClient_.setBufferSize(MQTT_BUFFER_SIZE);
  mqttClient_.setCallback(HitMqttClient::mqttMessageCallback);
  instance_ = this;

  if (!configured()) return;
  WiFi.persistent(false);
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

bool HitMqttClient::configured() const {
  return WIFI_SSID[0] != '\0' && MQTT_HOST[0] != '\0';
}

bool HitMqttClient::connected() {
  return mqttClient_.connected();
}

bool HitMqttClient::publishHitCandidate(int targetId,
                                        bool hit,
                                        uint32_t sequence,
                                        uint32_t firmwareTsMs,
                                        bool queued,
                                        uint32_t queuedForMs,
                                        uint8_t queueDepth,
                                        int peakRaw,
                                        int thresholdRaw) {
  if (!mqttClient_.connected()) return false;

  StaticJsonDocument<MQTT_BUFFER_SIZE> doc;
  doc["schema_version"] = 1;
  doc["event"] = "hit_candidate";
  doc["robot_id"] = ROBOT_ID;
  doc["sensor_id"] = targetIdToSensorId(targetId);
  doc["sequence"] = sequence;
  doc["hit"] = hit;
  doc["firmware_ts_ms"] = firmwareTsMs;
  addSourceMetadata(doc, clientId_);
  JsonObject metadata = doc["metadata"].as<JsonObject>();
  if (peakRaw >= 0) {
    doc["peak"] = peakRaw;
    metadata["adc_peak_raw"] = peakRaw;
  }
  if (thresholdRaw >= 0) {
    doc["threshold"] = thresholdRaw;
    metadata["adc_threshold_raw"] = thresholdRaw;
  }
  if (peakRaw >= 0 || thresholdRaw >= 0) {
    metadata["hit_source"] = "piezo_ao_adc_threshold";
  }

  if (queued) {
    doc["queued"] = true;
    doc["queued_for_ms"] = queuedForMs;
    metadata["queued"] = true;
    metadata["queued_for_ms"] = queuedForMs;
    metadata["queue_depth"] = queueDepth;
    metadata["queue_dropped"] = offlineQueueDropped_;
  }

  char buffer[MQTT_BUFFER_SIZE];
  size_t size = serializeJson(doc, buffer, sizeof(buffer));
  bool ok = mqttClient_.publish(eventTopic_, reinterpret_cast<const uint8_t*>(buffer), size, false);
  if (ok) {
    Serial.printf("[HIT] published candidate seq=%lu target=%d hit=%s peak=%d threshold=%d queued=%s topic=%s\n",
                  (unsigned long)sequence,
                  targetId,
                  hit ? "true" : "false",
                  peakRaw,
                  thresholdRaw,
                  queued ? "true" : "false",
                  eventTopic_);
  } else {
    Serial.printf("[HIT] candidate publish failed seq=%lu target=%d hit=%s peak=%d threshold=%d queued=%s\n",
                  (unsigned long)sequence,
                  targetId,
                  hit ? "true" : "false",
                  peakRaw,
                  thresholdRaw,
                  queued ? "true" : "false");
  }
  return ok;
}

void HitMqttClient::queueHitCandidate(int targetId,
                                      bool hit,
                                      uint32_t sequence,
                                      uint32_t firmwareTsMs,
                                      int peakRaw,
                                      int thresholdRaw) {
  if (!hit) return;

  if (offlineQueueCount_ >= OFFLINE_HIT_QUEUE_CAPACITY) {
    QueuedHitCandidate dropped = offlineQueue_[offlineQueueHead_];
    offlineQueueHead_ = (offlineQueueHead_ + 1) % OFFLINE_HIT_QUEUE_CAPACITY;
    offlineQueueCount_--;
    offlineQueueDropped_++;
    Serial.printf("[HIT] offline queue full; dropped oldest seq=%lu target=%d dropped_total=%lu\n",
                  (unsigned long)dropped.sequence,
                  dropped.targetId,
                  (unsigned long)offlineQueueDropped_);
  }

  uint8_t insertIndex = (offlineQueueHead_ + offlineQueueCount_) % OFFLINE_HIT_QUEUE_CAPACITY;
  QueuedHitCandidate queued;
  queued.targetId = targetId;
  queued.hit = hit;
  queued.sequence = sequence;
  queued.firmwareTsMs = firmwareTsMs;
  queued.peakRaw = peakRaw;
  queued.thresholdRaw = thresholdRaw;
  offlineQueue_[insertIndex] = queued;
  offlineQueueCount_++;
  Serial.printf("[HIT] queued offline candidate seq=%lu target=%d peak=%d threshold=%d queue=%u/%u\n",
                (unsigned long)sequence,
                targetId,
                peakRaw,
                thresholdRaw,
                offlineQueueCount_,
                (unsigned int)OFFLINE_HIT_QUEUE_CAPACITY);
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

const char* HitMqttClient::eventTopic() const {
  return eventTopic_;
}

const char* HitMqttClient::ringCommandTopic() const {
  return ringCommandTopic_;
}

const char* HitMqttClient::nixoCommandTopic() const {
  return nixoCommandTopic_;
}

void HitMqttClient::mqttMessageCallback(char* topic, byte* payload, unsigned int length) {
  if (instance_ == nullptr) return;
  instance_->handleMqttMessage(topic, payload, length);
}

void HitMqttClient::handleMqttMessage(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, nixoCommandTopic_) == 0) {
    handleNixoCommandMessage(payload, length);
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
  const char* robotId = doc["robot_id"] | ROBOT_ID;
  if (strcmp(robotId, ROBOT_ID) != 0) {
    Serial.printf("[MQTT] ring command ignored for robot_id=%s local=%s\n", robotId, ROBOT_ID);
    return;
  }

  BarDisplayUpdate update;
  update.fillRatio = doc["ring_fill_ratio"] | 1.0f;
  update.mode = String(doc["ring_display_mode"] | "idle");
  update.down = doc["down"] | false;
  update.ttlMs = doc["ttl_ms"] | 1000;
  update.resetHitState = doc["reset_hit_state"] | false;
  if (barHandler_ != nullptr) barHandler_(update);

  Serial.printf("[MQTT] ring command mode=%s fill=%.3f down=%s ttl=%lu reset_hit_state=%s\n",
                update.mode.c_str(),
                constrain(update.fillRatio, 0.0f, 1.0f),
                update.down ? "true" : "false",
                (unsigned long)update.ttlMs,
                update.resetHitState ? "true" : "false");
}


void HitMqttClient::handleNixoCommandMessage(byte* payload, unsigned int length) {
  if (nixoFireMirrorHandler_ == nullptr) return;
  if (length == 0) {
    Serial.println("[NIXO MON] ignored empty retained command clear");
    return;
  }

  StaticJsonDocument<MQTT_BUFFER_SIZE> doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    Serial.printf("[NIXO MON] invalid JSON: %s\n", error.c_str());
    return;
  }

  const int schemaVersion = doc["schema_version"] | 0;
  const char* command = doc["command"] | "";
  const char* nixoId = doc["nixo_id"] | "";
  const char* requestId = doc["request_id"] | "";

  if (schemaVersion != 1 || strcmp(command, "fire") != 0) return;
  if (strcmp(nixoId, NIXO_ID_VALUE) != 0) {
    Serial.printf("[NIXO MON] ignored nixo_id=%s expected=%s\n", nixoId, NIXO_ID_VALUE);
    return;
  }
  if (requestId[0] == '\0') {
    Serial.println("[NIXO MON] ignored fire command without request_id");
    return;
  }
  if (!doc["enabled"].is<bool>()) {
    Serial.println("[NIXO MON] ignored fire command without boolean enabled");
    return;
  }
  if (lastNixoFireRequestId_ == requestId) {
    Serial.printf("[NIXO MON] duplicate request_id=%s ignored\n", requestId);
    return;
  }

  lastNixoFireRequestId_ = requestId;
  const bool enabled = doc["enabled"].as<bool>();
  if (!enabled) {
    nixoFireMirrorHandler_(false, 0, NIXO_FIRE_COOLDOWN_MS);
    Serial.printf("[NIXO MON] fire mirror stop request_id=%s topic=%s\n", requestId, nixoCommandTopic_);
    return;
  }

  uint32_t durationMs = doc["duration_ms"] | NIXO_FIRE_DEFAULT_DURATION_MS;
  durationMs = constrain(durationMs, NIXO_FIRE_MIN_DURATION_MS, NIXO_FIRE_MAX_DURATION_MS);

  nixoFireMirrorHandler_(true, durationMs, NIXO_FIRE_COOLDOWN_MS);
  Serial.printf("[NIXO MON] fire mirror request_id=%s fire_duration_ms=%lu cooldown_ms=%lu topic=%s\n",
                requestId,
                (unsigned long)durationMs,
                (unsigned long)NIXO_FIRE_COOLDOWN_MS,
                nixoCommandTopic_);
}

void HitMqttClient::ensureWiFiConnected(uint32_t now) {
  if (!configured()) return;
  if (WiFi.status() == WL_CONNECTED) return;
  if (now - lastWiFiRetryMs_ < WIFI_RETRY_INTERVAL_MS) return;
  lastWiFiRetryMs_ = now;

  Serial.printf("[WIFI] connecting ssid=%s\n", WIFI_SSID);
  WiFi.disconnect(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void HitMqttClient::ensureMqttConnected(uint32_t now) {
  if (!configured()) return;
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqttClient_.connected()) return;
  if (now - lastMqttRetryMs_ < MQTT_RETRY_INTERVAL_MS) return;
  lastMqttRetryMs_ = now;

  mqttClient_.setServer(MQTT_HOST, MQTT_PORT);
  Serial.printf("[MQTT] connecting host=%s port=%u client_id=%s\n", MQTT_HOST, MQTT_PORT, clientId_);
  if (!mqttClient_.connect(clientId_)) {
    Serial.printf("[MQTT] connect failed state=%d\n", mqttClient_.state());
    return;
  }

  bool ok = mqttClient_.subscribe(ringCommandTopic_, 1);
  Serial.printf("[MQTT] %s %s\n", ok ? "subscribed" : "subscribe failed", ringCommandTopic_);
  if (nixoFireMirrorHandler_ != nullptr) {
    bool nixoOk = mqttClient_.subscribe(nixoCommandTopic_, 1);
    Serial.printf("[MQTT] %s nixo cooldown %s\n",
                  nixoOk ? "subscribed" : "subscribe failed",
                  nixoCommandTopic_);
  }
}

void HitMqttClient::flushOfflineQueue(uint32_t now) {
  if (offlineQueueCount_ == 0) return;
  if (!mqttClient_.connected()) return;
  if (now - lastOfflineQueueFlushMs_ < OFFLINE_HIT_QUEUE_FLUSH_INTERVAL_MS) return;

  QueuedHitCandidate candidate = offlineQueue_[offlineQueueHead_];
  uint32_t queuedForMs = now - candidate.firmwareTsMs;
  uint8_t queueDepthBeforePublish = offlineQueueCount_;
  if (!publishHitCandidate(candidate.targetId,
                           candidate.hit,
                           candidate.sequence,
                           candidate.firmwareTsMs,
                           true,
                           queuedForMs,
                           queueDepthBeforePublish,
                           candidate.peakRaw,
                           candidate.thresholdRaw)) {
    return;
  }

  popOfflineQueueHead();
  lastOfflineQueueFlushMs_ = now;
  Serial.printf("[HIT] flushed offline candidate seq=%lu remaining=%u\n",
                (unsigned long)candidate.sequence,
                offlineQueueCount_);
}

void HitMqttClient::popOfflineQueueHead() {
  if (offlineQueueCount_ == 0) return;
  offlineQueueHead_ = (offlineQueueHead_ + 1) % OFFLINE_HIT_QUEUE_CAPACITY;
  offlineQueueCount_--;
}

void HitMqttClient::publishHeartbeat(uint32_t now, bool remoteDisplayActive) {
  if (now - lastHeartbeatTxMs_ < HEARTBEAT_TX_PERIOD_MS) return;
  lastHeartbeatTxMs_ = now;

  StaticJsonDocument<MQTT_BUFFER_SIZE> doc;
  doc["schema_version"] = 1;
  doc["event"] = "heartbeat";
  doc["robot_id"] = ROBOT_ID;
  doc["sensor_id"] = "hit_ring";
  doc["sequence"] = ++heartbeatSequence_;
  doc["firmware_ts_ms"] = now;
  doc["mode"] = heartbeatMode(remoteDisplayActive);
  addSourceMetadata(doc, clientId_);
  JsonObject metadata = doc["metadata"].as<JsonObject>();
  metadata["offline_queue_count"] = offlineQueueCount_;
  metadata["offline_queue_capacity"] = OFFLINE_HIT_QUEUE_CAPACITY;
  metadata["offline_queue_dropped"] = offlineQueueDropped_;

  char buffer[MQTT_BUFFER_SIZE];
  size_t size = serializeJson(doc, buffer, sizeof(buffer));
  mqttClient_.publish(eventTopic_, reinterpret_cast<const uint8_t*>(buffer), size, false);
}

const char* HitMqttClient::heartbeatMode(bool remoteDisplayActive) {
  if (remoteDisplayActive) return "direct";
  if (mqttClient_.connected()) return "mqtt_connected";
  return "mqtt_disconnected";
}

}  // namespace go2
