#include "topics.h"

#include <bb_esp_core/mqtt/topic_utils.h>

namespace battlebang {
namespace station {
namespace {

String joinTopic(const String& a, const String& b, const String& c, const String& d, const String& e) {
  return battlebang::esp::mqtt::joinTopic(battlebang::esp::mqtt::joinTopic(a, b, c, d), e);
}

bool validateTopicSegment(const String& value, const char* field, String& error) {
  if (battlebang::esp::mqtt::isSafeTopicSegment(value)) return true;
  error = String(field) + " must use only A-Z, a-z, 0-9, '_', '-', or '.'";
  return false;
}

}  // namespace

TopicSet buildTopics(const RuntimeConfig& config) {
  TopicSet topics;
  String error;
  String root;
  if (!battlebang::esp::mqtt::normalizeRootOrError(config.mqttRoot, root, error)) return topics;
  if (!validateTopicSegment(config.stationId, "station_id", error)) return topics;
  topics.stationStatus = joinTopic(root, "devices", "station", config.stationId, "status");
  topics.stationConfig = joinTopic(root, "devices", "station", config.stationId, "config");
  topics.stationCommand = joinTopic(root, "devices", "station", config.stationId, "command");
  topics.stationOta = joinTopic(root, "devices", "station", config.stationId, "ota");
  topics.allOta = joinTopic(root, "devices", "station", "all", "ota");
  return topics;
}

std::vector<String> buildSubscriptionTopics(const RuntimeConfig& config) {
  TopicSet topics = buildTopics(config);
  std::vector<String> result;
  if (topics.stationConfig.length() > 0) result.push_back(topics.stationConfig);
  if (topics.stationCommand.length() > 0) result.push_back(topics.stationCommand);
  if (topics.stationOta.length() > 0) result.push_back(topics.stationOta);
  if (topics.allOta.length() > 0) result.push_back(topics.allOta);
  return result;
}

}  // namespace station
}  // namespace battlebang
