#include <WiFi.h>

#include "go2_full/blaster/blaster_fire_client.h"
#include "go2_full/debug_serial.h"

#include <bb_esp_core/config/string_buffer.h>
#include <bb_esp_core/mqtt/topic_utils.h>
#include <bb_esp_hw/relay_pin_utils.h>

namespace go2 {

BlasterFireClient* BlasterFireClient::instance_ = nullptr;

namespace {

constexpr uint16_t kMqttSocketTimeoutSeconds = 1;

void copyStringOrWarn(const char* label, const String& value, char* buffer, size_t length) {
  if (!battlebang::esp::config::copyToFixedBuffer(value, buffer, length)) {
    BB_DEBUG_SERIAL.printf("[CONFIG] %s truncated length=%u capacity=%u\n",
                  label,
                  static_cast<unsigned int>(value.length()),
                  static_cast<unsigned int>(length));
  }
}

void warnIfFormatTruncated(const char* label, int written, size_t capacity) {
  if (written < 0 || static_cast<size_t>(written) >= capacity) {
    BB_DEBUG_SERIAL.printf("[CONFIG] %s truncated formatted_length=%d capacity=%u\n",
                  label,
                  written,
                  static_cast<unsigned int>(capacity));
  }
}

}  // namespace

void BlasterFireClient::begin() {
  begin(runtimeConfigFromBuild());
}

void BlasterFireClient::begin(const RuntimeConfig& config) {
  if (mqttClient_.connected()) mqttClient_.disconnect();
  copyStringOrWarn("nixo.id", config.blaster.blasterId, blasterId_, sizeof(blasterId_));
  copyStringOrWarn("mqtt.host", config.common.mqttHost, mqttHost_, sizeof(mqttHost_));
  copyStringOrWarn("mqtt.username", config.common.mqttUsername, mqttUsername_, sizeof(mqttUsername_));
  copyStringOrWarn("mqtt.password", config.common.mqttPassword, mqttPassword_, sizeof(mqttPassword_));
  mqttPort_ = config.common.mqttPort;
  networkConfigured_ = config.common.configured;
  fireDefaultDurationMs_ = config.blaster.fireDefaultDurationMs;
  fireMinDurationMs_ = config.blaster.fireMinDurationMs;
  fireMaxDurationMs_ = config.blaster.fireMaxDurationMs;
  fireCooldownMs_ = 0;  // BTB-801: ESP-side Blaster cooldown removed.
  prefireDelayMs_ = config.blaster.prefireDelayMs;
  relayDelay1Ms_ = config.blaster.relayDelay1Ms;
  activeFireDurationMs_ = fireDefaultDurationMs_;

  const String commandTopic = battlebang::esp::mqtt::joinTopic(
      config.blaster.commandTopicPrefix, config.blaster.blasterId, "command");
  copyStringOrWarn("nixo.command_topic", commandTopic, commandTopic_, sizeof(commandTopic_));
  int clientIdLength = snprintf(clientId_, sizeof(clientId_), "battlebang-nixo-%s", blasterId_);
  warnIfFormatTruncated("nixo.client_id", clientIdLength, sizeof(clientId_));

  if (BLASTER_RELAY2_ENABLED_VALUE) {
    battlebang::esp::hw::configureRelayPinOffWithLevel(BLASTER_RELAY2_PIN_VALUE, BLASTER_RELAY_OFF_LEVEL_VALUE);
  }
  battlebang::esp::hw::configureRelayPinOffWithLevel(BLASTER_RELAY1_PIN_VALUE, BLASTER_RELAY_OFF_LEVEL_VALUE);
  relayOff();

  mqttClient_.setServer(mqttHost_, mqttPort_);
  mqttClient_.setCallback(BlasterFireClient::mqttMessageCallback);
  mqttClient_.setBufferSize(BLASTER_MQTT_BUFFER_SIZE);
  mqttClient_.setSocketTimeout(kMqttSocketTimeoutSeconds);
  wifiClient_.setTimeout(kMqttSocketTimeoutSeconds);
  instance_ = this;

  BB_DEBUG_SERIAL.printf("[NIXO] integrated id=%s topic=%s broker=%s:%u relay1=%d relay2=%d relay_on=%d relay_off=%d delay1_ms=%lu fire_default_ms=%lu fire_min_ms=%lu fire_max_ms=%lu cooldown_ms=%lu prefire_ms=%lu\n",
                blasterId_,
                commandTopic_,
                mqttHost_,
                mqttPort_,
                BLASTER_RELAY1_PIN_VALUE,
                BLASTER_RELAY2_PIN_VALUE,
                BLASTER_RELAY_ON_LEVEL_VALUE,
                BLASTER_RELAY_OFF_LEVEL_VALUE,
                (unsigned long)relayDelay1Ms_,
                (unsigned long)fireDefaultDurationMs_,
                (unsigned long)fireMinDurationMs_,
                (unsigned long)fireMaxDurationMs_,
                (unsigned long)fireCooldownMs_,
                (unsigned long)prefireDelayMs_);

  if (!configured()) {
    BB_DEBUG_SERIAL.println("[NIXO] MQTT disabled until ESP Wi-Fi/MQTT config is provided");
  }
}

void BlasterFireClient::tick(uint32_t now) {
  tickLocal(now);
  tickNetwork(now);
}

void BlasterFireClient::tickLocal(uint32_t now) {
  // Relay timing is a local safety/UX path and must never wait behind MQTT reconnect attempts.
  updateFireSequence(now);
}

void BlasterFireClient::tickNetwork(uint32_t now) {
  ensureMqttConnected(now);
  if (mqttClient_.connected()) {
    mqttClient_.loop();
  }
}

bool BlasterFireClient::configured() const {
  return networkConfigured_;
}

bool BlasterFireClient::connected() {
  return mqttClient_.connected();
}

void BlasterFireClient::setFireInhibited(bool inhibited) {
  if (fireInhibited_ == inhibited) return;
  fireInhibited_ = inhibited;
  if (fireInhibited_) {
    stopFire("inhibited");
  }
  BB_DEBUG_SERIAL.printf("[FIRE] inhibit=%s\n", fireInhibited_ ? "true" : "false");
}

bool BlasterFireClient::startFire(uint32_t durationMs, const char* source, bool immediateFlywheel) {
  uint32_t now = millis();

  if (fireInhibited_) {
    BB_DEBUG_SERIAL.printf("[FIRE] ignored source=%s reason=inhibited\n", source);
    return false;
  }
  if (isFiring()) {
    BB_DEBUG_SERIAL.printf("[FIRE] ignored source=%s reason=already_firing\n", source);
    return false;
  }
  uint32_t remainingMs = cooldownRemainingMs(now);
  if (remainingMs > 0) {
    BB_DEBUG_SERIAL.printf("[FIRE] ignored source=%s reason=cooldown remaining_ms=%lu\n",
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

  BB_DEBUG_SERIAL.printf("[FIRE] start source=%s duration_ms=%lu prefire_delay_ms=%lu immediate_flywheel=%s\n",
                source,
                (unsigned long)activeFireDurationMs_,
                (unsigned long)prefireDelayMs_,
                immediateFlywheel ? "true" : "false");
  return true;
}

void BlasterFireClient::stopFire(const char* source) {
  bool wasFiring = isFiring();
  const uint32_t now = millis();
  if (BLASTER_RELAY2_ENABLED_VALUE && (fireState_ == FIRE_RELAY_WAIT1 || fireState_ == FIRE_RELAY_WAIT2)) {
    beginStopSequence(now);
  } else {
    relayOff();
    fireState_ = FIRE_IDLE;
    if (wasFiring) {
      beginCooldown(now);
    }
    activeFireSource_[0] = '\0';
  }
  BB_DEBUG_SERIAL.printf("[FIRE] stop source=%s\n", source);
}

const char* BlasterFireClient::commandTopic() const {
  return commandTopic_;
}

bool BlasterFireClient::isFiring() const {
  return fireState_ != FIRE_IDLE;
}

bool BlasterFireClient::fireInhibited() const {
  return fireInhibited_;
}

const char* BlasterFireClient::activeFireSource() const {
  return activeFireSource_;
}

const char* BlasterFireClient::lastFireSource() const {
  return lastFireSource_;
}

void BlasterFireClient::noteFireSource(const char* source) {
  if (!isFiring()) return;
  copyFireSource(source, activeFireSource_, sizeof(activeFireSource_));
  copyFireSource(activeFireSource_, lastFireSource_, sizeof(lastFireSource_));
}

const char* BlasterFireClient::fireStateName() const {
  if (fireInhibited_) return "inhibited";
  switch (fireState_) {
    case FIRE_IDLE:
      return "ready";
    case FIRE_PREFIRE_DELAY:
      return "prefire_delay";
    case FIRE_RELAY_WAIT1:
      return BLASTER_RELAY2_ENABLED_VALUE ? "flywheel_spinup" : "firing";
    case FIRE_RELAY_WAIT2:
      return "firing";
    case FIRE_STOP_DELAY:
      return "flywheel_spindown";
  }
  return "unknown";
}

uint32_t BlasterFireClient::cooldownRemainingMs(uint32_t now) const {
  if (cooldownStartedMs_ == 0) return 0;
  uint32_t elapsed = now - cooldownStartedMs_;
  if (elapsed >= fireCooldownMs_) return 0;
  return fireCooldownMs_ - elapsed;
}

uint32_t BlasterFireClient::cooldownDurationMs() const {
  return fireCooldownMs_;
}

void BlasterFireClient::relayOff() {
  if (BLASTER_RELAY2_ENABLED_VALUE) {
    digitalWrite(BLASTER_RELAY2_PIN_VALUE, BLASTER_RELAY_OFF_LEVEL_VALUE);
  }
  digitalWrite(BLASTER_RELAY1_PIN_VALUE, BLASTER_RELAY_OFF_LEVEL_VALUE);
}

void BlasterFireClient::startFlywheelNow(uint32_t now) {
  digitalWrite(BLASTER_RELAY1_PIN_VALUE, BLASTER_RELAY_ON_LEVEL_VALUE);
  BB_DEBUG_SERIAL.printf("[RELAY] CH1 ON pin=%d level=%d readback=%d\n",
                BLASTER_RELAY1_PIN_VALUE,
                BLASTER_RELAY_ON_LEVEL_VALUE,
                digitalRead(BLASTER_RELAY1_PIN_VALUE));
  if (BLASTER_RELAY2_ENABLED_VALUE) {
    digitalWrite(BLASTER_RELAY2_PIN_VALUE, BLASTER_RELAY_OFF_LEVEL_VALUE);
  }
  fireState_ = FIRE_RELAY_WAIT1;
  fireTimerMs_ = now;
}

void BlasterFireClient::beginStopSequence(uint32_t now) {
  digitalWrite(BLASTER_RELAY2_PIN_VALUE, BLASTER_RELAY_OFF_LEVEL_VALUE);
  BB_DEBUG_SERIAL.printf("[RELAY] CH2 OFF pin=%d level=%d readback=%d\n",
                BLASTER_RELAY2_PIN_VALUE,
                BLASTER_RELAY_OFF_LEVEL_VALUE,
                digitalRead(BLASTER_RELAY2_PIN_VALUE));
  fireState_ = FIRE_STOP_DELAY;
  fireTimerMs_ = now;
}

void BlasterFireClient::updateFireSequence(uint32_t now) {
  switch (fireState_) {
    case FIRE_IDLE:
      return;
    case FIRE_PREFIRE_DELAY:
      if (now - fireTimerMs_ >= prefireDelayMs_) {
        digitalWrite(BLASTER_RELAY1_PIN_VALUE, BLASTER_RELAY_ON_LEVEL_VALUE);
        if (BLASTER_RELAY2_ENABLED_VALUE) {
          digitalWrite(BLASTER_RELAY2_PIN_VALUE, BLASTER_RELAY_OFF_LEVEL_VALUE);
        }
        fireState_ = FIRE_RELAY_WAIT1;
        fireTimerMs_ = now;
        BB_DEBUG_SERIAL.printf("[RELAY] CH1 ON pin=%d level=%d readback=%d\n",
                      BLASTER_RELAY1_PIN_VALUE,
                      BLASTER_RELAY_ON_LEVEL_VALUE,
                      digitalRead(BLASTER_RELAY1_PIN_VALUE));
      }
      return;
    case FIRE_RELAY_WAIT1:
      if (!BLASTER_RELAY2_ENABLED_VALUE) {
        if (now - fireTimerMs_ >= activeFireDurationMs_) {
          relayOff();
          fireState_ = FIRE_IDLE;
          BB_DEBUG_SERIAL.printf("[RELAY] CH1 OFF pin=%d level=%d readback=%d\n",
                        BLASTER_RELAY1_PIN_VALUE,
                        BLASTER_RELAY_OFF_LEVEL_VALUE,
                        digitalRead(BLASTER_RELAY1_PIN_VALUE));
          BB_DEBUG_SERIAL.println("[RELAY] ALL OFF / FIRE done");
          activeFireSource_[0] = '\0';
          beginCooldown(now);
        }
        return;
      }
      if (now - fireTimerMs_ >= relayDelay1Ms_) {
        digitalWrite(BLASTER_RELAY2_PIN_VALUE, BLASTER_RELAY_ON_LEVEL_VALUE);
        fireState_ = FIRE_RELAY_WAIT2;
        fireTimerMs_ = now;
        BB_DEBUG_SERIAL.printf("[RELAY] CH2 ON pin=%d level=%d readback=%d\n",
                      BLASTER_RELAY2_PIN_VALUE,
                      BLASTER_RELAY_ON_LEVEL_VALUE,
                      digitalRead(BLASTER_RELAY2_PIN_VALUE));
      }
      return;
    case FIRE_RELAY_WAIT2:
      if (now - fireTimerMs_ >= activeFireDurationMs_) {
        beginStopSequence(now);
      }
      return;
    case FIRE_STOP_DELAY:
      if (now - fireTimerMs_ >= relayDelay1Ms_) {
        digitalWrite(BLASTER_RELAY1_PIN_VALUE, BLASTER_RELAY_OFF_LEVEL_VALUE);
        fireState_ = FIRE_IDLE;
        BB_DEBUG_SERIAL.printf("[RELAY] CH1 OFF pin=%d level=%d readback=%d\n",
                      BLASTER_RELAY1_PIN_VALUE,
                      BLASTER_RELAY_OFF_LEVEL_VALUE,
                      digitalRead(BLASTER_RELAY1_PIN_VALUE));
        BB_DEBUG_SERIAL.println("[RELAY] ALL OFF / FIRE done");
        activeFireSource_[0] = '\0';
        beginCooldown(now);
      }
      return;
  }
}

void BlasterFireClient::beginCooldown(uint32_t now) {
  if (fireCooldownMs_ == 0) {
    cooldownStartedMs_ = 0;
    return;
  }
  cooldownStartedMs_ = now;
  BB_DEBUG_SERIAL.printf("[FIRE] cooldown start duration_ms=%lu\n", (unsigned long)fireCooldownMs_);
}

void BlasterFireClient::ensureMqttConnected(uint32_t now) {
  if (!configured()) return;
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqttClient_.connected()) return;
  if (now - lastMqttRetryMs_ < MQTT_RETRY_INTERVAL_MS) return;
  lastMqttRetryMs_ = now;

  mqttClient_.setServer(mqttHost_, mqttPort_);
  const bool useAuth = mqttUsername_[0] != '\0' || mqttPassword_[0] != '\0';
  BB_DEBUG_SERIAL.printf("[NIXO MQTT] connecting host=%s port=%u client_id=%s auth=%s\n",
                mqttHost_,
                mqttPort_,
                clientId_,
                useAuth ? "yes" : "no");
  const bool connected = useAuth
                             ? mqttClient_.connect(clientId_, mqttUsername_, mqttPassword_)
                             : mqttClient_.connect(clientId_);
  if (!connected) {
    BB_DEBUG_SERIAL.printf("[NIXO MQTT] connect failed state=%d\n", mqttClient_.state());
    return;
  }

  mqttClient_.publish(commandTopic_, "", true);
  bool ok = mqttClient_.subscribe(commandTopic_, BLASTER_MQTT_QOS);
  BB_DEBUG_SERIAL.printf("[NIXO MQTT] %s %s qos=%u\n",
                ok ? "subscribed" : "subscribe failed",
                commandTopic_,
                BLASTER_MQTT_QOS);
}

void BlasterFireClient::mqttMessageCallback(char* topic, byte* payload, unsigned int length) {
  if (instance_ == nullptr) return;
  instance_->handleMqttMessage(topic, payload, length);
}

void BlasterFireClient::handleMqttMessage(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, commandTopic_) != 0) {
    BB_DEBUG_SERIAL.printf("[NIXO MQTT] ignored topic=%s\n", topic);
    return;
  }
  if (length == 0) {
    BB_DEBUG_SERIAL.println("[NIXO MQTT] ignored empty retained command clear");
    return;
  }
  if (length >= BLASTER_MQTT_BUFFER_SIZE) {
    BB_DEBUG_SERIAL.printf("[NIXO MQTT] payload too large length=%u limit=%u\n", length, BLASTER_MQTT_BUFFER_SIZE - 1);
    return;
  }

  char payloadBuffer[BLASTER_MQTT_BUFFER_SIZE];
  memcpy(payloadBuffer, payload, length);
  payloadBuffer[length] = '\0';
  handleCommandPayload(payloadBuffer, length);
}

void BlasterFireClient::handleCommandPayload(const char* payload, unsigned int length) {
  StaticJsonDocument<BLASTER_MQTT_BUFFER_SIZE> doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    BB_DEBUG_SERIAL.printf("[NIXO MQTT] invalid JSON: %s\n", error.c_str());
    return;
  }

  const int schemaVersion = doc["schema_version"] | 0;
  const char* command = doc["command"] | "";
  const char* blasterId = doc["nixo_id"] | "";
  const char* requestId = doc["request_id"] | "";

  if (schemaVersion != 1) {
    BB_DEBUG_SERIAL.printf("[NIXO MQTT] ignored schema_version=%d\n", schemaVersion);
    return;
  }
  if (strcmp(command, "fire") != 0) {
    BB_DEBUG_SERIAL.printf("[NIXO MQTT] ignored command=%s\n", command);
    return;
  }
  if (strcmp(blasterId, blasterId_) != 0) {
    BB_DEBUG_SERIAL.printf("[NIXO MQTT] ignored nixo_id=%s expected=%s\n", blasterId, blasterId_);
    return;
  }
  if (requestId[0] == '\0') {
    BB_DEBUG_SERIAL.println("[NIXO MQTT] ignored fire command without request_id");
    return;
  }
  if (!doc["enabled"].is<bool>()) {
    BB_DEBUG_SERIAL.println("[NIXO MQTT] ignored fire command without boolean enabled");
    return;
  }
  if (lastMqttRequestId_ == requestId) {
    BB_DEBUG_SERIAL.printf("[NIXO MQTT] duplicate request_id=%s ignored\n", requestId);
    return;
  }
  lastMqttRequestId_ = requestId;

  const char* source = doc["source"] | "mqtt";
  const bool enabled = doc["enabled"].as<bool>();
  if (!enabled) {
    stopFire(source);
    BB_DEBUG_SERIAL.printf("[NIXO MQTT] fire off request_id=%s source=%s\n", requestId, source);
    return;
  }

  uint32_t durationMs = clampFireDuration(doc["duration_ms"] | fireDefaultDurationMs_);
  BB_DEBUG_SERIAL.printf("[NIXO MQTT] fire on request_id=%s source=%s duration_ms=%lu\n", requestId, source, (unsigned long)durationMs);
  if (!startFire(durationMs, source, false)) {
    BB_DEBUG_SERIAL.printf("[NIXO MQTT] fire not started request_id=%s\n", requestId);
  }
}

void BlasterFireClient::copyFireSource(const char* source, char* dest, size_t length) {
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

uint32_t BlasterFireClient::clampFireDuration(uint32_t durationMs) const {
  if (durationMs == 0) durationMs = fireDefaultDurationMs_;
  if (durationMs < fireMinDurationMs_) return fireMinDurationMs_;
  if (durationMs > fireMaxDurationMs_) return fireMaxDurationMs_;
  return durationMs;
}

}  // namespace go2
