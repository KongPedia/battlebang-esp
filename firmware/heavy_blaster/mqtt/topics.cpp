#include "topics.h"

namespace battlebang {
namespace heavy_blaster {
namespace {
String cleanRoot(const String& root) {
  String out = root;
  while (out.endsWith("/")) out.remove(out.length() - 1);
  while (out.startsWith("/")) out.remove(0, 1);
  return out.length() == 0 ? String("battlebang") : out;
}
}  // namespace

TopicSet buildTopics(const RuntimeConfig& config) {
  const String root = cleanRoot(config.mqttRoot);
  TopicSet topics;
  topics.deviceStatus = root + "/devices/" + config.deviceId + "/status";
  topics.deviceConfig = root + "/devices/" + config.deviceId + "/config";
  topics.deviceOta = root + "/devices/" + config.deviceId + "/ota";
  topics.allOta = root + "/heavy-blasters/all/ota";
  if (config.configured && config.blasterId.length() > 0) {
    const String base = root + "/heavy-blasters/" + config.blasterId;
    topics.blasterStatus = base + "/status";
    topics.blasterConfig = base + "/config";
    topics.blasterCommand = base + "/command";
    topics.blasterOta = base + "/ota";
  }
  return topics;
}

std::vector<String> buildSubscriptionTopics(const RuntimeConfig& config) {
  TopicSet topics = buildTopics(config);
  std::vector<String> result;
  result.push_back(topics.deviceConfig);
  result.push_back(topics.deviceOta);
  result.push_back(topics.allOta);
  if (topics.blasterConfig.length() > 0) result.push_back(topics.blasterConfig);
  if (topics.blasterCommand.length() > 0) result.push_back(topics.blasterCommand);
  if (topics.blasterOta.length() > 0) result.push_back(topics.blasterOta);
  return result;
}

}  // namespace heavy_blaster
}  // namespace battlebang
