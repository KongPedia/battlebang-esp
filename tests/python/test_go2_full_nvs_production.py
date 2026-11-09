"""Executable contracts for the production Go2 Full NVS transaction bodies."""

from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "firmware/go2_full/config/runtime_config.cpp").read_text(
    encoding="utf-8"
)


def extract_cpp_function(signature: str) -> str:
    start = SOURCE.index(signature)
    brace = SOURCE.index("{", start)
    depth = 0
    for index in range(brace, len(SOURCE)):
        if SOURCE[index] == "{":
            depth += 1
        elif SOURCE[index] == "}":
            depth -= 1
            if depth == 0:
                return SOURCE[start : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


@pytest.fixture(scope="session")
def production_migration_harness(tmp_path_factory: pytest.TempPathFactory) -> Path:
    """Compile the exact production transaction functions with an in-memory NVS fake."""

    compiler = shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        pytest.skip("C++ compiler is required for production NVS migration tests")
    functions = "\n\n".join(
        extract_cpp_function(signature)
        for signature in (
            "static bool canonicalStorageReady()",
            "bool loadRuntimeConfigFromNvs(RuntimeConfig& config)",
            "bool saveRuntimeConfigToNvs(const RuntimeConfig& config)",
            "bool clearRuntimeConfigNvs()",
        )
    )
    source = tmp_path_factory.mktemp("go2_full_nvs") / "migration_harness.cpp"
    binary = source.with_suffix("")
    source.write_text(CPP_PREAMBLE + functions + CPP_TESTS, encoding="utf-8")
    subprocess.run(
        [
            compiler,
            "-std=c++11",
            "-Wall",
            "-Wextra",
            "-Werror",
            str(source),
            "-o",
            str(binary),
        ],
        check=True,
        cwd=ROOT,
        text=True,
        capture_output=True,
    )
    return binary


@pytest.mark.parametrize(
    "scenario",
    [
        "success",
        "canonical",
        "fail-config",
        "fail-hardware",
        "fail-readback",
        "fail-serialize",
        "fail-marker",
        "clear",
    ],
)
def test_production_nvs_transaction_handles_success_failure_and_restart(
    production_migration_harness: Path, scenario: str
) -> None:
    subprocess.run(
        [str(production_migration_harness), scenario],
        check=True,
        cwd=ROOT,
        text=True,
        capture_output=True,
    )


CPP_PREAMBLE = r"""
#include <stdint.h>
#include <stdlib.h>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using String = std::string;

namespace go2 {
struct CommonConfig {
  bool configured = false;
  String robotId;
  String otaChannel;
  String otaPublicManifestUrl;
  String otaLocalMirrorUrl;
  uint32_t otaDesiredBuild = 0;
  bool otaAutoCheckEnabled = false;
  bool otaCommandCenterControlled = true;
  bool otaApplyOnlyInSafeState = true;
};
struct HitConfig {
  uint8_t piezoChannelEnableMask = 3;
  uint8_t hpBarGroupCount = 28;
  uint8_t hpBarColorOrder = 0;
  uint32_t tuning = 0;
};
struct BlasterConfig { String blasterId; uint32_t tuning = 0; };
struct RuntimeConfig { CommonConfig common; HitConfig hit; BlasterConfig blaster; };

const char* kConfigNamespace = "go2_full";
const char* kHardwareNamespace = "go2_full_hw";
const char* kPreviousConfigNamespace = "go2_nixo";
const char* kPreviousHardwareNamespace = "go2_nixo_hw";
const char* kMigrationCompleteKey = "migration_done";

struct Store {
  RuntimeConfig oldConfig;
  RuntimeConfig newConfig;
  uint8_t oldMask = 3, oldGroups = 28, oldColor = 0;
  uint8_t newMask = 3, newGroups = 28, newColor = 0;
  bool oldConfigured = false;
  bool newConfigured = false;
  bool marker = false;
  String fail;
  int oldLoads = 0;
  int newLoads = 0;
  std::vector<String> log;
} store;

void reset() {
  store = Store();
  store.oldConfig.common.robotId = "go2_06";
  store.oldConfig.common.otaChannel = "go2-nixo-old";
  store.oldConfig.common.otaPublicManifestUrl = "old-manifest";
  store.oldConfig.common.otaLocalMirrorUrl = "old-mirror";
  store.oldConfig.common.otaDesiredBuild = 99;
  store.oldConfig.common.otaAutoCheckEnabled = true;
  store.oldConfig.hit.tuning = 42;
  store.oldConfig.blaster.blasterId = "nixo_go2_06";
  store.oldConfig.blaster.tuning = 77;
  store.oldConfigured = true;
  store.oldMask = 2;
  store.oldGroups = 27;
  store.oldColor = 1;
  store.newConfig.common.robotId = "stale-partial";
  store.newMask = 1;
  store.newGroups = 1;
}

struct Preferences {
  String ns;
  bool readOnly = true;
  bool getBool(const char* key, bool fallback) const {
    return ns == kHardwareNamespace && String(key) == kMigrationCompleteKey
               ? store.marker : fallback;
  }
  size_t putUChar(const char* key, uint8_t value) {
    store.log.push_back(String("hardware:") + key);
    if (store.fail == "hardware") return 0;
    if (String(key) == "piezo_mask") store.newMask = value;
    if (String(key) == "bar_groups") store.newGroups = value;
    if (String(key) == "bar_color") store.newColor = value;
    return 1;
  }
  size_t putBool(const char* key, bool value) {
    store.log.push_back(String("marker:") + key);
    if (store.fail == "marker") return 0;
    if (String(key) == kMigrationCompleteKey) store.marker = value;
    return 1;
  }
};

namespace battlebang { namespace esp { namespace nvs {
struct ScopedPreferences {
  Preferences prefs;
  bool begin(const char* ns, bool readOnly) {
    prefs.ns = ns;
    prefs.readOnly = readOnly;
    store.log.push_back(String("begin:") + ns + (readOnly ? ":ro" : ":rw"));
    return true;
  }
  Preferences& preferences() { return prefs; }
  void end() {}
};
bool clearNamespace(const char* ns) {
  store.log.push_back(String("clear:") + ns);
  if (store.fail == "clear") return false;
  if (String(ns) == kConfigNamespace) {
    store.newConfig = RuntimeConfig();
    store.newConfigured = false;
  } else if (String(ns) == kHardwareNamespace) {
    store.newMask = 3;
    store.newGroups = 28;
    store.newColor = 0;
    store.marker = false;
  } else {
    std::cerr << "attempted legacy clear: " << ns << "\n";
    abort();
  }
  return true;
}
bool saveCommonRuntimeConfig(Preferences&, const CommonConfig& value, int) {
  store.log.push_back("save:config");
  if (store.fail == "config") return false;
  store.newConfig.common = value;
  store.newConfigured = value.configured;
  return true;
}
}}}

int commonNvsKeys() { return 0; }
bool saveHitRuntimeConfigToNvs(battlebang::esp::nvs::ScopedPreferences&,
                               const HitConfig& value) {
  if (store.fail != "config") store.newConfig.hit = value;
  return store.fail != "config";
}
bool saveBlasterRuntimeConfigToNvs(battlebang::esp::nvs::ScopedPreferences&,
                                   const BlasterConfig& value) {
  if (store.fail != "config") store.newConfig.blaster = value;
  return store.fail != "config";
}
void normalizeRuntimeConfig(RuntimeConfig&) {}
RuntimeConfig runtimeConfigFromBuild() {
  RuntimeConfig value;
  value.common.otaChannel = "go2-full-usb-2ch";
  value.common.otaPublicManifestUrl = "new-manifest";
  value.common.otaCommandCenterControlled = true;
  value.common.otaApplyOnlyInSafeState = true;
  return value;
}
RuntimeConfig runtimeConfigFromNvsOrBuild() {
  RuntimeConfig value = store.newConfig;
  value.hit.piezoChannelEnableMask = store.newMask;
  value.hit.hpBarGroupCount = store.newGroups;
  value.hit.hpBarColorOrder = store.newColor;
  return value;
}
bool loadStoredRuntimeConfig(RuntimeConfig& value, const char* configNs,
                             const char*, bool) {
  store.log.push_back(String("load:") + configNs);
  if (String(configNs) == kPreviousConfigNamespace) {
    ++store.oldLoads;
    value = store.oldConfig;
    value.hit.piezoChannelEnableMask = store.oldMask;
    value.hit.hpBarGroupCount = store.oldGroups;
    value.hit.hpBarColorOrder = store.oldColor;
    value.common.configured = store.oldConfigured;
    return store.oldConfigured;
  }
  ++store.newLoads;
  value = store.newConfig;
  value.hit.piezoChannelEnableMask = store.newMask;
  value.hit.hpBarGroupCount = store.newGroups;
  value.hit.hpBarColorOrder = store.newColor;
  value.common.configured = store.newConfigured;
  if (store.fail == "readback") value.common.robotId += "-corrupt";
  return store.newConfigured;
}
String runtimeConfigToJson(const RuntimeConfig& value, bool) {
  if (store.fail == "serialize") return "";
  std::ostringstream out;
  out << value.common.configured << '|' << value.common.robotId << '|'
      << value.common.otaChannel << '|' << value.common.otaPublicManifestUrl << '|'
      << value.common.otaLocalMirrorUrl << '|' << value.common.otaDesiredBuild << '|'
      << value.common.otaAutoCheckEnabled << '|'
      << value.common.otaCommandCenterControlled << '|'
      << value.common.otaApplyOnlyInSafeState << '|'
      << unsigned(value.hit.piezoChannelEnableMask) << '|'
      << unsigned(value.hit.hpBarGroupCount) << '|'
      << unsigned(value.hit.hpBarColorOrder) << '|' << value.hit.tuning << '|'
      << value.blaster.blasterId << '|' << value.blaster.tuning;
  return out.str();
}

bool saveRuntimeConfigToNvs(const RuntimeConfig& config);
"""


CPP_TESTS = r"""

int indexOf(const String& prefix) {
  for (size_t i = 0; i < store.log.size(); ++i)
    if (store.log[i].find(prefix) == 0) return int(i);
  return -1;
}
void require(bool value, const char* message) {
  if (!value) {
    std::cerr << message << "\n";
    exit(2);
  }
}
void migrationSuccess() {
  reset();
  RuntimeConfig loaded;
  require(loadRuntimeConfigFromNvs(loaded), "legacy configured result lost");
  require(store.marker, "marker missing");
  require(store.oldLoads == 1 && store.newLoads == 1, "wrong migration reads");
  require(store.newConfig.common.robotId == "go2_06", "identity not migrated");
  require(store.newConfig.hit.tuning == 42 && store.newConfig.blaster.tuning == 77,
          "tuning not migrated");
  require(store.newConfig.common.otaChannel == "go2-full-usb-2ch",
          "OTA channel resurrected");
  require(store.newConfig.common.otaPublicManifestUrl == "new-manifest",
          "OTA manifest resurrected");
  require(store.newConfig.common.otaLocalMirrorUrl.empty(), "OTA mirror resurrected");
  require(store.newConfig.common.otaDesiredBuild == 0 &&
              !store.newConfig.common.otaAutoCheckEnabled,
          "OTA desired/auto resurrected");
  require(store.newMask == 2 && store.newGroups == 27 && store.newColor == 1,
          "hardware calibration not migrated");
  require(indexOf("clear:go2_full") < indexOf("save:config"),
          "partial config not cleared first");
  require(indexOf("save:config") < indexOf("hardware:piezo_mask"),
          "hardware preceded config");
  require(indexOf("hardware:bar_color") < indexOf("load:go2_full"),
          "readback preceded hardware");
  require(indexOf("load:go2_full") < indexOf("marker:migration_done"),
          "marker preceded readback");
}
void canonicalOnly() {
  reset();
  store.marker = true;
  store.newConfigured = true;
  store.newConfig.common.robotId = "canonical";
  RuntimeConfig loaded;
  require(loadRuntimeConfigFromNvs(loaded), "canonical configured result lost");
  require(store.oldLoads == 0 && store.newLoads == 1, "legacy read after marker");
  require(loaded.common.robotId == "canonical", "canonical value not loaded");
}
void failureRetry(const String& stage) {
  reset();
  store.fail = stage;
  RuntimeConfig first;
  loadRuntimeConfigFromNvs(first);
  require(!store.marker, "failed transaction committed marker");
  require(store.oldConfig.common.robotId == "go2_06", "legacy source mutated");
  store.fail.clear();
  RuntimeConfig retry;
  loadRuntimeConfigFromNvs(retry);
  require(store.marker, "retry did not commit marker");
  require(store.oldLoads == 2, "retry did not reread legacy source");
}
void clearNoResurrection() {
  reset();
  store.marker = true;
  store.newConfigured = true;
  store.newConfig.common.robotId = "canonical";
  store.newMask = 2;
  store.newGroups = 27;
  store.newColor = 1;
  require(clearRuntimeConfigNvs(), "clear failed");
  require(store.marker, "clear removed migration marker");
  require(store.newMask == 2 && store.newGroups == 27 && store.newColor == 1,
          "clear removed calibration");
  RuntimeConfig loaded;
  require(!loadRuntimeConfigFromNvs(loaded), "cleared config remained configured");
  require(store.oldLoads == 0, "clear resurrected legacy config");
}
}  // namespace go2

int main(int argc, char** argv) {
  if (argc != 2) return 3;
  const std::string scenario = argv[1];
  if (scenario == "success") go2::migrationSuccess();
  else if (scenario == "canonical") go2::canonicalOnly();
  else if (scenario == "clear") go2::clearNoResurrection();
  else if (scenario.find("fail-") == 0) go2::failureRetry(scenario.substr(5));
  else return 4;
  return 0;
}
"""
