"""Host-executable contracts for the extracted Go2 Full piezo module."""

from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def piezo_core_binary(tmp_path_factory: pytest.TempPathFactory) -> Path:
    compiler = shutil.which("g++") or shutil.which("clang++")
    assert compiler, "A native C++ compiler is required"
    tmp_path = tmp_path_factory.mktemp("go2_piezo_core")
    source = tmp_path / "main.cpp"
    source.write_text(
        r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include "go2_full/piezo/piezo_sensor_core.h"
using namespace go2;

static PiezoSensorConfig config() {
  PiezoSensorConfig cfg;
  cfg.leftPin = 34; cfg.rightPin = 35; cfg.frontPin = 32; cfg.debugDoPin = -1;
  cfg.channelEnableMask = PIEZO_CHANNEL_LEFT | PIEZO_CHANNEL_RIGHT | PIEZO_CHANNEL_FRONT;
  cfg.thresholdRaw = 2400; cfg.rearmRaw = 1800; cfg.captureWindowMs = 30;
  cfg.rearmStableMs = 30; cfg.hitCooldownMs = 0; cfg.debugPeriodMs = 1000;
  return cfg;
}
static PiezoSample sample(int left, int right, int front) {
  PiezoSample value; value.left = left; value.right = right; value.front = front; return value;
}
static void qualify_and_arm(PiezoSensorCore& core, uint32_t start = 100) {
  PiezoHitCandidate out;
  assert(!core.processSample(sample(100, 100, 100), start, out));
  assert(!core.processSample(sample(100, 100, 100), start + 30, out));
  assert(!core.processSample(sample(100, 100, 100), start + 60, out));
  assert(core.diagnostics().armed);
}

int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string test = argv[1];
  PiezoHitCandidate out;
  PiezoSample initial = sample(-1, -1, -1);

  if (test == "off") {
    PiezoSensorCore core; core.begin(config(), false, initial, 0);
    assert(!core.processSample(sample(3000, 3000, 3000), 100, out));
    PiezoDiagnostics d = core.diagnostics();
    assert(!d.enabled && !d.armed && !d.capturing);
    return 0;
  }
  if (test == "qualify") {
    PiezoSensorCore core; core.begin(config(), true, initial, 0);
    assert(!core.processSample(sample(100, 2500, -1), 100, out));
    assert(!core.processSample(sample(100, 2500, -1), 130, out));
    PiezoDiagnostics d = core.diagnostics();
    assert(d.left.qualified && !d.right.qualified && !d.front.qualified);
    assert(!core.processSample(sample(2500, 100, 100), 160, out));
    assert(!core.processSample(sample(2500, 100, 100), 190, out));
    d = core.diagnostics();
    assert(d.left.qualified && d.right.qualified && d.front.qualified);
    return 0;
  }
  if (test == "mask_invalid") {
    PiezoSensorConfig cfg = config();
    cfg.leftPin = -1; cfg.channelEnableMask = PIEZO_CHANNEL_LEFT | PIEZO_CHANNEL_RIGHT;
    PiezoSensorCore core; core.begin(cfg, true, initial, 0);
    assert(!core.processSample(sample(100, 100, 4095), 100, out));
    assert(!core.processSample(sample(100, 100, 4095), 130, out));
    PiezoDiagnostics d = core.diagnostics();
    assert(!d.left.enabled && !d.left.qualified);
    assert(d.right.enabled && d.right.qualified);
    assert(!d.front.enabled && !d.front.qualified);
    return 0;
  }
  if (test == "peak") {
    PiezoSensorCore core; core.begin(config(), true, initial, 0); qualify_and_arm(core);
    assert(!core.processSample(sample(2500, 2600, 2700), 200, out));
    assert(!core.processSample(sample(2800, 3000, 2900), 210, out));
    assert(core.processSample(sample(2900, 2950, 2850), 230, out));
    assert(out.targetId == 2 && out.peakRaw == 3000 && out.thresholdRaw == 2400);
    assert(out.eventTsMs == 200 && std::strcmp(out.source, "piezo:right") == 0);
    return 0;
  }
  if (test == "cooldown") {
    PiezoSensorConfig cfg = config(); cfg.hitCooldownMs = 100;
    PiezoSensorCore core; core.begin(cfg, true, initial, 0); qualify_and_arm(core);
    assert(!core.processSample(sample(2500, 100, 100), 200, out));
    assert(core.processSample(sample(2600, 100, 100), 230, out));
    assert(!core.processSample(sample(100, 100, 100), 300, out));
    assert(!core.processSample(sample(100, 100, 100), 330, out));
    assert(!core.diagnostics().armed);
    assert(!core.processSample(sample(100, 100, 100), 360, out));
    assert(core.diagnostics().armed);
    return 0;
  }
  if (test == "toggle") {
    PiezoSensorCore core; core.begin(config(), true, initial, 0); qualify_and_arm(core);
    assert(!core.processSample(sample(2600, 100, 100), 200, out));
    assert(core.diagnostics().capturing);
    core.setEnabled(false, initial, 210);
    assert(!core.diagnostics().armed && !core.diagnostics().capturing);
    assert(!core.processSample(sample(3000, 3000, 3000), 240, out));
    core.setEnabled(true, initial, 250);
    assert(!core.processSample(sample(100, 100, 100), 250, out));
    assert(!core.diagnostics().capturing);
    return 0;
  }
  if (test == "wrap") {
    PiezoSensorCore core; core.begin(config(), true, initial, 0);
    assert(!core.processSample(sample(100, 100, 100), 0xfffffff0u, out));
    assert(!core.processSample(sample(100, 100, 100), 0x0eu, out));
    assert(!core.processSample(sample(100, 100, 100), 0x2cu, out));
    assert(core.diagnostics().armed);
    return 0;
  }
  if (test == "diagnostics") {
    PiezoSensorCore core; core.begin(config(), true, initial, 0);
    assert(!core.processSample(sample(100, 200, 300), 100, out));
    assert(!core.processSample(sample(200, 300, 400), 130, out));
    PiezoDiagnostics snapshot;
    assert(!core.takeDebugSnapshot(999, snapshot));
    assert(core.takeDebugSnapshot(1000, snapshot));
    // PR59 resetStats seeds min/max with raw=0 for an unavailable initial sample.
    assert(snapshot.sampleCount == 2 && snapshot.minRaw == 0 && snapshot.maxRaw == 400);
    assert(snapshot.averageRaw == 200);
    assert(core.diagnostics().sampleCount == 0);
    return 0;
  }
  return 2;
}
''',
        encoding="utf-8",
    )
    binary = tmp_path / "piezo_core"
    subprocess.run(
        [
            compiler,
            "-std=c++11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(ROOT / "firmware"),
            str(source),
            str(ROOT / "firmware/go2_full/piezo/piezo_sensor_core.cpp"),
            "-o",
            str(binary),
        ],
        check=True,
    )
    return binary


@pytest.mark.parametrize(
    "case",
    ("off", "qualify", "mask_invalid", "peak", "cooldown", "toggle", "wrap", "diagnostics"),
)
def test_extracted_piezo_core_preserves_pr59_behavior(piezo_core_binary: Path, case: str) -> None:
    subprocess.run([str(piezo_core_binary), case], check=True)
