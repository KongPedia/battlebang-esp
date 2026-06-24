#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include "../build_config.h"

namespace go2 {

struct BarDisplayUpdate {
  float fillRatio = 1.0f;
  String mode = "idle";
  bool down = false;
  uint32_t ttlMs = 1000;
  bool resetHitState = false;
};

struct QueuedHitCandidate {
  int targetId = 0;
  bool hit = false;
  uint32_t sequence = 0;
  uint32_t firmwareTsMs = 0;
  int peakRaw = -1;
  int thresholdRaw = -1;
};

using BarDisplayHandler = void (*)(const BarDisplayUpdate& update);

class HitMqttClient {
 public:
  void begin(BarDisplayHandler barHandler);
  void tick(uint32_t now, bool remoteDisplayActive);
  bool configured() const;
  bool connected();
  bool publishHitCandidate(int targetId,
                           bool hit,
                           uint32_t sequence,
                           uint32_t firmwareTsMs,
                           bool queued = false,
                           uint32_t queuedForMs = 0,
                           uint8_t queueDepth = 0,
                           int peakRaw = -1,
                           int thresholdRaw = -1);
  void queueHitCandidate(int targetId,
                         bool hit,
                         uint32_t sequence,
                         uint32_t firmwareTsMs,
                         int peakRaw = -1,
                         int thresholdRaw = -1);
  void clearOfflineQueue();
  uint8_t offlineQueueCount() const;
  const char* eventTopic() const;
  const char* ringCommandTopic() const;

 private:
  WiFiClient wifiClient_;
  PubSubClient mqttClient_{wifiClient_};
  BarDisplayHandler barHandler_ = nullptr;
  QueuedHitCandidate offlineQueue_[OFFLINE_HIT_QUEUE_CAPACITY] = {};
  uint8_t offlineQueueHead_ = 0;
  uint8_t offlineQueueCount_ = 0;
  uint32_t offlineQueueDropped_ = 0;
  uint32_t lastOfflineQueueFlushMs_ = 0;
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
  void flushOfflineQueue(uint32_t now);
  void popOfflineQueueHead();
  void publishHeartbeat(uint32_t now, bool remoteDisplayActive);
  const char* heartbeatMode(bool remoteDisplayActive);
};

}  // namespace go2
