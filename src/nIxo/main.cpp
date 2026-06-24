#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include "build_config.h"
#include <bb_esp_hw/relay_pin_utils.h>

// Standalone Nixo ESP firmware for the 2-ESP split.
// Control path is server/Command Center -> MQTT only.
// USB Serial is log-only: no serial-monitor fire command is accepted.
// No piezo, no LED ring, no UART/Jetson, no servo GPIO18.

WiFiClient nixoWifiClient;
PubSubClient nixoMqttClient(nixoWifiClient);

constexpr int RELAY1_PIN = NIXO_RELAY1_PIN;
constexpr int RELAY2_PIN = NIXO_RELAY2_PIN;
constexpr int RELAY_CHANNELS = NIXO_RELAY_CHANNELS;
constexpr bool RELAY2_ENABLED = RELAY2_PIN >= 0;
constexpr int RELAY_ON = NIXO_RELAY_ON_LEVEL;
constexpr int RELAY_OFF = NIXO_RELAY_OFF_LEVEL;
constexpr const char* RELAY_VARIANT = NIXO_RELAY_VARIANT_NAME;
constexpr const char* RELAY1_ROLE = NIXO_RELAY1_ROLE;
constexpr const char* RELAY2_ROLE = NIXO_RELAY2_ROLE;

constexpr uint32_t DEFAULT_FIRE_DURATION_MS = NIXO_FIRE_DEFAULT_DURATION_MS;
constexpr uint32_t MIN_FIRE_DURATION_MS = NIXO_FIRE_MIN_DURATION_MS;
constexpr uint32_t MAX_FIRE_DURATION_MS = NIXO_FIRE_MAX_DURATION_MS;
constexpr uint32_t FIRE_COOLDOWN_MS = NIXO_FIRE_COOLDOWN_MS;
constexpr uint32_t PREFIRE_DELAY_MS = NIXO_PREFIRE_DELAY_MS;
constexpr uint32_t RELAY_DELAY1_MS = NIXO_RELAY_DELAY1_MS;

static_assert(RELAY1_PIN >= 0, "NIXO_RELAY1_PIN must be a valid GPIO");
static_assert(RELAY_CHANNELS == 1 || RELAY_CHANNELS == 2, "NIXO_RELAY_CHANNELS must be 1 or 2");
static_assert((RELAY_CHANNELS == 2) == RELAY2_ENABLED, "NIXO_RELAY_CHANNELS must match NIXO_RELAY2_PIN");
static_assert(!RELAY2_ENABLED || RELAY1_PIN != RELAY2_PIN, "Nixo relay pins must be different");
static_assert(MIN_FIRE_DURATION_MS <= DEFAULT_FIRE_DURATION_MS, "default duration below min");
static_assert(DEFAULT_FIRE_DURATION_MS <= MAX_FIRE_DURATION_MS, "default duration above max");

char nixoMqttCommandTopic[128] = {0};
String lastMqttRequestId;
uint32_t lastWifiAttemptMs = 0;
uint32_t lastMqttAttemptMs = 0;

enum FireState : uint8_t {
  FIRE_IDLE,
  FIRE_PREFIRE_DELAY,
  FIRE_RELAY_WAIT1,
  FIRE_RELAY_WAIT2,
};

FireState fireState = FIRE_IDLE;
uint32_t fireTimerMs = 0;
uint32_t cooldownStartedMs = 0;
uint32_t activeFireDurationMs = DEFAULT_FIRE_DURATION_MS;

static bool isPlaceholder(const char* value, const char* placeholder) {
  return value == nullptr || value[0] == '\0' || strcmp(value, placeholder) == 0;
}

static bool hasWifiConfig() {
  return !isPlaceholder(NIXO_WIFI_SSID, "YOUR_WIFI_SSID");
}

static bool hasMqttConfig() {
  return !isPlaceholder(NIXO_MQTT_HOST, "YOUR_MQTT_HOST") &&
         !isPlaceholder(NIXO_MQTT_HOST, "COMMAND_CENTER_IP_OR_DNS");
}

static bool networkConfigReady() {
  return hasWifiConfig() && hasMqttConfig();
}

static bool isFiring() {
  return fireState != FIRE_IDLE;
}

static const char* fireStateName() {
  switch (fireState) {
    case FIRE_IDLE:
      return "idle";
    case FIRE_PREFIRE_DELAY:
      return "prefire_delay";
    case FIRE_RELAY_WAIT1:
      return "relay_wait1";
    case FIRE_RELAY_WAIT2:
      return "relay_wait2";
  }
  return "unknown";
}

static uint32_t clampFireDuration(uint32_t durationMs) {
  return constrain(durationMs, MIN_FIRE_DURATION_MS, MAX_FIRE_DURATION_MS);
}

static uint32_t cooldownRemainingMs(uint32_t now) {
  if (cooldownStartedMs == 0) return 0;
  uint32_t elapsed = now - cooldownStartedMs;
  if (elapsed >= FIRE_COOLDOWN_MS) return 0;
  return FIRE_COOLDOWN_MS - elapsed;
}

static void beginCooldown(uint32_t now) {
  if (FIRE_COOLDOWN_MS == 0) {
    cooldownStartedMs = 0;
    return;
  }
  cooldownStartedMs = now;
  Serial.printf("[FIRE] cooldown start duration_ms=%lu\n", (unsigned long)FIRE_COOLDOWN_MS);
}

static void relayOff() {
  if (RELAY2_ENABLED) {
    digitalWrite(RELAY2_PIN, RELAY_OFF);
  }
  digitalWrite(RELAY1_PIN, RELAY_OFF);
}

static void stopFireSequence(const char* source = "mqtt") {
  bool wasFiring = isFiring();
  relayOff();
  fireState = FIRE_IDLE;
  if (wasFiring) {
    beginCooldown(millis());
  }
  Serial.printf("[FIRE] stop source=%s%s\n", source, wasFiring ? "" : " already_idle=true");
}

static bool startFireSequence(uint32_t durationMs = DEFAULT_FIRE_DURATION_MS, const char* source = "mqtt") {
  uint32_t now = millis();

  if (isFiring()) {
    Serial.printf("[FIRE] ignored source=%s reason=already_firing state=%s\n", source, fireStateName());
    return false;
  }
  uint32_t remainingMs = cooldownRemainingMs(now);
  if (remainingMs > 0) {
    Serial.printf("[FIRE] ignored source=%s reason=cooldown remaining_ms=%lu\n", source, (unsigned long)remainingMs);
    return false;
  }

  activeFireDurationMs = clampFireDuration(durationMs);
  fireState = FIRE_PREFIRE_DELAY;
  fireTimerMs = now;

  Serial.printf("[FIRE] start source=%s duration_ms=%lu prefire_delay_ms=%lu\n",
                source,
                (unsigned long)activeFireDurationMs,
                (unsigned long)PREFIRE_DELAY_MS);
  return true;
}

static void updateFireSequence(uint32_t now) {
  switch (fireState) {
    case FIRE_IDLE:
      return;

    case FIRE_PREFIRE_DELAY:
      if (now - fireTimerMs >= PREFIRE_DELAY_MS) {
        digitalWrite(RELAY1_PIN, RELAY_ON);
        if (RELAY2_ENABLED) {
          digitalWrite(RELAY2_PIN, RELAY_OFF);
        }
        fireState = FIRE_RELAY_WAIT1;
        fireTimerMs = now;
        Serial.printf("[RELAY] CH1 ON pin=%d role=%s level=%d readback=%d\n",
                      RELAY1_PIN,
                      RELAY1_ROLE,
                      RELAY_ON,
                      digitalRead(RELAY1_PIN));
      }
      return;

    case FIRE_RELAY_WAIT1:
      if (!RELAY2_ENABLED) {
        if (now - fireTimerMs >= activeFireDurationMs) {
          relayOff();
          fireState = FIRE_IDLE;
          Serial.printf("[RELAY] CH1 OFF pin=%d role=%s level=%d readback=%d\n",
                        RELAY1_PIN,
                        RELAY1_ROLE,
                        RELAY_OFF,
                        digitalRead(RELAY1_PIN));
          Serial.println("[RELAY] ALL OFF / FIRE done");
          beginCooldown(now);
        }
        return;
      }

      if (now - fireTimerMs >= RELAY_DELAY1_MS) {
        digitalWrite(RELAY2_PIN, RELAY_ON);
        fireState = FIRE_RELAY_WAIT2;
        fireTimerMs = now;
        Serial.printf("[RELAY] CH2 ON pin=%d role=%s level=%d readback=%d\n",
                      RELAY2_PIN,
                      RELAY2_ROLE,
                      RELAY_ON,
                      digitalRead(RELAY2_PIN));
      }
      return;

    case FIRE_RELAY_WAIT2:
      if (now - fireTimerMs >= activeFireDurationMs) {
        digitalWrite(RELAY2_PIN, RELAY_OFF);
        Serial.printf("[RELAY] CH2 OFF pin=%d role=%s level=%d readback=%d\n",
                      RELAY2_PIN,
                      RELAY2_ROLE,
                      RELAY_OFF,
                      digitalRead(RELAY2_PIN));
        digitalWrite(RELAY1_PIN, RELAY_OFF);
        fireState = FIRE_IDLE;
        Serial.printf("[RELAY] CH1 OFF pin=%d role=%s level=%d readback=%d\n",
                      RELAY1_PIN,
                      RELAY1_ROLE,
                      RELAY_OFF,
                      digitalRead(RELAY1_PIN));
        Serial.println("[RELAY] ALL OFF / FIRE done");
        beginCooldown(now);
      }
      return;
  }
}

static void handleNixoMqttCommand(const char* payload, unsigned int length) {
  StaticJsonDocument<NIXO_MQTT_BUFFER_BYTES> doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    Serial.printf("[MQTT] invalid JSON: %s\n", error.c_str());
    return;
  }

  const int schemaVersion = doc["schema_version"] | 0;
  const char* command = doc["command"] | "";
  const char* nixoId = doc["nixo_id"] | "";
  const char* requestId = doc["request_id"] | "";

  if (schemaVersion != 1) {
    Serial.printf("[MQTT] ignored schema_version=%d\n", schemaVersion);
    return;
  }
  if (strcmp(command, "fire") != 0) {
    Serial.printf("[MQTT] ignored command=%s\n", command);
    return;
  }
  if (strcmp(nixoId, NIXO_ID) != 0) {
    Serial.printf("[MQTT] ignored nixo_id=%s expected=%s\n", nixoId, NIXO_ID);
    return;
  }
  if (requestId[0] == '\0') {
    Serial.println("[MQTT] ignored fire command without request_id");
    return;
  }
  if (!doc["enabled"].is<bool>()) {
    Serial.println("[MQTT] ignored fire command without boolean enabled");
    return;
  }
  if (lastMqttRequestId == requestId) {
    Serial.printf("[MQTT] duplicate request_id=%s ignored\n", requestId);
    return;
  }
  lastMqttRequestId = requestId;

  const bool enabled = doc["enabled"].as<bool>();
  if (!enabled) {
    stopFireSequence("mqtt");
    Serial.printf("[MQTT] fire off request_id=%s\n", requestId);
    return;
  }

  const uint32_t durationMs = clampFireDuration(doc["duration_ms"] | DEFAULT_FIRE_DURATION_MS);
  Serial.printf("[MQTT] fire on request_id=%s duration_ms=%lu\n", requestId, (unsigned long)durationMs);
  if (!startFireSequence(durationMs, "mqtt")) {
    Serial.printf("[MQTT] fire not started request_id=%s\n", requestId);
  }
}

static void onNixoMqttMessage(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, nixoMqttCommandTopic) != 0) {
    Serial.printf("[MQTT] ignored topic=%s\n", topic);
    return;
  }
  if (length == 0) {
    Serial.println("[MQTT] ignored empty retained command clear");
    return;
  }
  if (length >= NIXO_MQTT_BUFFER_BYTES) {
    Serial.printf("[MQTT] payload too large length=%u limit=%u\n", length, NIXO_MQTT_BUFFER_BYTES - 1);
    return;
  }

  char payloadBuffer[NIXO_MQTT_BUFFER_BYTES];
  memcpy(payloadBuffer, payload, length);
  payloadBuffer[length] = '\0';
  handleNixoMqttCommand(payloadBuffer, length);
}

static void setupNixoMqtt() {
  snprintf(nixoMqttCommandTopic, sizeof(nixoMqttCommandTopic), "%s/%s/command", NIXO_MQTT_TOPIC_PREFIX, NIXO_ID);
  nixoMqttClient.setServer(NIXO_MQTT_HOST, NIXO_MQTT_PORT);
  nixoMqttClient.setCallback(onNixoMqttMessage);
  nixoMqttClient.setBufferSize(NIXO_MQTT_BUFFER_BYTES);

  Serial.printf("[NIXO] relay-only id=%s topic=%s broker=%s:%d relay_variant=%s relay_channels=%d duration_ms=%lu..%lu default=%lu cooldown_ms=%lu\n",
                NIXO_ID,
                nixoMqttCommandTopic,
                NIXO_MQTT_HOST,
                NIXO_MQTT_PORT,
                RELAY_VARIANT,
                RELAY_CHANNELS,
                (unsigned long)MIN_FIRE_DURATION_MS,
                (unsigned long)MAX_FIRE_DURATION_MS,
                (unsigned long)DEFAULT_FIRE_DURATION_MS,
                (unsigned long)FIRE_COOLDOWN_MS);

  if (!networkConfigReady()) {
    Serial.println("[MQTT] disabled until src/nIxo/local_secrets.h, src/local_secrets.h, or NIXO_* env vars provide Wi-Fi/MQTT config");
    return;
  }

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
}

static void maintainNixoWifi(uint32_t now) {
  if (!networkConfigReady() || WiFi.status() == WL_CONNECTED) return;
  if (lastWifiAttemptMs != 0 && now - lastWifiAttemptMs < NIXO_WIFI_RETRY_MS) return;

  lastWifiAttemptMs = now;
  Serial.printf("[WiFi] connecting ssid=%s\n", NIXO_WIFI_SSID);
  WiFi.disconnect(false, false);
  WiFi.begin(NIXO_WIFI_SSID, NIXO_WIFI_PASSWORD);
}

static void maintainNixoMqtt(uint32_t now) {
  if (!networkConfigReady()) return;
  if (WiFi.status() != WL_CONNECTED) return;
  if (nixoMqttClient.connected()) return;
  if (lastMqttAttemptMs != 0 && now - lastMqttAttemptMs < NIXO_MQTT_RETRY_MS) return;

  lastMqttAttemptMs = now;
  char clientId[96];
  snprintf(clientId, sizeof(clientId), "battlebang-nixo-%s-%04X", NIXO_ID, (uint16_t)(ESP.getEfuseMac() & 0xFFFF));

  Serial.printf("[MQTT] connecting %s:%d client_id=%s\n", NIXO_MQTT_HOST, NIXO_MQTT_PORT, clientId);
  bool connected = false;
  if (strlen(NIXO_MQTT_USERNAME) > 0) {
    connected = nixoMqttClient.connect(clientId, NIXO_MQTT_USERNAME, NIXO_MQTT_PASSWORD);
  } else {
    connected = nixoMqttClient.connect(clientId);
  }

  if (!connected) {
    Serial.printf("[MQTT] connect failed rc=%d\n", nixoMqttClient.state());
    return;
  }

#if NIXO_CLEAR_RETAINED_COMMAND_ON_CONNECT
  nixoMqttClient.publish(nixoMqttCommandTopic, "", true);
  Serial.printf("[MQTT] cleared retained command topic=%s\n", nixoMqttCommandTopic);
#endif

  if (nixoMqttClient.subscribe(nixoMqttCommandTopic, NIXO_MQTT_QOS)) {
    Serial.printf("[MQTT] subscribed topic=%s qos=%d\n", nixoMqttCommandTopic, NIXO_MQTT_QOS);
  } else {
    Serial.printf("[MQTT] subscribe failed topic=%s\n", nixoMqttCommandTopic);
  }
}

static void nixoNetworkTick(uint32_t now) {
  maintainNixoWifi(now);
  maintainNixoMqtt(now);
  if (nixoMqttClient.connected()) {
    nixoMqttClient.loop();
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  if (RELAY2_ENABLED) {
    battlebang::esp::hw::configureRelayPinOffWithLevel(RELAY2_PIN, RELAY_OFF);
  }
  battlebang::esp::hw::configureRelayPinOffWithLevel(RELAY1_PIN, RELAY_OFF);
  relayOff();

  Serial.println("[MODE] standalone Nixo relay ESP: server/MQTT command only; USB serial is log-only");
  Serial.printf("[PIN] variant=%s channels=%d RELAY1=%d role=%s RELAY2=%d role=%s relay_on=%d relay_off=%d delay1_ms=%lu servo_gpio18=unused\n",
                RELAY_VARIANT,
                RELAY_CHANNELS,
                RELAY1_PIN,
                RELAY1_ROLE,
                RELAY2_PIN,
                RELAY2_ROLE,
                RELAY_ON,
                RELAY_OFF,
                (unsigned long)RELAY_DELAY1_MS);
  setupNixoMqtt();
}

void loop() {
  uint32_t now = millis();

  nixoNetworkTick(now);
  updateFireSequence(now);

  delay(1);
}
