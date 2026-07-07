#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClient.h>
#include <vector>

#include "station/config/runtime_config.h"
#include "station/net/wifi_manager.h"
#include "station/station/station_controller.h"

namespace battlebang {
namespace station {

class MqttBus {
 public:
  void begin(RuntimeConfig& config, RuntimeConfigStore& store, WifiManager& wifi, StationController& station);
  void reconfigure();
  void loop(bool deferAutomaticStatus = false);
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
  StationController* station_ = nullptr;
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

}  // namespace station
}  // namespace battlebang
