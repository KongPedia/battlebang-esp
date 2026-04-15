#pragma once

#include <Arduino.h>
#include <vector>

#include "../config/runtime_config.h"

namespace battlebang {
namespace turret_fleet {

struct TopicSet {
  String deviceStatus;
  String deviceConfig;
  String deviceOta;
  String allOta;
  String turretStatus;
  String turretConfig;
  String turretOta;
  String turretCommand;
};

TopicSet buildTopics(const RuntimeConfig& config);
std::vector<String> buildSubscriptionTopics(const RuntimeConfig& config);

}  // namespace turret_fleet
}  // namespace battlebang
