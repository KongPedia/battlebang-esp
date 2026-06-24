#pragma once

#include <Arduino.h>

namespace battlebang::esp::mqtt {

inline String trimSlashes(const String& value) {
  int start = 0;
  int end = static_cast<int>(value.length()) - 1;
  while (start <= end && value[start] == '/') ++start;
  while (end >= start && value[end] == '/') --end;
  if (end < start) return String();
  return value.substring(start, end + 1);
}

inline String normalizeRootOrDefault(const String& root, const String& fallback = String("battlebang")) {
  const String normalized = trimSlashes(root);
  return normalized.length() == 0 ? fallback : normalized;
}

inline bool isSafeTopicSegment(const String& segment) {
  if (segment.length() == 0) return false;
  for (size_t i = 0; i < segment.length(); ++i) {
    const char c = segment[i];
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
    if (!ok) return false;
  }
  return true;
}

inline bool hasEmptyTopicSegment(const String& path) {
  if (path.length() == 0) return true;
  if (path[0] == '/' || path[path.length() - 1] == '/') return true;
  return path.indexOf("//") >= 0;
}

inline bool isSafeTopicPath(const String& path) {
  if (hasEmptyTopicSegment(path)) return false;
  size_t segmentStart = 0;
  for (size_t i = 0; i <= path.length(); ++i) {
    if (i != path.length() && path[i] != '/') continue;
    if (!isSafeTopicSegment(path.substring(segmentStart, i))) return false;
    segmentStart = i + 1;
  }
  return true;
}

inline bool normalizeConfiguredRoot(String& root, String& error) {
  root = trimSlashes(root);
  if (root.length() == 0) {
    error = "mqtt.root must not be empty";
    return false;
  }
  if (hasEmptyTopicSegment(root)) {
    error = "mqtt.root must not contain empty path segments";
    return false;
  }
  if (!isSafeTopicPath(root)) {
    error = "mqtt.root must use slash-separated topic segments with only A-Z, a-z, 0-9, '_', '-', or '.'";
    return false;
  }
  return true;
}

inline bool normalizeRootOrError(const String& root, String& normalized, String& error) {
  normalized = normalizeRootOrDefault(root);
  return normalizeConfiguredRoot(normalized, error);
}

inline String joinTopic(const String& a, const String& b) {
  const String left = trimSlashes(a);
  const String right = trimSlashes(b);
  if (left.length() == 0) return right;
  if (right.length() == 0) return left;
  return left + "/" + right;
}

inline String joinTopic(const String& a, const String& b, const String& c) {
  return joinTopic(joinTopic(a, b), c);
}

inline String joinTopic(const String& a, const String& b, const String& c, const String& d) {
  return joinTopic(joinTopic(a, b, c), d);
}

}  // namespace battlebang::esp::mqtt
