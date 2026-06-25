#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClient.h>
#include <vector>

#include "heavy_blaster/blaster/heavy_blaster_controller.h"
#include "heavy_blaster/config/runtime_config.h"
#include "heavy_blaster/net/wifi_manager.h"

namespace battlebang {
namespace heavy_blaster {

class MqttBus {
 public:
  void begin(RuntimeConfig& config, RuntimeConfigStore& store, WifiManager& wifi, HeavyBlasterController& blaster);
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
  HeavyBlasterController* blaster_ = nullptr;
  WiFiClient wifiClient_;
  PubSubClient client_{wifiClient_};
  unsigned long lastConnectAttemptMs_ = 0;
  unsigned long lastStatusMs_ = 0;
  unsigned long lastStatusChangeCheckMs_ = 0;
  String lastStatusSignature_;
  String lastRequestId_;
  std::vector<String> subscribedTopics_;
  bool subscriptionsDirty_ = true;
};

}  // namespace heavy_blaster
}  // namespace battlebang
