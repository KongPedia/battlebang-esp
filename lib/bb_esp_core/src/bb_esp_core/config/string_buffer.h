#pragma once

#include <Arduino.h>
#include <stddef.h>

namespace battlebang::esp::config {

inline bool copyToFixedBuffer(const String& value, char* buffer, size_t length) {
  if (buffer == nullptr || length == 0) return false;
  value.toCharArray(buffer, length);
  return value.length() < length;
}

inline bool copyToFixedBuffer(const char* value, char* buffer, size_t length) {
  return copyToFixedBuffer(value == nullptr ? String() : String(value), buffer, length);
}

}  // namespace battlebang::esp::config
