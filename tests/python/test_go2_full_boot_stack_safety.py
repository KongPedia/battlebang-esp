"""Guard the Go2 Full USB boot regression without connecting to hardware."""

from __future__ import annotations

import configparser
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]


def test_go2_full_pins_the_verified_idf4_platform_and_bounds_stack_frames() -> None:
    config = configparser.ConfigParser()
    config.read(ROOT / "platformio.ini")
    go2_full = config["env:esp32dev_go2_full"]
    assert go2_full["platform"] == "espressif32@7.1.0"
    assert "-Werror=frame-larger-than=4096" in go2_full["build_flags"]


@pytest.mark.parametrize("usb_transport", [True, False], ids=["usb", "uart"])
def test_boot_only_formats_text_status_on_the_text_usb_transport(tmp_path, usb_transport: bool) -> None:
    compiler = shutil.which("g++") or shutil.which("clang++")
    assert compiler, "A C++ preprocessor is required for the firmware contract tests"
    source = (ROOT / "firmware/go2_full/main.cpp").read_text()
    setup = "void setup() {" + source.split("void setup() {", 1)[1].split("void loop() {", 1)[0]
    unit = tmp_path / "setup.cpp"
    unit.write_text(setup)
    result = subprocess.run(
        [compiler, "-E", "-P", f"-DBB_GO2_FULL_JETSON_USB_SERIAL={int(usb_transport)}", str(unit)],
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    assert ('printStatusJson("usb", "boot");' in result) is not usb_transport
    assert "queueJetsonDeviceStatusPacket();" in result
    assert "queueJetsonHpSnapshotPacket();" in result
