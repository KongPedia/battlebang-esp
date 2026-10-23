"""Lock the local HP-bar indication used while piezo detection is disabled."""

import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = (ROOT / "firmware/go2_nixo/display/bar_display.h").read_text()
SOURCE = (ROOT / "firmware/go2_nixo/display/bar_display.cpp").read_text()


def test_piezo_detection_defaults_enabled_and_exposes_a_runtime_switch() -> None:
    assert "void setPiezoDetectionEnabled(bool enabled);" in HEADER
    assert "bool piezoDetectionEnabled_ = true;" in HEADER
    assert "void BarDisplay::setPiezoDetectionEnabled(bool enabled)" in SOURCE
    assert "piezoDetectionEnabled_ = enabled;" in SOURCE
    assert (
        "dirty_ = true;"
        in SOURCE.split("void BarDisplay::setPiezoDetectionEnabled", 1)[1].split(
            "}", 1
        )[0]
    )


def test_disabled_indicator_is_a_nonblocking_hp_perimeter_overlay() -> None:
    assert "constexpr uint32_t PIEZO_DISABLED_FRAME_MS = 90;" in SOURCE
    renderer = SOURCE.split("void BarDisplay::renderPiezoDisabled", 1)[1].split(
        "void BarDisplay::renderLocal", 1
    )[0]
    assert "frame != lastPiezoDisabledFrame_" in renderer
    assert "groupsForFill(fillRatio, groupCount_)" in renderer
    assert "if (healthyGroups == 0) return;" in renderer
    assert "wireColor(CRGB::White)" in renderer
    assert "delay(" not in renderer
    assert "FastLED.show" not in renderer
    assert "renderBlank();" not in renderer
    assert "CRGB(0, 255, 190)" not in renderer


def test_safety_and_operator_indications_precede_disabled_indicator() -> None:
    tick = SOURCE.split("void BarDisplay::tick", 1)[1].split(
        "bool BarDisplay::startupReady", 1
    )[0]
    assert tick.index("if (diagnosticGroup_ > 0 || diagnosticPixel_ > 0)") < tick.index(
        "startupLoading_ && !startupReady(now)"
    )
    assert tick.index("startupLoading_ && !startupReady(now)") < tick.index(
        "!piezoDetectionEnabled_"
    )
    assert tick.index("if (localDown_)") < tick.index("!piezoDetectionEnabled_")
    assert 'remoteDown_ || remoteMode_ == "down" || remoteMode_ == "disabled"' in tick
    assert tick.index("renderRemote(now);") < tick.index("renderPiezoDisabled(now);")
    assert tick.rindex("renderLocal(now);") < tick.index("renderPiezoDisabled(now);")


def test_actual_cpp_overlay_never_refills_lost_hp_and_tracks_border(tmp_path) -> None:
    compiler = shutil.which("g++") or shutil.which("clang++")
    assert compiler, "Native C++ compiler required"
    renderer = (
        "void BarDisplay::renderPiezoDisabled"
        + SOURCE.split("void BarDisplay::renderPiezoDisabled", 1)[1].split(
            "void BarDisplay::renderLocal", 1
        )[0]
    )
    source = tmp_path / "border.cpp"
    source.write_text(
        r"""
#include <cassert>
#include <cstdint>
struct CRGB { int value; static const CRGB White; };
const CRGB CRGB::White{2};
constexpr uint32_t PIEZO_DISABLED_FRAME_MS = 90;
int groupsForFill(float ratio, int count) {
  int n = int(ratio * count + .5f);
  return n < 0 ? 0 : (n > count ? count : n);
}
struct BarDisplay {
  CRGB leds_[87] = {};
  uint32_t lastPiezoDisabledFrame_ = UINT32_MAX;
  bool dirty_ = false, remoteActive_ = false;
  float remoteFillRatio_ = 0, localRatio = 0;
  int groupCount_ = 29;
  float localFillRatio() const { return localRatio; }
  CRGB wireColor(CRGB c) const { return c; }
  void renderPiezoDisabled(uint32_t now);
};
"""
        + renderer
        + r"""
int main() {
  for (int count : {1, 3, 27, 29}) {
    for (int filled = 0; filled <= count; ++filled) {
      for (bool remote : {false, true}) {
        for (int frame = 0; frame < 2 * count + 2; ++frame) {
          BarDisplay bar;
          bar.groupCount_ = count;
          bar.remoteActive_ = remote;
          bar.localRatio = remote ? 1.f : float(filled) / count;
          bar.remoteFillRatio_ = remote ? float(filled) / count : 1.f;
          for (int group = 0; group < filled; ++group) {
            bar.leds_[group].value = 1;
            bar.leds_[2*count-1-group].value = 1;
            bar.leds_[2*count+group].value = 1;
          }
          bar.renderPiezoDisabled(frame * 90);
          int white = 0;
          for (int group = 0; group < count; ++group) {
            int indexes[] = {group, 2*count-1-group, 2*count+group};
            for (int row = 0; row < 3; ++row) {
              int value = bar.leds_[indexes[row]].value;
              if (group >= filled) assert(value == 0);
              else assert(value == 1 || value == 2);
              if (value == 2) {
                ++white;
                assert(row != 1 || group == 0 || group == filled-1);
              }
            }
          }
          assert(white == (filled ? 1 : 0));
          for (int i = 3*count; i < 87; ++i) assert(bar.leds_[i].value == 0);
        }
      }
    }
  }
}
"""
    )
    # initializer_list supports concise exhaustive width/remote-path fixtures.
    source.write_text("#include <initializer_list>\n" + source.read_text())
    binary = tmp_path / "border"
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
    )
    subprocess.run([str(binary)], check=True)
