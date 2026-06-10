#pragma once

#include <Arduino.h>
#include <vector>

#include "hit_target/config/runtime_config.h"

namespace battlebang {
namespace hit_target {

struct TopicSet {
  String deviceStatus;
  String deviceConfig;
  String deviceOta;
  String allOta;
  String targetStatus;
  String targetConfig;
  String targetCommand;
  String targetOta;
  String linkedDeviceStatus;
};

TopicSet buildTopics(const RuntimeConfig& config);
std::vector<String> buildSubscriptionTopics(const RuntimeConfig& config);

}  // namespace hit_target
}  // namespace battlebang
