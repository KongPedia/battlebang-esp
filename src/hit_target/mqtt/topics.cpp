#include "topics.h"

namespace battlebang {
namespace hit_target {
namespace {
String cleanRoot(const String& root) {
  String out = root;
  while (out.endsWith("/")) out.remove(out.length() - 1);
  return out.length() == 0 ? String("battlebang") : out;
}

String linkedDeviceCollection(const String& kind) {
  String normalized = kind;
  normalized.trim();
  normalized.toLowerCase();
  if (normalized == "turret") return "turrets";
  return "devices/" + normalized;
}
}

TopicSet buildTopics(const RuntimeConfig& config) {
  const String root = cleanRoot(config.mqttRoot);
  TopicSet topics;
  topics.deviceStatus = root + "/devices/hit_target/" + config.deviceId + "/status";
  topics.deviceConfig = root + "/devices/hit_target/" + config.deviceId + "/config";
  topics.deviceOta = root + "/devices/hit_target/" + config.deviceId + "/ota";
  topics.allOta = root + "/hit_targets/all/ota";
  if (config.configured && config.targetId.length() > 0) {
    const String base = root + "/hit_targets/" + config.targetId;
    topics.targetStatus = base + "/status";
    topics.targetConfig = base + "/config";
    topics.targetCommand = base + "/command";
    topics.targetOta = base + "/ota";
  }
  if (config.activation.mode == "linked_device" && config.activation.linkedDeviceId.length() > 0) {
    topics.linkedDeviceStatus = root + "/" + linkedDeviceCollection(config.activation.linkedDeviceKind) + "/" +
                                config.activation.linkedDeviceId + "/status";
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
  if (topics.linkedDeviceStatus.length() > 0) result.push_back(topics.linkedDeviceStatus);
  return result;
}

}  // namespace hit_target
}  // namespace battlebang
