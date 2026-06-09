#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include "go2_nixo/build_config.h"

namespace go2 {

class NixoFireClient {
 public:
  void begin();
  void tick(uint32_t now);
  bool configured() const;
  bool connected();
  void setFireInhibited(bool inhibited);
  bool startFire(uint32_t durationMs = NIXO_FIRE_DEFAULT_DURATION_MS, const char* source = "local");
  void stopFire(const char* source = "local");
  const char* commandTopic() const;

 private:
  enum FireState {
    FIRE_IDLE,
    FIRE_PREFIRE_DELAY,
    FIRE_RELAY_WAIT1,
    FIRE_RELAY_WAIT2,
  };

  WiFiClient wifiClient_;
  PubSubClient mqttClient_{wifiClient_};
  char commandTopic_[160] = {0};
  char clientId_[96] = {0};
  String lastMqttRequestId_;
  uint32_t lastMqttRetryMs_ = 0;

  FireState fireState_ = FIRE_IDLE;
  uint32_t fireTimerMs_ = 0;
  uint32_t lastFireStartMs_ = 0;
  uint32_t activeFireDurationMs_ = NIXO_FIRE_DEFAULT_DURATION_MS;
  bool fireInhibited_ = false;

  static NixoFireClient* instance_;
  static void mqttMessageCallback(char* topic, byte* payload, unsigned int length);

  bool isFiring() const;
  void relayOff();
  void updateFireSequence(uint32_t now);
  void ensureMqttConnected(uint32_t now);
  void handleMqttMessage(char* topic, byte* payload, unsigned int length);
  void handleCommandPayload(const char* payload, unsigned int length);
  uint32_t clampFireDuration(uint32_t durationMs) const;
};

}  // namespace go2
