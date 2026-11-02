#pragma once

#include "version_autogen.h"

#ifndef BB_GO2_FULL_APP_NAME
#define BB_GO2_FULL_APP_NAME "battlebang-go2-full-usb"
#endif

#ifndef BB_GO2_FULL_HARDWARE
#define BB_GO2_FULL_HARDWARE "esp32dev-go2-full-button-relay-2ch-v1"
#endif

#ifndef BB_GO2_FULL_OTA_CHANNEL
#define BB_GO2_FULL_OTA_CHANNEL "go2-full-usb-2ch"
#endif

#ifndef BB_GO2_FULL_STABLE_TAG
#define BB_GO2_FULL_STABLE_TAG "go2-full-usb-2ch-latest"
#endif

#ifndef BB_GO2_FULL_MANIFEST_NAME
#define BB_GO2_FULL_MANIFEST_NAME "go2-full-usb-2ch-manifest.json"
#endif

#ifndef BB_GO2_FULL_VERSION
#define BB_GO2_FULL_VERSION "0.3.0-local"
#endif

#ifndef BB_GO2_FULL_BUILD
#define BB_GO2_FULL_BUILD 1
#endif

#ifndef BB_GO2_FULL_GIT_SHA
#define BB_GO2_FULL_GIT_SHA "local"
#endif

#ifndef BB_GO2_FULL_RELEASE_REPO
#define BB_GO2_FULL_RELEASE_REPO "KongPedia/battlebang-esp"
#endif

#ifndef BB_GO2_FULL_LATEST_MANIFEST_URL
#define BB_GO2_FULL_LATEST_MANIFEST_URL \
  "https://github.com/" BB_GO2_FULL_RELEASE_REPO "/releases/download/" \
      BB_GO2_FULL_STABLE_TAG "/" BB_GO2_FULL_MANIFEST_NAME
#endif
