#pragma once

#include <Arduino.h>
#include <vector>

#include "station/config/runtime_config.h"

namespace battlebang {
namespace station {

struct TopicSet {
  String stationStatus;
  String stationConfig;
  String stationCommand;
  String stationOta;
  String allOta;
};

TopicSet buildTopics(const RuntimeConfig& config);
std::vector<String> buildSubscriptionTopics(const RuntimeConfig& config);

}  // namespace station
}  // namespace battlebang
