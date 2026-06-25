#include "topics.h"

#include <bb_esp_core/mqtt/device_topics.h>
#include <bb_esp_core/mqtt/topic_utils.h>

namespace battlebang {
namespace turret_fleet {

TopicSet buildTopics(const RuntimeConfig& config) {
  const String root = battlebang::esp::mqtt::normalizeRootOrDefault(config.mqttRoot);
  String topicError;
  battlebang::esp::mqtt::DeviceTopics deviceTopics;

  TopicSet topics;
  if (!battlebang::esp::mqtt::makeDeviceTopicsChecked(root, config.deviceId, deviceTopics, topicError)) {
    return topics;
  }
  topics.deviceStatus = deviceTopics.status;
  topics.deviceConfig = deviceTopics.config;
  topics.deviceOta = deviceTopics.ota;
  battlebang::esp::mqtt::makeAllOtaTopicChecked(root, "turrets", topics.allOta, topicError);

  if (config.configured && config.turretId.length() > 0) {
    battlebang::esp::mqtt::EntityTopics entityTopics;
    if (battlebang::esp::mqtt::makeEntityTopicsChecked(
            root, "turrets", config.turretId, entityTopics, topicError, "turret_id")) {
      topics.turretStatus = entityTopics.status;
      topics.turretConfig = entityTopics.config;
      topics.turretCommand = entityTopics.command;
      topics.turretOta = entityTopics.ota;
    }
  }
  return topics;
}

std::vector<String> buildSubscriptionTopics(const RuntimeConfig& config) {
  TopicSet topics = buildTopics(config);
  std::vector<String> result;
  if (topics.deviceConfig.length() > 0) result.push_back(topics.deviceConfig);
  if (topics.deviceOta.length() > 0) result.push_back(topics.deviceOta);
  if (topics.allOta.length() > 0) result.push_back(topics.allOta);
  if (topics.turretConfig.length() > 0) result.push_back(topics.turretConfig);
  if (topics.turretOta.length() > 0) result.push_back(topics.turretOta);
  if (topics.turretCommand.length() > 0) result.push_back(topics.turretCommand);
  return result;
}

}  // namespace turret_fleet
}  // namespace battlebang
