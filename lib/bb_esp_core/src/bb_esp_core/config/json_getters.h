#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <stdint.h>

namespace battlebang::esp::config {

inline String getStringOr(JsonVariantConst value, const String& fallback) {
  return value.is<const char*>() ? String(value.as<const char*>()) : fallback;
}

inline bool getBoolOr(JsonVariantConst value, bool fallback) {
  return value.is<bool>() ? value.as<bool>() : fallback;
}

inline int getIntOr(JsonVariantConst value, int fallback) {
  return value.is<int>() ? value.as<int>() : fallback;
}

inline uint32_t getUInt32Or(JsonVariantConst value, uint32_t fallback) {
  return value.is<uint32_t>() ? value.as<uint32_t>() : fallback;
}

}  // namespace battlebang::esp::config
