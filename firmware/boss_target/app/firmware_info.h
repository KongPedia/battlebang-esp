#pragma once

#include "version_autogen.h"

#ifndef BB_BOSS_TARGET_APP_NAME
#define BB_BOSS_TARGET_APP_NAME "battlebang-boss-target"
#endif

#ifndef BB_BOSS_TARGET_HARDWARE
#define BB_BOSS_TARGET_HARDWARE "esp32dev-boss-target-ring-v1"
#endif

#ifndef BB_BOSS_TARGET_VERSION
#define BB_BOSS_TARGET_VERSION "0.1.0-local"
#endif

#ifndef BB_BOSS_TARGET_BUILD
#define BB_BOSS_TARGET_BUILD 1
#endif

#ifndef BB_BOSS_TARGET_GIT_SHA
#define BB_BOSS_TARGET_GIT_SHA "local"
#endif

#ifndef BB_BOSS_TARGET_RELEASE_REPO
#define BB_BOSS_TARGET_RELEASE_REPO "KongPedia/battlebang-esp"
#endif

#ifndef BB_BOSS_TARGET_LATEST_MANIFEST_URL
#define BB_BOSS_TARGET_LATEST_MANIFEST_URL \
  "https://github.com/" BB_BOSS_TARGET_RELEASE_REPO "/releases/download/boss-target-latest/boss-target-manifest.json"
#endif
