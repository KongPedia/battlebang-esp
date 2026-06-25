#pragma once

#include <Arduino.h>

#include <bb_esp_core/mqtt/topic_utils.h>

namespace battlebang::esp::mqtt {

struct DeviceTopics {
  String status;
  String config;
  String ota;
};

struct EntityTopics {
  String status;
  String config;
  String command;
  String ota;
};

inline DeviceTopics makeDeviceTopics(const String& root, const String& deviceId) {
  const String base = joinTopic(normalizeRootOrDefault(root), "devices", deviceId);
  return DeviceTopics{joinTopic(base, "status"), joinTopic(base, "config"), joinTopic(base, "ota")};
}

inline EntityTopics makeEntityTopics(const String& root, const String& collection, const String& entityId) {
  const String base = joinTopic(normalizeRootOrDefault(root), collection, entityId);
  return EntityTopics{joinTopic(base, "status"), joinTopic(base, "config"), joinTopic(base, "command"), joinTopic(base, "ota")};
}

inline String makeAllOtaTopic(const String& root, const String& collection) {
  return joinTopic(normalizeRootOrDefault(root), collection, "all", "ota");
}

inline bool makeDeviceTopicsChecked(const String& root, const String& deviceId, DeviceTopics& out, String& error) {
  String normalizedRoot;
  if (!normalizeRootOrError(root, normalizedRoot, error)) return false;
  if (!isSafeTopicSegment(deviceId)) {
    error = "device_id must use only A-Z, a-z, 0-9, '_', '-', or '.'";
    return false;
  }
  out = makeDeviceTopics(normalizedRoot, deviceId);
  return true;
}

inline bool makeEntityTopicsChecked(const String& root,
                                    const String& collection,
                                    const String& entityId,
                                    EntityTopics& out,
                                    String& error,
                                    const char* entityFieldName = "entity_id") {
  String normalizedRoot;
  if (!normalizeRootOrError(root, normalizedRoot, error)) return false;
  if (!isSafeTopicSegment(collection)) {
    error = "collection must use only A-Z, a-z, 0-9, '_', '-', or '.'";
    return false;
  }
  if (!isSafeTopicSegment(entityId)) {
    error = String(entityFieldName) + " must use only A-Z, a-z, 0-9, '_', '-', or '.'";
    return false;
  }
  out = makeEntityTopics(normalizedRoot, collection, entityId);
  return true;
}

inline bool makeAllOtaTopicChecked(const String& root, const String& collection, String& out, String& error) {
  String normalizedRoot;
  if (!normalizeRootOrError(root, normalizedRoot, error)) return false;
  if (!isSafeTopicSegment(collection)) {
    error = "collection must use only A-Z, a-z, 0-9, '_', '-', or '.'";
    return false;
  }
  out = makeAllOtaTopic(normalizedRoot, collection);
  return true;
}

}  // namespace battlebang::esp::mqtt
