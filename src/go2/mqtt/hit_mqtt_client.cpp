#include "go2/mqtt/hit_mqtt_client.h"

namespace go2 {

HitMqttClient* HitMqttClient::instance_ = nullptr;

void HitMqttClient::begin(RingDisplayHandler ringHandler) {
  ringHandler_ = ringHandler;
  snprintf(eventTopic_, sizeof(eventTopic_), "%s/%s/events", MQTT_TOPIC_PREFIX, ROBOT_ID);
  snprintf(ringCommandTopic_, sizeof(ringCommandTopic_), "%s/%s/ring_display/command", MQTT_TOPIC_PREFIX, ROBOT_ID);
  snprintf(clientId_, sizeof(clientId_), "battlebang-hit-%s", ROBOT_ID);
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
  publishHeartbeat(now, remoteDisplayActive);
}

bool HitMqttClient::configured() const {
  return WIFI_SSID[0] != '\0' && MQTT_HOST[0] != '\0';
}

bool HitMqttClient::connected() {
  return mqttClient_.connected();
}

bool HitMqttClient::publishHitCandidate(int targetId, uint16_t peak, uint32_t sequence, uint32_t now) {
  if (!mqttClient_.connected()) return false;

  StaticJsonDocument<384> doc;
  doc["schema_version"] = 1;
  doc["event"] = "hit_candidate";
  doc["robot_id"] = ROBOT_ID;
  doc["sensor_id"] = targetIdToSensorId(targetId);
  doc["sequence"] = sequence;
  doc["peak"] = peak;
  doc["threshold"] = HIT_THRESHOLD;
  doc["firmware_ts_ms"] = now;

  char buffer[384];
  size_t size = serializeJson(doc, buffer, sizeof(buffer));
  bool ok = mqttClient_.publish(eventTopic_, reinterpret_cast<const uint8_t*>(buffer), size, false);
  if (ok) {
    Serial.printf("[HIT] published candidate seq=%lu target=%d peak=%u topic=%s\n",
                  (unsigned long)sequence,
                  targetId,
                  peak,
                  eventTopic_);
  } else {
    Serial.printf("[HIT] candidate publish failed seq=%lu target=%d peak=%u\n",
                  (unsigned long)sequence,
                  targetId,
                  peak);
  }
  return ok;
}

void HitMqttClient::startPending(int targetId, uint16_t peak, uint32_t sequence, uint32_t now) {
  pending_.active = true;
  pending_.targetId = targetId;
  pending_.peak = peak;
  pending_.sequence = sequence;
  pending_.startedMs = now;
}

bool HitMqttClient::popTimedOutFallback(uint32_t now, PendingHitCandidate& out) {
  if (!pending_.active) return false;
  if (now - pending_.startedMs < AUTHORITY_FALLBACK_TIMEOUT_MS) return false;
  return popPending(out);
}

bool HitMqttClient::popSupersededFallback(PendingHitCandidate& out) {
  return popPending(out);
}

void HitMqttClient::clearPending() {
  pending_ = PendingHitCandidate{};
}

const char* HitMqttClient::eventTopic() const {
  return eventTopic_;
}

const char* HitMqttClient::ringCommandTopic() const {
  return ringCommandTopic_;
}

void HitMqttClient::mqttMessageCallback(char* topic, byte* payload, unsigned int length) {
  if (instance_ == nullptr) return;
  instance_->handleMqttMessage(topic, payload, length);
}

void HitMqttClient::handleMqttMessage(char* topic, byte* payload, unsigned int length) {
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

  clearPending();
  RingDisplayUpdate update;
  update.fillRatio = doc["ring_fill_ratio"] | 1.0f;
  update.mode = String(doc["ring_display_mode"] | "idle");
  update.down = doc["down"] | false;
  update.ttlMs = doc["ttl_ms"] | 1000;
  if (ringHandler_ != nullptr) ringHandler_(update);

  Serial.printf("[MQTT] ring command mode=%s fill=%.3f down=%s ttl=%lu\n",
                update.mode.c_str(),
                constrain(update.fillRatio, 0.0f, 1.0f),
                update.down ? "true" : "false",
                (unsigned long)update.ttlMs);
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
}

void HitMqttClient::publishHeartbeat(uint32_t now, bool remoteDisplayActive) {
  if (now - lastHeartbeatTxMs_ < HEARTBEAT_TX_PERIOD_MS) return;
  lastHeartbeatTxMs_ = now;

  StaticJsonDocument<256> doc;
  doc["schema_version"] = 1;
  doc["event"] = "heartbeat";
  doc["robot_id"] = ROBOT_ID;
  doc["sensor_id"] = "hit_ring";
  doc["sequence"] = ++heartbeatSequence_;
  doc["firmware_ts_ms"] = now;
  doc["mode"] = heartbeatMode(remoteDisplayActive);

  char buffer[256];
  size_t size = serializeJson(doc, buffer, sizeof(buffer));
  mqttClient_.publish(eventTopic_, reinterpret_cast<const uint8_t*>(buffer), size, false);
}

const char* HitMqttClient::heartbeatMode(bool remoteDisplayActive) {
  if (remoteDisplayActive) return "direct";
  if (mqttClient_.connected()) return "mqtt_connected";
  return "fallback";
}

bool HitMqttClient::popPending(PendingHitCandidate& out) {
  if (!pending_.active) return false;
  out = pending_;
  clearPending();
  return true;
}

}  // namespace go2
