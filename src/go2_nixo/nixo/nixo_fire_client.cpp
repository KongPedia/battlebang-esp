#include "go2_nixo/nixo/nixo_fire_client.h"

namespace go2 {

NixoFireClient* NixoFireClient::instance_ = nullptr;

namespace {

uint32_t usToDuty(int us) {
  us = constrain(us, 500, 2400);
  return (uint32_t)((((uint64_t)us) * ((1UL << 16) - 1)) / 20000ULL);
}

int angleToUs(int angle) {
  angle = constrain(angle, 0, 180);
  return map(angle, 0, 180, 500, 2400);
}

}  // namespace

void NixoFireClient::begin() {
  snprintf(commandTopic_, sizeof(commandTopic_), "%s/%s/command", NIXO_MQTT_TOPIC_PREFIX_VALUE, NIXO_ID_VALUE);
  snprintf(clientId_, sizeof(clientId_), "battlebang-nixo-%s", NIXO_ID_VALUE);

  pinMode(NIXO_RELAY1_PIN_VALUE, OUTPUT);
  if (NIXO_RELAY2_ENABLED_VALUE) {
    pinMode(NIXO_RELAY2_PIN_VALUE, OUTPUT);
  }
  relayOff();

  setupServo();
  writeServoAngle(servoCur_);

  mqttClient_.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient_.setCallback(NixoFireClient::mqttMessageCallback);
  mqttClient_.setBufferSize(NIXO_MQTT_BUFFER_SIZE);
  instance_ = this;

  Serial.printf("[NIXO] integrated id=%s topic=%s broker=%s:%u relay1=%d relay2=%d relay_on=%d relay_off=%d\n",
                NIXO_ID_VALUE,
                commandTopic_,
                MQTT_HOST,
                MQTT_PORT,
                NIXO_RELAY1_PIN_VALUE,
                NIXO_RELAY2_PIN_VALUE,
                NIXO_RELAY_ON_LEVEL_VALUE,
                NIXO_RELAY_OFF_LEVEL_VALUE);

  if (!configured()) {
    Serial.println("[NIXO] MQTT disabled until ESP Wi-Fi/MQTT config is provided");
  }
}

void NixoFireClient::tick(uint32_t now) {
  ensureMqttConnected(now);
  if (mqttClient_.connected()) {
    mqttClient_.loop();
  }
  updateFireSequence(now);
}

bool NixoFireClient::configured() const {
  return WIFI_SSID[0] != '\0' && MQTT_HOST[0] != '\0';
}

bool NixoFireClient::connected() {
  return mqttClient_.connected();
}

bool NixoFireClient::startFire(uint32_t durationMs, const char* source) {
  uint32_t now = millis();

  if (isFiring()) {
    Serial.printf("[FIRE] ignored source=%s reason=already_firing\n", source);
    return false;
  }
  if (lastFireStartMs_ != 0 && now - lastFireStartMs_ < NIXO_FIRE_COOLDOWN_MS) {
    Serial.printf("[FIRE] ignored source=%s reason=cooldown remaining_ms=%lu\n",
                  source,
                  (unsigned long)(NIXO_FIRE_COOLDOWN_MS - (now - lastFireStartMs_)));
    return false;
  }

  lastFireStartMs_ = now;
  activeFireDurationMs_ = clampFireDuration(durationMs);

  int next = (servoCur_ == NIXO_FIRE_POS_A) ? NIXO_FIRE_POS_B : NIXO_FIRE_POS_A;
  setServoTarget(next);
  fireState_ = FIRE_SERVO_MOVING;

  Serial.printf("[FIRE] start source=%s duration_ms=%lu\n", source, (unsigned long)activeFireDurationMs_);
  return true;
}

void NixoFireClient::stopFire(const char* source) {
  relayOff();
  fireState_ = FIRE_IDLE;
  Serial.printf("[FIRE] stop source=%s\n", source);
}

const char* NixoFireClient::commandTopic() const {
  return commandTopic_;
}

bool NixoFireClient::isFiring() const {
  return fireState_ != FIRE_IDLE;
}

void NixoFireClient::relayOff() {
  digitalWrite(NIXO_RELAY1_PIN_VALUE, NIXO_RELAY_OFF_LEVEL_VALUE);
  if (NIXO_RELAY2_ENABLED_VALUE) {
    digitalWrite(NIXO_RELAY2_PIN_VALUE, NIXO_RELAY_OFF_LEVEL_VALUE);
  }
}

void NixoFireClient::setupServo() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(NIXO_SERVO_PIN_VALUE, 50, 16);
#else
  static constexpr int SERVO_PWM_CHANNEL = 0;
  ledcSetup(SERVO_PWM_CHANNEL, 50, 16);
  ledcAttachPin(NIXO_SERVO_PIN_VALUE, SERVO_PWM_CHANNEL);
#endif
}

void NixoFireClient::writeServoAngle(int angle) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(NIXO_SERVO_PIN_VALUE, usToDuty(angleToUs(angle)));
#else
  static constexpr int SERVO_PWM_CHANNEL = 0;
  ledcWrite(SERVO_PWM_CHANNEL, usToDuty(angleToUs(angle)));
#endif
}

void NixoFireClient::setServoTarget(int angle) {
  servoTarget_ = constrain(angle, 0, 180);
}

bool NixoFireClient::updateServoMove(uint32_t now) {
  if (servoCur_ == servoTarget_) return true;
  if (now - lastServoMs_ < NIXO_SERVO_STEP_DT_MS) return false;
  lastServoMs_ = now;

  if (servoCur_ < servoTarget_) {
    servoCur_ = min(servoCur_ + NIXO_SERVO_STEP, servoTarget_);
  } else {
    servoCur_ = max(servoCur_ - NIXO_SERVO_STEP, servoTarget_);
  }
  writeServoAngle(servoCur_);
  return servoCur_ == servoTarget_;
}

void NixoFireClient::updateFireSequence(uint32_t now) {
  switch (fireState_) {
    case FIRE_IDLE:
      return;
    case FIRE_SERVO_MOVING:
      if (updateServoMove(now)) {
        digitalWrite(NIXO_RELAY1_PIN_VALUE, NIXO_RELAY_ON_LEVEL_VALUE);
        if (NIXO_RELAY2_ENABLED_VALUE) {
          digitalWrite(NIXO_RELAY2_PIN_VALUE, NIXO_RELAY_OFF_LEVEL_VALUE);
        }
        fireState_ = FIRE_RELAY_WAIT1;
        fireTimerMs_ = now;
        Serial.printf("[RELAY] CH1 ON pin=%d level=%d readback=%d\n",
                      NIXO_RELAY1_PIN_VALUE,
                      NIXO_RELAY_ON_LEVEL_VALUE,
                      digitalRead(NIXO_RELAY1_PIN_VALUE));
      }
      return;
    case FIRE_RELAY_WAIT1:
      if (!NIXO_RELAY2_ENABLED_VALUE) {
        if (now - fireTimerMs_ >= activeFireDurationMs_) {
          relayOff();
          fireState_ = FIRE_IDLE;
          Serial.printf("[RELAY] CH1 OFF pin=%d level=%d readback=%d\n",
                        NIXO_RELAY1_PIN_VALUE,
                        NIXO_RELAY_OFF_LEVEL_VALUE,
                        digitalRead(NIXO_RELAY1_PIN_VALUE));
          Serial.println("[RELAY] ALL OFF / FIRE done");
        }
        return;
      }
      if (now - fireTimerMs_ >= NIXO_RELAY_DELAY1_MS) {
        digitalWrite(NIXO_RELAY2_PIN_VALUE, NIXO_RELAY_ON_LEVEL_VALUE);
        fireState_ = FIRE_RELAY_WAIT2;
        fireTimerMs_ = now;
        Serial.printf("[RELAY] CH2 ON pin=%d level=%d readback=%d\n",
                      NIXO_RELAY2_PIN_VALUE,
                      NIXO_RELAY_ON_LEVEL_VALUE,
                      digitalRead(NIXO_RELAY2_PIN_VALUE));
      }
      return;
    case FIRE_RELAY_WAIT2:
      if (now - fireTimerMs_ >= activeFireDurationMs_) {
        relayOff();
        fireState_ = FIRE_IDLE;
        Serial.printf("[RELAY] CH1 OFF pin=%d readback=%d | CH2 OFF pin=%d readback=%d\n",
                      NIXO_RELAY1_PIN_VALUE,
                      digitalRead(NIXO_RELAY1_PIN_VALUE),
                      NIXO_RELAY2_PIN_VALUE,
                      digitalRead(NIXO_RELAY2_PIN_VALUE));
        Serial.println("[RELAY] ALL OFF / FIRE done");
      }
      return;
  }
}

void NixoFireClient::ensureMqttConnected(uint32_t now) {
  if (!configured()) return;
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqttClient_.connected()) return;
  if (now - lastMqttRetryMs_ < MQTT_RETRY_INTERVAL_MS) return;
  lastMqttRetryMs_ = now;

  mqttClient_.setServer(MQTT_HOST, MQTT_PORT);
  Serial.printf("[NIXO MQTT] connecting host=%s port=%u client_id=%s\n", MQTT_HOST, MQTT_PORT, clientId_);
  if (!mqttClient_.connect(clientId_)) {
    Serial.printf("[NIXO MQTT] connect failed state=%d\n", mqttClient_.state());
    return;
  }

  mqttClient_.publish(commandTopic_, "", true);
  bool ok = mqttClient_.subscribe(commandTopic_, NIXO_MQTT_QOS);
  Serial.printf("[NIXO MQTT] %s %s qos=%u\n",
                ok ? "subscribed" : "subscribe failed",
                commandTopic_,
                NIXO_MQTT_QOS);
}

void NixoFireClient::mqttMessageCallback(char* topic, byte* payload, unsigned int length) {
  if (instance_ == nullptr) return;
  instance_->handleMqttMessage(topic, payload, length);
}

void NixoFireClient::handleMqttMessage(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, commandTopic_) != 0) {
    Serial.printf("[NIXO MQTT] ignored topic=%s\n", topic);
    return;
  }
  if (length == 0) {
    Serial.println("[NIXO MQTT] ignored empty retained command clear");
    return;
  }
  if (length >= NIXO_MQTT_BUFFER_SIZE) {
    Serial.printf("[NIXO MQTT] payload too large length=%u limit=%u\n", length, NIXO_MQTT_BUFFER_SIZE - 1);
    return;
  }

  char payloadBuffer[NIXO_MQTT_BUFFER_SIZE];
  memcpy(payloadBuffer, payload, length);
  payloadBuffer[length] = '\0';
  handleCommandPayload(payloadBuffer, length);
}

void NixoFireClient::handleCommandPayload(const char* payload, unsigned int length) {
  StaticJsonDocument<NIXO_MQTT_BUFFER_SIZE> doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    Serial.printf("[NIXO MQTT] invalid JSON: %s\n", error.c_str());
    return;
  }

  const int schemaVersion = doc["schema_version"] | 0;
  const char* command = doc["command"] | "";
  const char* nixoId = doc["nixo_id"] | "";
  const char* requestId = doc["request_id"] | "";
  const bool enabled = doc["enabled"] | true;

  if (schemaVersion != 1) {
    Serial.printf("[NIXO MQTT] ignored schema_version=%d\n", schemaVersion);
    return;
  }
  if (strcmp(command, "fire") != 0) {
    Serial.printf("[NIXO MQTT] ignored command=%s\n", command);
    return;
  }
  if (strcmp(nixoId, NIXO_ID_VALUE) != 0) {
    Serial.printf("[NIXO MQTT] ignored nixo_id=%s expected=%s\n", nixoId, NIXO_ID_VALUE);
    return;
  }
  if (requestId[0] != '\0' && lastMqttRequestId_ == requestId) {
    Serial.printf("[NIXO MQTT] duplicate request_id=%s ignored\n", requestId);
    return;
  }
  lastMqttRequestId_ = requestId;

  if (!enabled) {
    stopFire("mqtt");
    Serial.printf("[NIXO MQTT] fire off request_id=%s\n", requestId);
    return;
  }

  uint32_t durationMs = clampFireDuration(doc["duration_ms"] | NIXO_FIRE_DEFAULT_DURATION_MS);
  Serial.printf("[NIXO MQTT] fire on request_id=%s duration_ms=%lu\n", requestId, (unsigned long)durationMs);
  if (!startFire(durationMs, "mqtt")) {
    Serial.printf("[NIXO MQTT] fire not started request_id=%s\n", requestId);
  }
}

uint32_t NixoFireClient::clampFireDuration(uint32_t durationMs) const {
  if (durationMs < NIXO_FIRE_MIN_DURATION_MS) return NIXO_FIRE_MIN_DURATION_MS;
  if (durationMs > NIXO_FIRE_MAX_DURATION_MS) return NIXO_FIRE_MAX_DURATION_MS;
  return durationMs;
}

}  // namespace go2
