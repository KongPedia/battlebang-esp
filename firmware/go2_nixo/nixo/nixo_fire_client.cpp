#include <WiFi.h>

#include "go2_nixo/nixo/nixo_fire_client.h"

#include <bb_esp_core/config/string_buffer.h>
#include <bb_esp_core/mqtt/topic_utils.h>
#include <bb_esp_hw/relay_pin_utils.h>

namespace go2 {

NixoFireClient* NixoFireClient::instance_ = nullptr;

namespace {

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

void NixoFireClient::begin() {
  begin(runtimeConfigFromBuild());
}

void NixoFireClient::begin(const RuntimeConfig& config) {
  if (mqttClient_.connected()) mqttClient_.disconnect();
  copyStringOrWarn("nixo.id", config.nixo.nixoId, nixoId_, sizeof(nixoId_));
  copyStringOrWarn("mqtt.host", config.common.mqttHost, mqttHost_, sizeof(mqttHost_));
  copyStringOrWarn("mqtt.username", config.common.mqttUsername, mqttUsername_, sizeof(mqttUsername_));
  copyStringOrWarn("mqtt.password", config.common.mqttPassword, mqttPassword_, sizeof(mqttPassword_));
  mqttPort_ = config.common.mqttPort;
  networkConfigured_ = config.common.configured;
  fireDefaultDurationMs_ = config.nixo.fireDefaultDurationMs;
  fireMinDurationMs_ = config.nixo.fireMinDurationMs;
  fireMaxDurationMs_ = config.nixo.fireMaxDurationMs;
  fireCooldownMs_ = 0;  // BTB-801: ESP-side Nixo cooldown removed.
  prefireDelayMs_ = config.nixo.prefireDelayMs;
  relayDelay1Ms_ = config.nixo.relayDelay1Ms;
  activeFireDurationMs_ = fireDefaultDurationMs_;

  const String commandTopic = battlebang::esp::mqtt::joinTopic(
      config.nixo.commandTopicPrefix, config.nixo.nixoId, "command");
  copyStringOrWarn("nixo.command_topic", commandTopic, commandTopic_, sizeof(commandTopic_));
  int clientIdLength = snprintf(clientId_, sizeof(clientId_), "battlebang-nixo-%s", nixoId_);
  warnIfFormatTruncated("nixo.client_id", clientIdLength, sizeof(clientId_));

  if (NIXO_RELAY2_ENABLED_VALUE) {
    battlebang::esp::hw::configureRelayPinOffWithLevel(NIXO_RELAY2_PIN_VALUE, NIXO_RELAY_OFF_LEVEL_VALUE);
  }
  battlebang::esp::hw::configureRelayPinOffWithLevel(NIXO_RELAY1_PIN_VALUE, NIXO_RELAY_OFF_LEVEL_VALUE);
  relayOff();

  mqttClient_.setServer(mqttHost_, mqttPort_);
  mqttClient_.setCallback(NixoFireClient::mqttMessageCallback);
  mqttClient_.setBufferSize(NIXO_MQTT_BUFFER_SIZE);
  instance_ = this;

  Serial.printf("[NIXO] integrated id=%s topic=%s broker=%s:%u relay1=%d relay2=%d relay_on=%d relay_off=%d delay1_ms=%lu fire_default_ms=%lu fire_min_ms=%lu fire_max_ms=%lu cooldown_ms=%lu prefire_ms=%lu\n",
                nixoId_,
                commandTopic_,
                mqttHost_,
                mqttPort_,
                NIXO_RELAY1_PIN_VALUE,
                NIXO_RELAY2_PIN_VALUE,
                NIXO_RELAY_ON_LEVEL_VALUE,
                NIXO_RELAY_OFF_LEVEL_VALUE,
                (unsigned long)relayDelay1Ms_,
                (unsigned long)fireDefaultDurationMs_,
                (unsigned long)fireMinDurationMs_,
                (unsigned long)fireMaxDurationMs_,
                (unsigned long)fireCooldownMs_,
                (unsigned long)prefireDelayMs_);

  if (!configured()) {
    Serial.println("[NIXO] MQTT disabled until ESP Wi-Fi/MQTT config is provided");
  }
}

void NixoFireClient::tick(uint32_t now) {
  // Relay timing is a local safety/UX path and must not wait behind MQTT reconnect attempts.
  updateFireSequence(now);
  ensureMqttConnected(now);
  if (mqttClient_.connected()) {
    mqttClient_.loop();
  }
}

bool NixoFireClient::configured() const {
  return networkConfigured_;
}

bool NixoFireClient::connected() {
  return mqttClient_.connected();
}

void NixoFireClient::setFireInhibited(bool inhibited) {
  if (fireInhibited_ == inhibited) return;
  fireInhibited_ = inhibited;
  if (fireInhibited_) {
    stopFire("inhibited");
  }
  Serial.printf("[FIRE] inhibit=%s\n", fireInhibited_ ? "true" : "false");
}

bool NixoFireClient::startFire(uint32_t durationMs, const char* source, bool immediateFlywheel) {
  uint32_t now = millis();

  if (fireInhibited_) {
    Serial.printf("[FIRE] ignored source=%s reason=inhibited\n", source);
    return false;
  }
  if (isFiring()) {
    Serial.printf("[FIRE] ignored source=%s reason=already_firing\n", source);
    return false;
  }
  uint32_t remainingMs = cooldownRemainingMs(now);
  if (remainingMs > 0) {
    Serial.printf("[FIRE] ignored source=%s reason=cooldown remaining_ms=%lu\n",
                  source,
                  (unsigned long)remainingMs);
    return false;
  }

  lastFireStartMs_ = now;
  copyFireSource(source, activeFireSource_, sizeof(activeFireSource_));
  copyFireSource(activeFireSource_, lastFireSource_, sizeof(lastFireSource_));
  activeFireDurationMs_ = clampFireDuration(durationMs);
  if (immediateFlywheel) {
    startFlywheelNow(now);
  } else {
    fireState_ = FIRE_PREFIRE_DELAY;
    fireTimerMs_ = now;
  }

  Serial.printf("[FIRE] start source=%s duration_ms=%lu prefire_delay_ms=%lu immediate_flywheel=%s\n",
                source,
                (unsigned long)activeFireDurationMs_,
                (unsigned long)prefireDelayMs_,
                immediateFlywheel ? "true" : "false");
  return true;
}

void NixoFireClient::stopFire(const char* source) {
  bool wasFiring = isFiring();
  const uint32_t now = millis();
  if (NIXO_RELAY2_ENABLED_VALUE && (fireState_ == FIRE_RELAY_WAIT1 || fireState_ == FIRE_RELAY_WAIT2)) {
    beginStopSequence(now);
  } else {
    relayOff();
    fireState_ = FIRE_IDLE;
    if (wasFiring) {
      beginCooldown(now);
    }
    activeFireSource_[0] = '\0';
  }
  Serial.printf("[FIRE] stop source=%s\n", source);
}

const char* NixoFireClient::commandTopic() const {
  return commandTopic_;
}

bool NixoFireClient::isFiring() const {
  return fireState_ != FIRE_IDLE;
}

bool NixoFireClient::fireInhibited() const {
  return fireInhibited_;
}

const char* NixoFireClient::activeFireSource() const {
  return activeFireSource_;
}

const char* NixoFireClient::lastFireSource() const {
  return lastFireSource_;
}

void NixoFireClient::noteFireSource(const char* source) {
  if (!isFiring()) return;
  copyFireSource(source, activeFireSource_, sizeof(activeFireSource_));
  copyFireSource(activeFireSource_, lastFireSource_, sizeof(lastFireSource_));
}

const char* NixoFireClient::fireStateName() const {
  if (fireInhibited_) return "inhibited";
  switch (fireState_) {
    case FIRE_IDLE:
      return "ready";
    case FIRE_PREFIRE_DELAY:
      return "prefire_delay";
    case FIRE_RELAY_WAIT1:
      return NIXO_RELAY2_ENABLED_VALUE ? "flywheel_spinup" : "firing";
    case FIRE_RELAY_WAIT2:
      return "firing";
    case FIRE_STOP_DELAY:
      return "flywheel_spindown";
  }
  return "unknown";
}

uint32_t NixoFireClient::cooldownRemainingMs(uint32_t now) const {
  if (cooldownStartedMs_ == 0) return 0;
  uint32_t elapsed = now - cooldownStartedMs_;
  if (elapsed >= fireCooldownMs_) return 0;
  return fireCooldownMs_ - elapsed;
}

uint32_t NixoFireClient::cooldownDurationMs() const {
  return fireCooldownMs_;
}

void NixoFireClient::relayOff() {
  if (NIXO_RELAY2_ENABLED_VALUE) {
    digitalWrite(NIXO_RELAY2_PIN_VALUE, NIXO_RELAY_OFF_LEVEL_VALUE);
  }
  digitalWrite(NIXO_RELAY1_PIN_VALUE, NIXO_RELAY_OFF_LEVEL_VALUE);
}

void NixoFireClient::startFlywheelNow(uint32_t now) {
  digitalWrite(NIXO_RELAY1_PIN_VALUE, NIXO_RELAY_ON_LEVEL_VALUE);
  Serial.printf("[RELAY] CH1 ON pin=%d level=%d readback=%d\n",
                NIXO_RELAY1_PIN_VALUE,
                NIXO_RELAY_ON_LEVEL_VALUE,
                digitalRead(NIXO_RELAY1_PIN_VALUE));
  if (NIXO_RELAY2_ENABLED_VALUE) {
    digitalWrite(NIXO_RELAY2_PIN_VALUE, NIXO_RELAY_OFF_LEVEL_VALUE);
  }
  fireState_ = FIRE_RELAY_WAIT1;
  fireTimerMs_ = now;
}

void NixoFireClient::beginStopSequence(uint32_t now) {
  digitalWrite(NIXO_RELAY2_PIN_VALUE, NIXO_RELAY_OFF_LEVEL_VALUE);
  Serial.printf("[RELAY] CH2 OFF pin=%d level=%d readback=%d\n",
                NIXO_RELAY2_PIN_VALUE,
                NIXO_RELAY_OFF_LEVEL_VALUE,
                digitalRead(NIXO_RELAY2_PIN_VALUE));
  fireState_ = FIRE_STOP_DELAY;
  fireTimerMs_ = now;
}

void NixoFireClient::updateFireSequence(uint32_t now) {
  switch (fireState_) {
    case FIRE_IDLE:
      return;
    case FIRE_PREFIRE_DELAY:
      if (now - fireTimerMs_ >= prefireDelayMs_) {
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
          activeFireSource_[0] = '\0';
          beginCooldown(now);
        }
        return;
      }
      if (now - fireTimerMs_ >= relayDelay1Ms_) {
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
        beginStopSequence(now);
      }
      return;
    case FIRE_STOP_DELAY:
      if (now - fireTimerMs_ >= relayDelay1Ms_) {
        digitalWrite(NIXO_RELAY1_PIN_VALUE, NIXO_RELAY_OFF_LEVEL_VALUE);
        fireState_ = FIRE_IDLE;
        Serial.printf("[RELAY] CH1 OFF pin=%d level=%d readback=%d\n",
                      NIXO_RELAY1_PIN_VALUE,
                      NIXO_RELAY_OFF_LEVEL_VALUE,
                      digitalRead(NIXO_RELAY1_PIN_VALUE));
        Serial.println("[RELAY] ALL OFF / FIRE done");
        activeFireSource_[0] = '\0';
        beginCooldown(now);
      }
      return;
  }
}

void NixoFireClient::beginCooldown(uint32_t now) {
  if (fireCooldownMs_ == 0) {
    cooldownStartedMs_ = 0;
    return;
  }
  cooldownStartedMs_ = now;
  Serial.printf("[FIRE] cooldown start duration_ms=%lu\n", (unsigned long)fireCooldownMs_);
}

void NixoFireClient::ensureMqttConnected(uint32_t now) {
  if (!configured()) return;
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqttClient_.connected()) return;
  if (now - lastMqttRetryMs_ < MQTT_RETRY_INTERVAL_MS) return;
  lastMqttRetryMs_ = now;

  mqttClient_.setServer(mqttHost_, mqttPort_);
  const bool useAuth = mqttUsername_[0] != '\0' || mqttPassword_[0] != '\0';
  Serial.printf("[NIXO MQTT] connecting host=%s port=%u client_id=%s auth=%s\n",
                mqttHost_,
                mqttPort_,
                clientId_,
                useAuth ? "yes" : "no");
  const bool connected = useAuth
                             ? mqttClient_.connect(clientId_, mqttUsername_, mqttPassword_)
                             : mqttClient_.connect(clientId_);
  if (!connected) {
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

  if (schemaVersion != 1) {
    Serial.printf("[NIXO MQTT] ignored schema_version=%d\n", schemaVersion);
    return;
  }
  if (strcmp(command, "fire") != 0) {
    Serial.printf("[NIXO MQTT] ignored command=%s\n", command);
    return;
  }
  if (strcmp(nixoId, nixoId_) != 0) {
    Serial.printf("[NIXO MQTT] ignored nixo_id=%s expected=%s\n", nixoId, nixoId_);
    return;
  }
  if (requestId[0] == '\0') {
    Serial.println("[NIXO MQTT] ignored fire command without request_id");
    return;
  }
  if (!doc["enabled"].is<bool>()) {
    Serial.println("[NIXO MQTT] ignored fire command without boolean enabled");
    return;
  }
  if (lastMqttRequestId_ == requestId) {
    Serial.printf("[NIXO MQTT] duplicate request_id=%s ignored\n", requestId);
    return;
  }
  lastMqttRequestId_ = requestId;

  const char* source = doc["source"] | "mqtt";
  const bool enabled = doc["enabled"].as<bool>();
  if (!enabled) {
    stopFire(source);
    Serial.printf("[NIXO MQTT] fire off request_id=%s source=%s\n", requestId, source);
    return;
  }

  uint32_t durationMs = clampFireDuration(doc["duration_ms"] | fireDefaultDurationMs_);
  Serial.printf("[NIXO MQTT] fire on request_id=%s source=%s duration_ms=%lu\n", requestId, source, (unsigned long)durationMs);
  if (!startFire(durationMs, source, false)) {
    Serial.printf("[NIXO MQTT] fire not started request_id=%s\n", requestId);
  }
}

void NixoFireClient::copyFireSource(const char* source, char* dest, size_t length) {
  if (length == 0) return;
  if (source == nullptr || source[0] == '\0') source = "unknown";
  size_t out = 0;
  for (size_t i = 0; source[i] != '\0' && out + 1 < length; ++i) {
    const char c = source[i];
    const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == ':';
    dest[out++] = safe ? c : '_';
  }
  dest[out] = '\0';
}

uint32_t NixoFireClient::clampFireDuration(uint32_t durationMs) const {
  if (durationMs == 0) durationMs = fireDefaultDurationMs_;
  if (durationMs < fireMinDurationMs_) return fireMinDurationMs_;
  if (durationMs > fireMaxDurationMs_) return fireMaxDurationMs_;
  return durationMs;
}

}  // namespace go2
