#pragma once

#include <Arduino.h>
#include <vector>

#include "boss_target/config/runtime_config.h"

namespace battlebang {
namespace boss_target {

struct TopicSet {
  String deviceStatus;
  String deviceConfig;
  String deviceOta;
  String allOta;
  String bossStatus;
  String bossConfig;
  String bossCommand;
  String bossOta;
};

TopicSet buildTopics(const RuntimeConfig& config);
std::vector<String> buildSubscriptionTopics(const RuntimeConfig& config);

}  // namespace boss_target
}  // namespace battlebang
