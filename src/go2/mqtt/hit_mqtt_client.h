#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include "../build_config.h"

namespace go2 {

struct PendingHitCandidate {
  bool active = false;
  int targetId = 0;
  uint16_t peak = 0;
  uint32_t sequence = 0;
  uint32_t startedMs = 0;
};

struct RingDisplayUpdate {
  float fillRatio = 1.0f;
  String mode = "idle";
  bool down = false;
  uint32_t ttlMs = 1000;
};

using RingDisplayHandler = void (*)(const RingDisplayUpdate& update);

class HitMqttClient {
 public:
  void begin(RingDisplayHandler ringHandler);
  void tick(uint32_t now, bool remoteDisplayActive);
  bool configured() const;
  bool connected();
  bool publishHitCandidate(int targetId, uint16_t peak, uint32_t sequence, uint32_t now);
  void startPending(int targetId, uint16_t peak, uint32_t sequence, uint32_t now);
  bool popTimedOutFallback(uint32_t now, PendingHitCandidate& out);
  bool popSupersededFallback(PendingHitCandidate& out);
  void clearPending();
  const char* eventTopic() const;
  const char* ringCommandTopic() const;

 private:
  WiFiClient wifiClient_;
  PubSubClient mqttClient_{wifiClient_};
  RingDisplayHandler ringHandler_ = nullptr;
  PendingHitCandidate pending_;
  char eventTopic_[128] = {0};
  char ringCommandTopic_[160] = {0};
  char clientId_[96] = {0};
  uint32_t lastWiFiRetryMs_ = 0;
  uint32_t lastMqttRetryMs_ = 0;
  uint32_t lastHeartbeatTxMs_ = 0;
  uint32_t heartbeatSequence_ = 0;

  static HitMqttClient* instance_;
  static void mqttMessageCallback(char* topic, byte* payload, unsigned int length);
  void handleMqttMessage(char* topic, byte* payload, unsigned int length);
  void ensureWiFiConnected(uint32_t now);
  void ensureMqttConnected(uint32_t now);
  void publishHeartbeat(uint32_t now, bool remoteDisplayActive);
  const char* heartbeatMode(bool remoteDisplayActive);
  bool popPending(PendingHitCandidate& out);
};

}  // namespace go2
