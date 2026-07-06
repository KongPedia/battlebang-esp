#pragma once

#include "version_autogen.h"

#ifndef BB_STATION_APP_NAME
#define BB_STATION_APP_NAME "battlebang-station"
#endif

#ifndef BB_STATION_HARDWARE
#define BB_STATION_HARDWARE "esp32dev-target-station-v1"
#endif

#ifndef BB_STATION_VERSION
#define BB_STATION_VERSION "0.1.0-local"
#endif

#ifndef BB_STATION_BUILD
#define BB_STATION_BUILD 1
#endif

#ifndef BB_STATION_GIT_SHA
#define BB_STATION_GIT_SHA "local"
#endif

#ifndef BB_STATION_RELEASE_REPO
#define BB_STATION_RELEASE_REPO "KongPedia/battlebang-esp"
#endif

#ifndef BB_STATION_LATEST_MANIFEST_URL
#define BB_STATION_LATEST_MANIFEST_URL \
  "https://github.com/" BB_STATION_RELEASE_REPO "/releases/download/station-latest/station-manifest.json"
#endif
