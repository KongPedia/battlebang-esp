#include "topics.h"

#include <bb_esp_core/mqtt/device_topics.h>
#include <bb_esp_core/mqtt/topic_utils.h>

namespace battlebang {
namespace boss_target {

TopicSet buildTopics(const RuntimeConfig& config) {
  const String root = battlebang::esp::mqtt::normalizeRootOrDefault(config.mqttRoot);
  String topicError;
  battlebang::esp::mqtt::DeviceTopics deviceTopics;

  TopicSet topics;
  if (!battlebang::esp::mqtt::makeDeviceTopicsChecked(root, "boss_target", config.deviceId, deviceTopics, topicError)) {
    return topics;
  }
  topics.deviceStatus = deviceTopics.status;
  topics.deviceConfig = deviceTopics.config;
  topics.deviceOta = deviceTopics.ota;
  battlebang::esp::mqtt::makeAllOtaTopicChecked(root, "boss_targets", topics.allOta, topicError);

  if (config.configured && config.bossId.length() > 0) {
    battlebang::esp::mqtt::EntityTopics entityTopics;
    if (battlebang::esp::mqtt::makeEntityTopicsChecked(
            root, "boss_targets", config.bossId, entityTopics, topicError, "boss_id")) {
      topics.bossStatus = entityTopics.status;
      topics.bossConfig = entityTopics.config;
      topics.bossCommand = entityTopics.command;
      topics.bossOta = entityTopics.ota;
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
  if (topics.bossConfig.length() > 0) result.push_back(topics.bossConfig);
  if (topics.bossCommand.length() > 0) result.push_back(topics.bossCommand);
  if (topics.bossOta.length() > 0) result.push_back(topics.bossOta);
  return result;
}

}  // namespace boss_target
}  // namespace battlebang
