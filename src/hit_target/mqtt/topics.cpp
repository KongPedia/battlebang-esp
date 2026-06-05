#include "topics.h"

namespace battlebang {
namespace hit_target {
namespace {
String cleanRoot(const String& root) {
  String out = root;
  while (out.endsWith("/")) out.remove(out.length() - 1);
  return out.length() == 0 ? String("battlebang") : out;
}
}

TopicSet buildTopics(const RuntimeConfig& config) {
  const String root = cleanRoot(config.mqttRoot);
  TopicSet topics;
  topics.deviceStatus = root + "/devices/" + config.deviceId + "/status";
  topics.deviceConfig = root + "/devices/" + config.deviceId + "/config";
  topics.deviceOta = root + "/devices/" + config.deviceId + "/ota";
  topics.allOta = root + "/hit_targets/all/ota";
  if (config.configured && config.targetId.length() > 0) {
    const String base = root + "/hit_targets/" + config.targetId;
    topics.targetStatus = base + "/status";
    topics.targetConfig = base + "/config";
    topics.targetCommand = base + "/command";
    topics.targetOta = base + "/ota";
  }
  return topics;
}

std::vector<String> buildSubscriptionTopics(const RuntimeConfig& config) {
  TopicSet topics = buildTopics(config);
  std::vector<String> result;
  result.push_back(topics.deviceConfig);
  result.push_back(topics.deviceOta);
  result.push_back(topics.allOta);
  if (topics.targetConfig.length() > 0) result.push_back(topics.targetConfig);
  if (topics.targetCommand.length() > 0) result.push_back(topics.targetCommand);
  if (topics.targetOta.length() > 0) result.push_back(topics.targetOta);
  return result;
}

}  // namespace hit_target
}  // namespace battlebang
