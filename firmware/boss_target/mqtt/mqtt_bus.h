#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClient.h>
#include <vector>

#include "boss_target/config/runtime_config.h"
#include "boss_target/net/wifi_manager.h"
#include "boss_target/target/boss_target_controller.h"

namespace battlebang {
namespace boss_target {

class MqttBus {
 public:
  void begin(RuntimeConfig& config, RuntimeConfigStore& store, WifiManager& wifi, BossTargetController& target);
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
  void handleOtaPayload(const char* payload);

  RuntimeConfig* config_ = nullptr;
  RuntimeConfigStore* store_ = nullptr;
  WifiManager* wifi_ = nullptr;
  BossTargetController* target_ = nullptr;
  WiFiClient wifiClient_;
  PubSubClient client_{wifiClient_};
  unsigned long lastConnectAttemptMs_ = 0;
  unsigned long lastStatusMs_ = 0;
  unsigned long lastStatusChangeCheckMs_ = 0;
  String lastStatusSignature_;
  std::vector<String> subscribedTopics_;
  bool subscriptionsDirty_ = true;
};

}  // namespace boss_target
}  // namespace battlebang
