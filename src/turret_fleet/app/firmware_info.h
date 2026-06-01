#pragma once

// CI may generate this file before build. It is intentionally not required for
// local development builds.
#if __has_include("version_autogen.h")
#include "version_autogen.h"
#endif

#ifndef BB_TURRET_FLEET_APP_NAME
#define BB_TURRET_FLEET_APP_NAME "battlebang-turret-fleet"
#endif

#ifndef BB_TURRET_FLEET_HARDWARE
#define BB_TURRET_FLEET_HARDWARE "esp32dev-turret-v2"
#endif

#ifndef BB_TURRET_FLEET_VERSION
#define BB_TURRET_FLEET_VERSION "0.1.0-dev"
#endif

#ifndef BB_TURRET_FLEET_BUILD
#define BB_TURRET_FLEET_BUILD 1
#endif

#ifndef BB_TURRET_FLEET_GIT_SHA
#define BB_TURRET_FLEET_GIT_SHA "local"
#endif

#ifndef BB_TURRET_FLEET_RELEASE_REPO
#define BB_TURRET_FLEET_RELEASE_REPO "KongPedia/battlebang-esp"
#endif

#ifndef BB_TURRET_FLEET_LATEST_MANIFEST_URL
#define BB_TURRET_FLEET_LATEST_MANIFEST_URL \
  "https://github.com/KongPedia/battlebang-esp/releases/latest/download/manifest.json"
#endif
