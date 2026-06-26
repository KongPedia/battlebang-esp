#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include "go2_nixo/config/runtime_config.h"

namespace go2 {

struct BarDisplayUpdate {
  float fillRatio = 1.0f;
  String mode = "idle";
  bool down = false;
  uint32_t ttlMs = 1000;
  bool resetHitState = false;
  bool debugOverride = false;
};

struct QueuedHitEvent {
  int targetId = 0;
  uint32_t sequence = 0;
  uint32_t firmwareTsMs = 0;
  int peakRaw = -1;
  int thresholdRaw = -1;
  uint16_t acceptedHitCount = 0;
  uint16_t hpRemaining = 0;
  uint16_t maxHits = 1;
  bool down = false;
};

using BarDisplayHandler = void (*)(const BarDisplayUpdate& update);
using ManagementMessageHandler = void (*)(const char* topic, const byte* payload, unsigned int length);

class HitMqttClient {
 public:
  void begin(BarDisplayHandler barHandler);
  void begin(const RuntimeConfig& config, BarDisplayHandler barHandler);
  void setManagementHandlers(ManagementMessageHandler configHandler, ManagementMessageHandler otaHandler);
  void tick(uint32_t now, bool remoteDisplayActive);
  bool configured() const;
  bool connected();
  bool publishHitEvent(int targetId,
                       uint32_t sequence,
                       uint32_t firmwareTsMs,
                       bool queued = false,
                       uint32_t queuedForMs = 0,
                       uint8_t queueDepth = 0,
                       int peakRaw = -1,
                       int thresholdRaw = -1,
                       uint16_t acceptedHitCount = 0,
                       uint16_t hpRemaining = 0,
                       uint16_t maxHits = 1,
                       bool down = false);
  void queueHitEvent(int targetId,
                     uint32_t sequence,
                     uint32_t firmwareTsMs,
                     int peakRaw = -1,
                     int thresholdRaw = -1,
                     uint16_t acceptedHitCount = 0,
                     uint16_t hpRemaining = 0,
                     uint16_t maxHits = 1,
                     bool down = false);
  void clearOfflineQueue();
  uint8_t offlineQueueCount() const;
  bool publishDeviceStatus(const char* payload);
  const char* eventTopic() const;
  const char* ringCommandTopic() const;
  const char* deviceStatusTopic() const;
  const char* deviceConfigTopic() const;
  const char* deviceOtaTopic() const;

 private:
  WiFiClient wifiClient_;
  PubSubClient mqttClient_{wifiClient_};
  BarDisplayHandler barHandler_ = nullptr;
  ManagementMessageHandler configHandler_ = nullptr;
  ManagementMessageHandler otaHandler_ = nullptr;
  QueuedHitEvent offlineQueue_[OFFLINE_HIT_QUEUE_CAPACITY] = {};
  uint8_t offlineQueueHead_ = 0;
  uint8_t offlineQueueCount_ = 0;
  uint8_t offlineQueueCapacity_ = OFFLINE_HIT_QUEUE_CAPACITY;
  uint32_t offlineQueueDropped_ = 0;
  uint32_t offlineQueueFlushIntervalMs_ = OFFLINE_HIT_QUEUE_FLUSH_INTERVAL_MS;
  uint32_t lastOfflineQueueFlushMs_ = 0;
  char robotId_[48] = {0};
  char wifiSsid_[64] = {0};
  char wifiPassword_[96] = {0};
  char mqttHost_[96] = {0};
  char mqttUsername_[64] = {0};
  char mqttPassword_[96] = {0};
  uint16_t mqttPort_ = MQTT_PORT;
  bool networkConfigured_ = false;
  char eventTopic_[128] = {0};
  char ringCommandTopic_[160] = {0};
  char deviceStatusTopic_[160] = {0};
  char deviceConfigTopic_[160] = {0};
  char deviceOtaTopic_[160] = {0};
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
