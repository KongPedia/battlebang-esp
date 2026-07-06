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
DEFAULT_ENV_FILE = PROJECT_ROOT / "firmware" / "station" / ".env.station"
SERIAL_BOOT_SETTLE_S = 4.0
SERIAL_WRITE_CHUNK_BYTES = 96
SERIAL_WRITE_CHUNK_DELAY_S = 0.02
DEFAULT_LATEST_MANIFEST_URL = (
    "https://github.com/KongPedia/battlebang-esp/releases/download/"
    "station-latest/station-manifest.json"
)


class StationProvisionError(RuntimeError):
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
        if key.startswith("STATION_") or key.startswith("BATTLEBANG_STATION_"):
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
        raise StationProvisionError(f"{key} must be an integer, got {value!r}") from exc


def env_bool(env: dict[str, str], key: str, default: bool) -> bool:
    value = env.get(key)
    if value is None or value == "":
        return default
    normalized = value.strip().lower()
    if normalized in {"1", "true", "yes", "y", "on", "enable", "enabled"}:
        return True
    if normalized in {"0", "false", "no", "n", "off", "disable", "disabled"}:
        return False
    raise StationProvisionError(f"{key} must be boolean true/false, got {value!r}")


def set_if_present(doc: dict[str, Any], key: str, value: Any) -> None:
    if value is not None and value != "":
        doc[key] = value


def station_env_key(station_id: str) -> str | None:
    normalized = station_id.strip().lower()
    if normalized.startswith("station_") and normalized[-2:].isdigit():
        return f"STATION_{normalized[-2:]}_ID"
    return None


def station_display_name(station_id: str) -> str:
    suffix = station_id.rsplit("_", 1)[-1]
    return f"Station {suffix}" if suffix.isdigit() else station_id


def build_provision_config(env: dict[str, str], *, station_id: str) -> dict[str, Any]:
    config_version = env_int(env, "STATION_CONFIG_VERSION")
    if config_version is None:
        config_version = int(time.time())

    display_name = env_first(env, "STATION_DISPLAY_NAME", "STATION_NAME", default=station_display_name(station_id))
    device_id = env_first(env, "STATION_DEVICE_ID", default=station_id)

    doc: dict[str, Any] = {
        "type": "provision",
        "schema": 1,
        "config_version": config_version,
        "configured": True,
        "device_id": device_id,
        "station_id": station_id,
        "display_name": display_name,
        "debug_allow_simulate_hit": env_bool(env, "STATION_DEBUG_ALLOW_SIMULATE_HIT", False),
        "sensor": {
            "hit_threshold": env_int(env, "STATION_SENSOR_HIT_THRESHOLD", 3000),
            "release_threshold": env_int(env, "STATION_SENSOR_RELEASE_THRESHOLD", 1200),
            "hit_cooldown_ms": env_int(env, "STATION_SENSOR_HIT_COOLDOWN_MS", 300),
            "sample_interval_ms": env_int(env, "STATION_SENSOR_SAMPLE_INTERVAL_MS", 2),
            "settle_us": env_int(env, "STATION_SENSOR_SETTLE_US", 80),
        },
        "led": {
            "num_leds": env_int(env, "STATION_LED_NUM_LEDS", 60),
            "brightness": env_int(env, "STATION_LED_BRIGHTNESS", 120),
            "max_ma": env_int(env, "STATION_LED_MAX_MA", 3000),
            "waiting_color": env_first(env, "STATION_LED_WAITING_COLOR", default="#00FF00"),
            "captured_color": env_first(env, "STATION_LED_CAPTURED_COLOR", default="#FF0000"),
            "hit_flash_color": env_first(env, "STATION_LED_HIT_FLASH_COLOR", default="#FFFFFF"),
            "waiting_breath_bpm": env_int(env, "STATION_LED_WAITING_BREATH_BPM", 20),
            "waiting_breath_min": env_int(env, "STATION_LED_WAITING_BREATH_MIN", 5),
            "waiting_breath_max": env_int(env, "STATION_LED_WAITING_BREATH_MAX", 220),
        },
        "gameplay": {
            "lock_after_hit": env_bool(env, "STATION_GAMEPLAY_LOCK_AFTER_HIT", True),
            "auto_reset_ms": env_int(env, "STATION_GAMEPLAY_AUTO_RESET_MS", 0),
            "heartbeat_interval_ms": env_int(env, "STATION_GAMEPLAY_HEARTBEAT_INTERVAL_MS", 5000),
        },
        "hardware_profile": {
            "piezo_pin": env_int(env, "STATION_PIEZO_PIN", 32),
            "led_pin": env_int(env, "STATION_LED_PIN", 33),
            "led_type": env_first(env, "STATION_LED_TYPE", default="WS2812B"),
            "color_order": env_first(env, "STATION_COLOR_ORDER", default="RGB"),
        },
        "wifi": {
            "ssid": env_first(env, "STATION_WIFI_SSID", default=""),
            "password": env_first(env, "STATION_WIFI_PASSWORD", default=""),
            "auto_start": env_bool(env, "STATION_NETWORK_AUTO_START", True),
            "start_delay_ms": env_int(env, "STATION_NETWORK_START_DELAY_MS", 0),
        },
        "mqtt": {
            "host": env_first(env, "STATION_MQTT_HOST", default=""),
            "port": env_int(env, "STATION_MQTT_PORT", 1883),
            "username": env_first(env, "STATION_MQTT_USERNAME", default=""),
            "password": env_first(env, "STATION_MQTT_PASSWORD", default=""),
            "root": env_first(env, "STATION_MQTT_ROOT", default="battlebang"),
        },
        "ota": {
            "command_center_controlled": env_bool(env, "STATION_OTA_COMMAND_CENTER_CONTROLLED", True),
            "auto_check_enabled": env_bool(env, "STATION_OTA_AUTO_CHECK", False),
            "channel": env_first(env, "STATION_OTA_CHANNEL", default="station"),
            "desired_build": env_int(env, "STATION_OTA_DESIRED_BUILD", 0),
            "public_manifest_url": env_first(
                env,
                "STATION_OTA_PUBLIC_MANIFEST_URL",
                default=DEFAULT_LATEST_MANIFEST_URL,
            ),
            "local_mirror_url": env_first(env, "STATION_OTA_LOCAL_MIRROR_URL", default=""),
            "check_interval_s": env_int(env, "STATION_OTA_CHECK_INTERVAL_S", 300),
            "apply_only_in_safe_state": env_bool(env, "STATION_OTA_APPLY_ONLY_IN_SAFE_STATE", True),
        },
    }

    set_if_present(doc, "group", env_first(env, "STATION_GROUP"))
    set_if_present(doc, "stage_id", env_first(env, "STATION_STAGE_ID"))
    set_if_present(doc, "location", env_first(env, "STATION_LOCATION"))
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
        raise StationProvisionError("pyserial is required; use ./.venv-pio/bin/python or install pyserial") from exc

    with serial.Serial(port, baudrate=baud, timeout=wait_s) as ser:  # type: ignore[attr-defined]
        time.sleep(SERIAL_BOOT_SETTLE_S)
        ser.reset_input_buffer()
        encoded = command.encode("utf-8") + b"\n"
        for start in range(0, len(encoded), SERIAL_WRITE_CHUNK_BYTES):
            ser.write(encoded[start : start + SERIAL_WRITE_CHUNK_BYTES])
            ser.flush()
            time.sleep(SERIAL_WRITE_CHUNK_DELAY_S)
        deadline = time.time() + wait_s
        while time.time() < deadline:
            line = ser.readline()
            if not line:
                continue
            decoded = line.decode("utf-8", errors="replace").rstrip()
            print(decoded)
            if "config_applied" in decoded or "config_applied_save_failed" in decoded:
                return
        raise StationProvisionError("timed out waiting for station provision acknowledgement")


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Build and optionally send Station USB serial provisioning JSON.")
    parser.add_argument("--env-file", type=Path, default=DEFAULT_ENV_FILE, help="default: firmware/station/.env.station")
    parser.add_argument("--station-id", help="Station id to write into NVS, e.g. station_01")
    parser.add_argument("--serial-port", help="USB serial port; if omitted, print JSON only")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--wait-s", type=float, default=8.0)
    parser.add_argument("--print-json", action="store_true", help="print redacted JSON before sending")
    parser.add_argument("--print-json-secrets", action="store_true", help="print unredacted JSON and exit/send")
    parser.add_argument("--no-serial", action="store_true", help="do not write to serial even if --serial-port is set")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = make_parser()
    args = parser.parse_args(argv)
    env = merged_env(args.env_file)
    station_id = args.station_id or env_first(env, "STATION_STATION_ID", "STATION_01_ID", default="station_01")
    station_key = station_env_key(station_id)
    if args.station_id is None and station_key and env_first(env, station_key):
        station_id = env_first(env, station_key, default=station_id)
    if station_id is None or not station_id.strip():
        raise StationProvisionError("station_id is required")
    station_id = station_id.strip()

    payload = build_provision_config(env, station_id=station_id)
    payload_json = json.dumps(payload, separators=(",", ":"), ensure_ascii=False)
    if len(payload_json.encode("utf-8")) > 3900:
        raise StationProvisionError(f"provision JSON is {len(payload_json)} bytes; firmware serial line limit is 4096")

    if args.print_json or args.print_json_secrets or not args.serial_port or args.no_serial:
        printable = payload if args.print_json_secrets else redacted(payload)
        print(json.dumps(printable, indent=2, ensure_ascii=False))

    if args.serial_port and not args.no_serial:
        write_serial(args.serial_port, args.baud, "provision " + payload_json, args.wait_s)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except StationProvisionError as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(2)
