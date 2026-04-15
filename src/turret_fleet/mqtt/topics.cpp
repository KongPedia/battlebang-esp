#include "topics.h"

namespace battlebang {
namespace turret_fleet {
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
  topics.allOta = root + "/turrets/all/ota";

  if (config.configured && config.turretId.length() > 0) {
    const String turretBase = root + "/turrets/" + config.turretId;
    topics.turretStatus = turretBase + "/status";
    topics.turretConfig = turretBase + "/config";
    topics.turretOta = turretBase + "/ota";
    topics.turretCommand = turretBase + "/command";
  }
  return topics;
}

std::vector<String> buildSubscriptionTopics(const RuntimeConfig& config) {
  TopicSet topics = buildTopics(config);
  std::vector<String> result;
  result.push_back(topics.deviceConfig);
  result.push_back(topics.deviceOta);
  result.push_back(topics.allOta);
  if (topics.turretConfig.length() > 0) result.push_back(topics.turretConfig);
  if (topics.turretOta.length() > 0) result.push_back(topics.turretOta);
  if (topics.turretCommand.length() > 0) result.push_back(topics.turretCommand);
  return result;
}

}  // namespace turret_fleet
}  // namespace battlebang
