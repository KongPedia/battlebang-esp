#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include "go2_nixo/config/runtime_config.h"
#include "go2_nixo/mqtt/uart_friendly_wifi_client.h"

namespace go2 {

class NixoFireClient {
 public:
  void begin();
  void begin(const RuntimeConfig& config);
  void tick(uint32_t now);
  void tickLocal(uint32_t now);
  void tickNetwork(uint32_t now);
  bool configured() const;
  bool connected();
  void setFireInhibited(bool inhibited);
  bool startFire(uint32_t durationMs = 0, const char* source = "local", bool immediateFlywheel = false);
  void stopFire(const char* source = "local");
  bool isFiring() const;
  bool fireInhibited() const;
  const char* activeFireSource() const;
  const char* lastFireSource() const;
  void noteFireSource(const char* source);
  const char* fireStateName() const;
  uint32_t cooldownRemainingMs(uint32_t now) const;
  uint32_t cooldownDurationMs() const;
  uint32_t fireRemainingMs(uint32_t now) const;
  const char* commandTopic() const;

 private:
  enum FireState {
    FIRE_IDLE,
    FIRE_PREFIRE_DELAY,
    FIRE_RELAY_WAIT1,
    FIRE_RELAY_WAIT2,
  };

  UartFriendlyWiFiClient wifiClient_;
  PubSubClient mqttClient_{wifiClient_};
  char nixoId_[64] = {0};
  char mqttHost_[96] = {0};
  char mqttUsername_[64] = {0};
  char mqttPassword_[96] = {0};
  uint16_t mqttPort_ = MQTT_PORT;
  bool networkConfigured_ = false;
  char commandTopic_[160] = {0};
  char clientId_[96] = {0};
  String lastMqttRequestId_;
  uint32_t lastMqttRetryMs_ = 0;

  FireState fireState_ = FIRE_IDLE;
  uint32_t fireTimerMs_ = 0;
  uint32_t lastFireStartMs_ = 0;
  uint32_t cooldownStartedMs_ = 0;
  uint32_t fireDefaultDurationMs_ = NIXO_FIRE_DEFAULT_DURATION_MS;
  uint32_t fireMinDurationMs_ = NIXO_FIRE_MIN_DURATION_MS;
  uint32_t fireMaxDurationMs_ = NIXO_FIRE_MAX_DURATION_MS;
  uint32_t fireCooldownMs_ = NIXO_FIRE_COOLDOWN_MS;
  uint32_t prefireDelayMs_ = NIXO_PREFIRE_DELAY_MS;
  uint32_t relayDelay1Ms_ = NIXO_RELAY_DELAY1_MS;
  uint32_t activeFireDurationMs_ = NIXO_FIRE_DEFAULT_DURATION_MS;
  bool fireInhibited_ = false;
  char activeFireSource_[32] = {0};
  char lastFireSource_[32] = {0};

  static NixoFireClient* instance_;
  static void mqttMessageCallback(char* topic, byte* payload, unsigned int length);

  void relayOff();
  void startFlywheelNow(uint32_t now);
  void updateFireSequence(uint32_t now);
  void beginCooldown(uint32_t now);
  void ensureMqttConnected(uint32_t now);
  void handleMqttMessage(char* topic, byte* payload, unsigned int length);
  void handleCommandPayload(const char* payload, unsigned int length);
  void copyFireSource(const char* source, char* dest, size_t length);
  uint32_t clampFireDuration(uint32_t durationMs) const;
};

}  // namespace go2
