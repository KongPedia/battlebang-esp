#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClient.h>
#include <vector>

#include "hit_target/config/runtime_config.h"
#include "hit_target/net/wifi_manager.h"
#include "hit_target/target/hit_target_controller.h"

namespace battlebang {
namespace hit_target {

class MqttBus {
 public:
  void begin(RuntimeConfig& config, RuntimeConfigStore& store, WifiManager& wifi, HitTargetController& target);
  void reconfigure();
  void loop();
  bool connected();
  void publishStatus(const char* reason);

 private:
  bool connectIfNeeded();
  void subscribeTopics();
  void handleMessage(char* topic, byte* payload, unsigned int length);
  void handleConfigPayload(const char* payload);
  void handleCommandPayload(const char* topic, const char* payload);
  void handleLinkedDeviceStatusPayload(const byte* payload, unsigned int length);
  void handleOtaPayload(const char* payload);

  RuntimeConfig* config_ = nullptr;
  RuntimeConfigStore* store_ = nullptr;
  WifiManager* wifi_ = nullptr;
  HitTargetController* target_ = nullptr;
  WiFiClient wifiClient_;
  PubSubClient client_{wifiClient_};
  unsigned long lastConnectAttemptMs_ = 0;
  unsigned long lastStatusMs_ = 0;
  unsigned long lastStatusChangeCheckMs_ = 0;
  String lastStatusSignature_;
  std::vector<String> subscribedTopics_;
  bool subscriptionsDirty_ = true;
};

}  // namespace hit_target
}  // namespace battlebang
