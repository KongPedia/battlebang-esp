"""Native switch behavior plus firmware wiring; no hardware is accessed."""

import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MAIN = (ROOT / "firmware/go2_nixo_framed_packet_uart/main.cpp").read_text()


def test_switch_is_default_off_debounced_edge_triggered_and_volatile(tmp_path):
    compiler = shutil.which("g++") or shutil.which("clang++")
    assert compiler, "A native C++ compiler is required"
    source = tmp_path / "switch.cpp"
    source.write_text(r"""
#include <cassert>
#include "firmware/go2_nixo_framed_packet_uart/piezo_enable_switch.h"
int main() {
  go2::PiezoEnableSwitch sw;
  assert(!sw.enabled());
  sw.begin(false, 0);
  assert(!sw.update(true, 10));
  assert(!sw.update(false, 20)); // contact bounce must not toggle
  assert(!sw.update(true, 30));
  assert(!sw.update(true, 79));
  assert(sw.update(true, 80) && sw.enabled());
  assert(!sw.update(true, 10000) && sw.enabled()); // held is one press
  assert(!sw.update(false, 10001));
  assert(!sw.update(false, 10051));
  assert(!sw.update(true, 10100));
  assert(sw.update(true, 10150) && !sw.enabled());
  sw.begin(true, 20000); // held during boot must never arm
  assert(!sw.update(true, 25000) && !sw.enabled());
  assert(!sw.update(false, 25001));
  assert(!sw.update(false, 25051));
  assert(!sw.update(true, 25052));
  assert(sw.update(true, 25102) && sw.enabled());
  sw.begin(false, 0xfffffff0u);
  assert(!sw.enabled()); // reset does not persist ON
  assert(!sw.update(true, 0xfffffff1u));
  assert(sw.update(true, 0x23u) && sw.enabled()); // millis wrap
}
""")
    binary = tmp_path / "switch"
    subprocess.run(
        [
            compiler,
            "-std=c++11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(ROOT),
            str(source),
            "-o",
            str(binary),
        ],
        check=True,
    )
    subprocess.run([str(binary)], check=True)


def test_toggle_gates_adc_and_discards_pending_capture_without_resetting_hp():
    assert "pinMode(PIEZO_ENABLE_BUTTON_PIN, INPUT_PULLDOWN)" in MAIN
    assert "constexpr int PIEZO_ENABLE_BUTTON_PIN = 27;" in MAIN
    assert "PIEZO_DO_PIN != PIEZO_ENABLE_BUTTON_PIN" in MAIN
    poll = MAIN.split("static void pollAnalogPiezo(uint32_t now) {", 1)[1].split(
        "\n}", 1
    )[0]
    assert poll.index("!piezoEnableSwitch.enabled()") < poll.index("readPiezoSample()")
    toggle = MAIN.split("static void pollPiezoEnableButton(uint32_t now) {", 1)[
        1
    ].split("\n}", 1)[0]
    assert "resetAnalogPiezoState();" in toggle
    assert "resetLocalHitState" not in toggle
    assert "setPiezoDetectionEnabled(piezoEnableSwitch.enabled())" in toggle
    assert MAIN.index("pollPiezoEnableButton(now);") < MAIN.index(
        "pollAnalogPiezo(now);"
    )
    setup = MAIN.split("void setup() {", 1)[1]
    assert setup.index("beginPiezoEnableButton();") < setup.index("beginAnalogPiezo();")
    assert 'doc["piezo_adc_enabled"]' in MAIN
    # Runtime config / HP recovery must not re-arm the physical opt-in switch.
    assert MAIN.count("piezoEnableSwitch.begin(") == 1


def test_button_firmware_has_separate_release_series():
    workflow = (ROOT / ".github/workflows/firmware-ota.yml").read_text()
    assert 'VERSION="0.3.${GITHUB_RUN_NUMBER}-main"' in workflow
    assert 'VERSION="0.2.${GITHUB_RUN_NUMBER}-main"' in workflow
    assert 'if [[ "${{ matrix.id }}" == go2-nixo-framed-packet-* ]]; then\n            UPDATE_STABLE_LATEST="false"' in workflow
