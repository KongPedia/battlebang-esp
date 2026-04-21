#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClient.h>

#include "../config/runtime_config.h"
#include "../control/turret_control.h"
#include "../net/wifi_manager.h"
#include "topics.h"

namespace battlebang {
namespace turret_fleet {

class MqttBus {
 public:
  void begin(RuntimeConfig& config, RuntimeConfigStore& store, WifiManager& wifi, TurretControl& control);
  void reconfigure();
  void loop();
  void publishStatus(const char* reason);
  bool connected();
  int state();
  String statusSummary();
  void printStatus(const char* reason);

 private:
  RuntimeConfig* config_ = nullptr;
  RuntimeConfigStore* store_ = nullptr;
  WifiManager* wifi_ = nullptr;
  TurretControl* control_ = nullptr;
  WiFiClient wifiClient_;
  PubSubClient client_{wifiClient_};
  unsigned long lastConnectAttemptMs_ = 0;
  unsigned long lastStatusMs_ = 0;
  unsigned long lastConnectSuccessMs_ = 0;
  unsigned long lastConnectFailMs_ = 0;
  int lastConnectState_ = 0;
  bool subscriptionsDirty_ = true;

  bool connectIfNeeded();
  void subscribeTopics();
  void handleMessage(char* topic, byte* payload, unsigned int length);
  void handleConfigPayload(const char* payload);
  void handleOtaPayload(const char* payload);
  void handleCommandPayload(const char* topic, const char* payload);
};

}  // namespace turret_fleet
}  // namespace battlebang
