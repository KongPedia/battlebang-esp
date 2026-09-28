"""Keep the temporary HP-bar wiring diagnostic isolated from HP ownership."""

import json
import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = (ROOT / "firmware/go2_nixo/display/bar_display.h").read_text()
SOURCE = (ROOT / "firmware/go2_nixo/display/bar_display.cpp").read_text()
FRAMED = (ROOT / "firmware/go2_nixo_framed_packet_uart/main.cpp").read_text()
RUNTIME = (ROOT / "firmware/go2_nixo/config/runtime_config.cpp").read_text()
PROVISION = (ROOT / "scripts/go2_nixo/provision.py").read_text()


def test_hp_bar_diagnostic_selects_one_group_or_raw_pixel_with_timeout() -> None:
    assert "bool setDiagnosticGroup(int group1Based, uint32_t now);" in HEADER
    assert "bool setDiagnosticPixel(int pixel1Based, uint32_t now);" in HEADER
    assert "void clearDiagnostic();" in HEADER
    assert "constexpr uint32_t DIAGNOSTIC_TTL_MS = 10000;" in SOURCE
    assert "setHpBarGroup(diagnosticGroup_, CRGB::White)" in SOURCE
    assert "leds_[diagnosticPixel_ - 1] = CRGB::White" in SOURCE
    assert "diagnosticExpiresMs_ = now + DIAGNOSTIC_TTL_MS" in SOURCE
    assert "clearDiagnostic();" in SOURCE


def test_framed_serial_led_diagnostic_never_mutates_hp() -> None:
    command = FRAMED.split("static void handleCommandLine", 1)[1].split("static bool isImmediateCommandChar", 1)[0]
    assert 'lower.startsWith("led-test ")' in command
    assert "barDisplay.setDiagnosticGroup(" in command
    assert "barDisplay.setDiagnosticPixel(" in command
    assert "barDisplay.clearDiagnostic()" in command
    assert "applyLocalHit(" not in command


def test_hp_down_preempts_bench_diagnostic() -> None:
    assert 'if (localDown_ || (remoteActive_ && (remoteDown_ || remoteMode_ == "down"))) return false;' in SOURCE
    assert 'if (localDown_ && (diagnosticGroup_ > 0 || diagnosticPixel_ > 0)) clearDiagnostic();' in SOURCE
    assert 'if ((remoteDown_ || mode == "down") && (diagnosticGroup_ > 0 || diagnosticPixel_ > 0)) clearDiagnostic();' in SOURCE
    assert SOURCE.index('A bench wiring check must never hide') < SOURCE.index('if (diagnosticGroup_ > 0 || diagnosticPixel_ > 0) {')
    assert 'if (localDown_) {\n      renderLocal(now);\n    } else if (remoteActive_)' in SOURCE


def test_per_robot_27_column_serpentine_layout_and_nvs() -> None:
    def indices(group: int, count: int) -> tuple[int, int, int]:
        return group, 2 * count + 1 - group, 2 * count + group

    assert indices(1, 27) == (1, 54, 55)
    assert indices(27, 27) == (27, 28, 81)
    assert {pixel for group in range(1, 28) for pixel in indices(group, 27)} == set(range(1, 82))
    assert "2 * groupCount_ - group1Based" in SOURCE
    assert "2 * groupCount_ - 1 + group1Based" in SOURCE
    assert 'getUChar("bar_groups", hit.hpBarGroupCount)' in RUNTIME
    assert 'putUChar("bar_groups", hit.hpBarGroupCount)' in RUNTIME
    assert '"hp_bar_group_count": env_int(env, prefixed_tuning_keys("HP_BAR_GROUP_COUNT"), 28)' in PROVISION
    assert "barDisplay.setGroupCount(runtimeConfig.hit.hpBarGroupCount);" in FRAMED
    groups_by_hp = [int(hp * 27 / 14 + 0.5) for hp in range(14, -1, -1)]
    assert groups_by_hp[:2] == [27, 25]
    assert [before - after for before, after in zip(groups_by_hp, groups_by_hp[1:])].count(1) == 1


def test_provision_env_can_select_go2_06_bar_without_changing_default() -> None:
    script = ROOT / "scripts/go2_nixo/provision.py"
    example = ROOT / "firmware/go2_nixo_framed_packet_uart/.env.go2_nixo_framed_packet_uart.example"
    env = os.environ.copy()
    env.update(GO2_NIXO_HP_BAR_GROUP_COUNT="27", GO2_NIXO_PIEZO_CHANNEL_ENABLE_MASK="2")
    result = subprocess.run(
        [sys.executable, str(script), "--env-file", str(example), "--robot-id", "go2_06", "--no-serial", "--print-json"],
        cwd=ROOT,
        env=env,
        check=True,
        capture_output=True,
        text=True,
    )
    payload = json.loads(result.stdout.splitlines()[-1])
    assert payload["hit"]["hp_bar_group_count"] == 27
    assert payload["hit"]["piezo_channel_enable_mask"] == 2


def test_usb_framed_provisioning_refuses_ascii_on_jetson_data_port() -> None:
    script = ROOT / "scripts/go2_nixo/provision.py"
    example = ROOT / "firmware/go2_nixo_framed_packet_uart/.env.go2_nixo_framed_packet_uart.example"
    env = os.environ.copy()
    env["GO2_NIXO_JETSON_TRANSPORT"] = "usb_serial"
    result = subprocess.run(
        [sys.executable, str(script), "--env-file", str(example), "--command", "show-config", "--serial-port", "/dev/ttyUSB0"],
        cwd=ROOT,
        env=env,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 2
    assert "USB-framed Jetson port accepts binary packets" in result.stderr
