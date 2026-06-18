from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NIXO_DIR = ROOT / "src/nIxo"


def load_variant(name: str) -> dict:
    return json.loads((NIXO_DIR / "variants" / name / "config.json").read_text())


def test_nixo_relay_variants_are_split_by_folder() -> None:
    relay_1ch = load_variant("relay_1ch")
    relay_2ch = load_variant("relay_2ch")

    assert relay_1ch["relay_channels"] == 1
    assert relay_1ch["relay1_pin"] == 23
    assert relay_1ch["relay2_pin"] == -1
    assert relay_1ch["relay1_role"] == "single_fire_gpio23"
    assert relay_1ch["relay2_role"] == "disabled"

    assert relay_2ch["relay_channels"] == 2
    assert relay_2ch["relay1_pin"] == 22
    assert relay_2ch["relay2_pin"] == 23
    assert {relay_2ch["relay1_pin"], relay_2ch["relay2_pin"]} == {22, 23}
    assert relay_2ch["relay1_role"] == "flywheel_gpio22"
    assert relay_2ch["relay2_role"] == "chain_gpio23"
    assert relay_2ch["relay_delay1_ms"] == 150
    assert relay_2ch["relay_on_level"] == "LOW"
    assert relay_2ch["relay_off_level"] == "HIGH"


def test_platformio_exposes_1ch_and_2ch_nixo_envs() -> None:
    platformio = (ROOT / "platformio.ini").read_text()

    assert "custom_nixo_variant = relay_1ch" in platformio
    assert "custom_nixo_variant = relay_2ch" in platformio
    for robot_id in ("go2_03", "go2_05", "go2_06", "go2_07"):
        assert f"[env:esp32dev_nixo_1ch_{robot_id}]" in platformio
        assert f"[env:esp32dev_nixo_2ch_{robot_id}]" in platformio


def test_nixo_config_loads_variant_configs_and_env_overrides() -> None:
    source = (ROOT / "scripts/nixo_config.py").read_text()

    assert "def load_relay_variant" in source
    assert '"src" / "nIxo" / "variants" / name / "config.json"' in source
    assert '"NIXO_RELAY_VARIANT"' in source
    assert '"custom_nixo_variant"' in source
    assert '"NIXO_BUILD_RELAY_VARIANT_NAME"' in source
    assert '"NIXO_BUILD_RELAY_CHANNELS"' in source
    assert '"NIXO_BUILD_RELAY1_PIN"' in source
    assert '"NIXO_BUILD_RELAY2_PIN"' in source
    assert '"NIXO_BUILD_RELAY1_ROLE"' in source
    assert '"NIXO_BUILD_RELAY2_ROLE"' in source
    assert "validate_relay_variant" in source


def test_nixo_build_config_accepts_variant_relay_overrides() -> None:
    source = (NIXO_DIR / "build_config.h").read_text()

    assert "#ifdef NIXO_BUILD_RELAY_VARIANT_NAME" in source
    assert "#define NIXO_RELAY_VARIANT_NAME NIXO_BUILD_RELAY_VARIANT_NAME" in source
    assert "#define NIXO_RELAY_CHANNELS NIXO_BUILD_RELAY_CHANNELS" in source
    assert "#define NIXO_RELAY1_PIN NIXO_BUILD_RELAY1_PIN" in source
    assert "#define NIXO_RELAY2_PIN NIXO_BUILD_RELAY2_PIN" in source
    assert "#define NIXO_RELAY1_ROLE NIXO_BUILD_RELAY1_ROLE" in source
    assert "#define NIXO_RELAY2_ROLE NIXO_BUILD_RELAY2_ROLE" in source
    assert '#define NIXO_RELAY_VARIANT_NAME "relay_1ch"' in source
    assert "#define NIXO_RELAY_CHANNELS 1" in source
    assert "#define NIXO_RELAY1_PIN 23" in source
    assert "#define NIXO_RELAY2_PIN -1" in source
    assert '#define NIXO_RELAY1_ROLE "single_fire_gpio23"' in source
    assert '#define NIXO_RELAY2_ROLE "disabled"' in source


def test_nixo_firmware_logs_variant_roles_and_guards_pin_contract() -> None:
    source = (NIXO_DIR / "main.cpp").read_text()

    assert "constexpr int RELAY_CHANNELS = NIXO_RELAY_CHANNELS;" in source
    assert "constexpr const char* RELAY_VARIANT = NIXO_RELAY_VARIANT_NAME;" in source
    assert "constexpr const char* RELAY1_ROLE = NIXO_RELAY1_ROLE;" in source
    assert "constexpr const char* RELAY2_ROLE = NIXO_RELAY2_ROLE;" in source
    assert "NIXO_RELAY_CHANNELS must be 1 or 2" in source
    assert "NIXO_RELAY_CHANNELS must match NIXO_RELAY2_PIN" in source
    assert "Nixo relay pins must be different" in source
    assert "relay_variant=%s relay_channels=%d" in source
    assert "variant=%s channels=%d RELAY1=%d role=%s RELAY2=%d role=%s" in source
    assert "CH1 ON pin=%d role=%s" in source
    assert "CH2 ON pin=%d role=%s" in source
    assert "digitalWrite(RELAY1_PIN, RELAY_OFF);" in source.split("pinMode(RELAY1_PIN, OUTPUT);", 1)[0]
    update_block = source.split("static void updateFireSequence", 1)[1]
    two_channel_done = update_block.split("case FIRE_RELAY_WAIT2:", 1)[1].split("return;", 1)[0]
    assert two_channel_done.index("digitalWrite(RELAY2_PIN, RELAY_OFF);") < two_channel_done.index(
        "digitalWrite(RELAY1_PIN, RELAY_OFF);"
    )
    assert two_channel_done.index("CH2 OFF pin=%d role=%s level=%d readback=%d") < two_channel_done.index("CH1 OFF pin=%d role=%s level=%d readback=%d")
