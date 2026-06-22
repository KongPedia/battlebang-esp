#include "topics.h"

namespace battlebang {
namespace boss_target {
namespace {
String cleanRoot(const String& root) {
  String out = root;
  while (out.endsWith("/")) out.remove(out.length() - 1);
  return out.length() == 0 ? String("battlebang") : out;
}
}  // namespace

TopicSet buildTopics(const RuntimeConfig& config) {
  const String root = cleanRoot(config.mqttRoot);
  TopicSet topics;
  topics.deviceStatus = root + "/devices/" + config.deviceId + "/status";
  topics.deviceConfig = root + "/devices/" + config.deviceId + "/config";
  topics.deviceOta = root + "/devices/" + config.deviceId + "/ota";
  topics.allOta = root + "/boss_targets/all/ota";
  if (config.configured && config.bossId.length() > 0) {
    const String base = root + "/boss_targets/" + config.bossId;
    topics.bossStatus = base + "/status";
    topics.bossConfig = base + "/config";
    topics.bossCommand = base + "/command";
    topics.bossOta = base + "/ota";
  }
  return topics;
}

std::vector<String> buildSubscriptionTopics(const RuntimeConfig& config) {
  TopicSet topics = buildTopics(config);
  std::vector<String> result;
  result.push_back(topics.deviceConfig);
  result.push_back(topics.deviceOta);
  result.push_back(topics.allOta);
  if (topics.bossConfig.length() > 0) result.push_back(topics.bossConfig);
  if (topics.bossCommand.length() > 0) result.push_back(topics.bossCommand);
  if (topics.bossOta.length() > 0) result.push_back(topics.bossOta);
  return result;
}

}  // namespace boss_target
}  // namespace battlebang
