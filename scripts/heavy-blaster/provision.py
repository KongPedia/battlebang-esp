#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import sys
import time
from pathlib import Path
from typing import Any

PROJECT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_ENV_FILE = PROJECT_ROOT / "src" / "heavy-blaster" / ".env.heavy-blaster"
DEFAULT_LATEST_MANIFEST_URL = (
    "https://github.com/KongPedia/battlebang-esp/releases/download/"
    "heavy-blaster-latest/heavy-blaster-manifest.json"
)


class HeavyBlasterProvisionError(RuntimeError):
    pass


def parse_dotenv(path: Path) -> dict[str, str]:
    if not path.exists():
        return {}
    values: dict[str, str] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("export "):
            line = line[len("export ") :].strip()
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip()
        if not key:
            continue
        if len(value) >= 2 and value[0] == value[-1] and value[0] in {"'", '"'}:
            value = value[1:-1]
        values[key] = value
    return values


def merged_env(env_file: Path) -> dict[str, str]:
    values = parse_dotenv(env_file)
    for key, value in os.environ.items():
        if key.startswith("HEAVY_BLASTER_") or key.startswith("BATTLEBANG_HEAVY_BLASTER_"):
            values[key] = value
    return values


def env_first(env: dict[str, str], *keys: str, default: str | None = None) -> str | None:
    for key in keys:
        value = env.get(key)
        if value is not None and value != "":
            return value
    return default


def env_int(env: dict[str, str], key: str, default: int | None = None) -> int | None:
    value = env.get(key)
    if value is None or value == "":
        return default
    try:
        return int(value, 0)
    except ValueError as exc:
        raise HeavyBlasterProvisionError(f"{key} must be an integer, got {value!r}") from exc


def env_bool(env: dict[str, str], key: str, default: bool) -> bool:
    value = env.get(key)
    if value is None or value == "":
        return default
    normalized = value.strip().lower()
    if normalized in {"1", "true", "yes", "y", "on", "enable", "enabled"}:
        return True
    if normalized in {"0", "false", "no", "n", "off", "disable", "disabled"}:
        return False
    raise HeavyBlasterProvisionError(f"{key} must be boolean true/false, got {value!r}")


def set_if_present(doc: dict[str, Any], key: str, value: Any) -> None:
    if value is not None and value != "":
        doc[key] = value


def split_csv_ints(value: str | None, expected: int, label: str, defaults: list[int]) -> list[int]:
    if not value:
        return defaults
    parsed: list[int] = []
    for part in value.split(","):
        part = part.strip()
        if not part:
            continue
        try:
            parsed.append(int(part, 0))
        except ValueError as exc:
            raise HeavyBlasterProvisionError(f"{label} contains non-integer value {part!r}") from exc
    if len(parsed) != expected:
        raise HeavyBlasterProvisionError(f"{label} must contain {expected} integers")
    return parsed


def build_provision_config(env: dict[str, str]) -> dict[str, Any]:
    config_version = env_int(env, "HEAVY_BLASTER_CONFIG_VERSION")
    if config_version is None:
        config_version = int(time.time())

    slot_count = env_int(env, "HEAVY_BLASTER_SLOT_COUNT", 4)
    if slot_count is None or slot_count < 1:
        raise HeavyBlasterProvisionError("HEAVY_BLASTER_SLOT_COUNT must be positive")

    doc: dict[str, Any] = {
        "type": "provision",
        "schema": 1,
        "config_version": config_version,
        "configured": True,
        "debug_allow_local_control": env_bool(env, "HEAVY_BLASTER_DEBUG_ALLOW_LOCAL_CONTROL", False),
        "unlock": {
            "required_slots": env_int(env, "HEAVY_BLASTER_REQUIRED_SLOTS", 4),
            "pre_effect_ms": env_int(env, "HEAVY_BLASTER_PRE_EFFECT_MS", 10000),
            "fade_out_ms": env_int(env, "HEAVY_BLASTER_FADE_OUT_MS", 4000),
            "blink_bpm": env_int(env, "HEAVY_BLASTER_BLINK_BPM", 40),
            "relay_on_after_all_slots": env_bool(env, "HEAVY_BLASTER_RELAY_ON_AFTER_ALL_SLOTS", True),
        },
        "led": {
            "slot_count": slot_count,
            "matrix_width": env_int(env, "HEAVY_BLASTER_MATRIX_WIDTH", 8),
            "matrix_height": env_int(env, "HEAVY_BLASTER_MATRIX_HEIGHT", 8),
            "matrix_num_leds": env_int(env, "HEAVY_BLASTER_MATRIX_NUM_LEDS", 64),
            "brightness": env_int(env, "HEAVY_BLASTER_LED_BRIGHTNESS", 30),
            "max_ma": env_int(env, "HEAVY_BLASTER_LED_MAX_MA", 2000),
            "active_color": env_first(env, "HEAVY_BLASTER_ACTIVE_COLOR", default="#FFFF00"),
        },
        "hardware_profile": {
            "slot_count": slot_count,
            "matrix_pins": split_csv_ints(
                env_first(env, "HEAVY_BLASTER_MATRIX_PINS"),
                4,
                "HEAVY_BLASTER_MATRIX_PINS",
                [23, 22, 21, 19],
            ),
            "relay_pin": env_int(env, "HEAVY_BLASTER_RELAY_PIN", 26),
            "relay_active_low": env_bool(env, "HEAVY_BLASTER_RELAY_ACTIVE_LOW", False),
            "relay_profile": env_first(env, "HEAVY_BLASTER_RELAY_PROFILE", default="single_channel_active_high"),
            "led_type": env_first(env, "HEAVY_BLASTER_LED_TYPE", default="WS2812B"),
            "color_order": env_first(env, "HEAVY_BLASTER_COLOR_ORDER", default="GRB"),
        },
        "wifi": {
            "ssid": env_first(env, "HEAVY_BLASTER_WIFI_SSID", default=""),
            "password": env_first(env, "HEAVY_BLASTER_WIFI_PASSWORD", default=""),
            "auto_start": env_bool(env, "HEAVY_BLASTER_NETWORK_AUTO_START", True),
            "start_delay_ms": env_int(env, "HEAVY_BLASTER_NETWORK_START_DELAY_MS", 0),
        },
        "mqtt": {
            "host": env_first(env, "HEAVY_BLASTER_MQTT_HOST", default=""),
            "port": env_int(env, "HEAVY_BLASTER_MQTT_PORT", 1883),
            "username": env_first(env, "HEAVY_BLASTER_MQTT_USERNAME", default=""),
            "password": env_first(env, "HEAVY_BLASTER_MQTT_PASSWORD", default=""),
            "root": env_first(env, "HEAVY_BLASTER_MQTT_ROOT", default="battlebang"),
        },
        "ota": {
            "command_center_controlled": env_bool(env, "HEAVY_BLASTER_OTA_COMMAND_CENTER_CONTROLLED", True),
            "auto_check_enabled": env_bool(env, "HEAVY_BLASTER_OTA_AUTO_CHECK", False),
            "channel": env_first(env, "HEAVY_BLASTER_OTA_CHANNEL", default="heavy-blaster"),
            "desired_build": env_int(env, "HEAVY_BLASTER_OTA_DESIRED_BUILD", 0),
            "public_manifest_url": env_first(
                env,
                "HEAVY_BLASTER_OTA_PUBLIC_MANIFEST_URL",
                default=DEFAULT_LATEST_MANIFEST_URL,
            ),
            "local_mirror_url": env_first(env, "HEAVY_BLASTER_OTA_LOCAL_MIRROR_URL", default=""),
            "check_interval_s": env_int(env, "HEAVY_BLASTER_OTA_CHECK_INTERVAL_S", 300),
            "apply_only_in_safe_state": env_bool(env, "HEAVY_BLASTER_OTA_APPLY_ONLY_IN_SAFE_STATE", True),
        },
    }

    set_if_present(doc, "blaster_id", env_first(env, "HEAVY_BLASTER_BLASTER_ID"))
    set_if_present(doc, "display_name", env_first(env, "HEAVY_BLASTER_DISPLAY_NAME", "HEAVY_BLASTER_NAME"))
    set_if_present(doc, "group", env_first(env, "HEAVY_BLASTER_GROUP"))
    set_if_present(doc, "location", env_first(env, "HEAVY_BLASTER_LOCATION"))
    return doc


def redacted(doc: dict[str, Any]) -> dict[str, Any]:
    clone = json.loads(json.dumps(doc, ensure_ascii=False))
    for section in ("wifi", "mqtt"):
        value = clone.get(section)
        if isinstance(value, dict) and value.get("password"):
            value["password"] = "***"
    return clone


def write_serial(port: str, baud: int, command: str, wait_s: float) -> None:
    try:
        import serial  # type: ignore[import-not-found]
    except ImportError as exc:
        raise HeavyBlasterProvisionError("pyserial is required; use ./.venv-pio/bin/python or install pyserial") from exc

    with serial.Serial(port, baudrate=baud, timeout=wait_s) as ser:  # type: ignore[attr-defined]
        time.sleep(0.2)
        ser.reset_input_buffer()
        ser.write(command.encode("utf-8") + b"\n")
        ser.flush()
        deadline = time.time() + wait_s
        while time.time() < deadline:
            line = ser.readline()
            if line:
                sys.stdout.write(line.decode("utf-8", errors="replace"))


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Provision a BattleBang heavy-blaster from src/heavy-blaster/.env.heavy-blaster.")
    parser.add_argument("--env-file", type=Path, default=DEFAULT_ENV_FILE, help="default: src/heavy-blaster/.env.heavy-blaster")
    parser.add_argument("--serial-port", help="ESP32 serial port; default/env HEAVY_BLASTER_SERIAL_PORT")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--wait-s", type=float, default=2.0, help="seconds to read serial response after writing")
    parser.add_argument("--print-json", action="store_true", help="print generated provision JSON with secrets redacted")
    parser.add_argument("--print-json-secrets", action="store_true", help="print generated provision JSON including secrets")
    parser.add_argument("--no-serial", action="store_true", help="do not send the provision command to a board")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = make_parser().parse_args(argv)
    env = merged_env(args.env_file)
    doc = build_provision_config(env)
    if args.print_json:
        print(json.dumps(redacted(doc), ensure_ascii=False, separators=(",", ":")))
    if args.print_json_secrets:
        print(json.dumps(doc, ensure_ascii=False, separators=(",", ":")))

    port = args.serial_port or env_first(env, "HEAVY_BLASTER_SERIAL_PORT")
    if args.no_serial or not port:
        return 0

    payload = json.dumps(doc, ensure_ascii=False, separators=(",", ":"))
    if len(payload) > 4000:
        raise HeavyBlasterProvisionError(f"provision JSON is {len(payload)} bytes; firmware serial line limit is 4096")
    write_serial(port, args.baud, f"provision {payload}", args.wait_s)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except HeavyBlasterProvisionError as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(2)
