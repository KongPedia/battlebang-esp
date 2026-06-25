#pragma once

#include <stdint.h>

namespace battlebang::esp::app {

struct FirmwareIdentity {
  const char* app = "";
  const char* hardware = "";
  const char* version = "";
  uint32_t build = 0;
  const char* gitSha = "";
  const char* releaseRepo = "";
  const char* latestManifestUrl = "";
};

}  // namespace battlebang::esp::app
