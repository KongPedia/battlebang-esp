#include "topics.h"

#include <bb_esp_core/mqtt/device_topics.h>
#include <bb_esp_core/mqtt/topic_utils.h>

namespace battlebang {
namespace heavy_blaster {

TopicSet buildTopics(const RuntimeConfig& config) {
  const String root = battlebang::esp::mqtt::normalizeRootOrDefault(config.mqttRoot);
  String topicError;
  battlebang::esp::mqtt::DeviceTopics deviceTopics;

  TopicSet topics;
  if (!battlebang::esp::mqtt::makeDeviceTopicsChecked(root, "heavy_blaster", config.deviceId, deviceTopics, topicError)) {
    return topics;
  }
  topics.deviceStatus = deviceTopics.status;
  topics.deviceConfig = deviceTopics.config;
  topics.deviceOta = deviceTopics.ota;
  battlebang::esp::mqtt::makeAllOtaTopicChecked(root, "heavy-blasters", topics.allOta, topicError);

  if (config.configured && config.blasterId.length() > 0) {
    battlebang::esp::mqtt::EntityTopics entityTopics;
    if (battlebang::esp::mqtt::makeEntityTopicsChecked(
            root, "heavy-blasters", config.blasterId, entityTopics, topicError, "blaster_id")) {
      topics.blasterStatus = entityTopics.status;
      topics.blasterConfig = entityTopics.config;
      topics.blasterCommand = entityTopics.command;
      topics.blasterOta = entityTopics.ota;
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
  if (topics.blasterConfig.length() > 0) result.push_back(topics.blasterConfig);
  if (topics.blasterCommand.length() > 0) result.push_back(topics.blasterCommand);
  if (topics.blasterOta.length() > 0) result.push_back(topics.blasterOta);
  return result;
}

}  // namespace heavy_blaster
}  // namespace battlebang
