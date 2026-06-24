#pragma once

#include <Arduino.h>
#include <vector>

#include "heavy-blaster/config/runtime_config.h"

namespace battlebang {
namespace heavy_blaster {

struct TopicSet {
  String deviceStatus;
  String deviceConfig;
  String deviceOta;
  String allOta;
  String blasterStatus;
  String blasterConfig;
  String blasterCommand;
  String blasterOta;
};

TopicSet buildTopics(const RuntimeConfig& config);
std::vector<String> buildSubscriptionTopics(const RuntimeConfig& config);

}  // namespace heavy_blaster
}  // namespace battlebang
