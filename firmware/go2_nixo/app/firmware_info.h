#pragma once

#include "version_autogen.h"

#ifndef BB_GO2_NIXO_APP_NAME
#define BB_GO2_NIXO_APP_NAME "battlebang-go2-nixo"
#endif

#ifndef BB_GO2_NIXO_HARDWARE
#define BB_GO2_NIXO_HARDWARE "esp32dev-go2-nixo-relay-1ch-v1"
#endif

#ifndef BB_GO2_NIXO_OTA_CHANNEL
#define BB_GO2_NIXO_OTA_CHANNEL "go2-nixo-1ch"
#endif

#ifndef BB_GO2_NIXO_STABLE_TAG
#define BB_GO2_NIXO_STABLE_TAG "go2-nixo-1ch-latest"
#endif

#ifndef BB_GO2_NIXO_MANIFEST_NAME
#define BB_GO2_NIXO_MANIFEST_NAME "go2-nixo-1ch-manifest.json"
#endif

#ifndef BB_GO2_NIXO_VERSION
#define BB_GO2_NIXO_VERSION "0.1.0-local"
#endif

#ifndef BB_GO2_NIXO_BUILD
#define BB_GO2_NIXO_BUILD 1
#endif

#ifndef BB_GO2_NIXO_GIT_SHA
#define BB_GO2_NIXO_GIT_SHA "local"
#endif

#ifndef BB_GO2_NIXO_RELEASE_REPO
#define BB_GO2_NIXO_RELEASE_REPO "KongPedia/battlebang-esp"
#endif

#ifndef BB_GO2_NIXO_LATEST_MANIFEST_URL
#define BB_GO2_NIXO_LATEST_MANIFEST_URL \
  "https://github.com/" BB_GO2_NIXO_RELEASE_REPO "/releases/download/" \
      BB_GO2_NIXO_STABLE_TAG "/" BB_GO2_NIXO_MANIFEST_NAME
#endif
