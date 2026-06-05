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
DEFAULT_ENV_FILE = PROJECT_ROOT / "src" / "hit_target" / ".env.hit_target"
DEFAULT_LATEST_MANIFEST_URL = "https://github.com/KongPedia/battlebang-esp/releases/download/hit-target-latest/hit-target-manifest.json"


class HitTargetProvisionError(RuntimeError):
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
        if key.startswith("HIT_TARGET_") or key.startswith("BATTLEBANG_HIT_TARGET_"):
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
        raise HitTargetProvisionError(f"{key} must be an integer, got {value!r}") from exc


def env_bool(env: dict[str, str], key: str, default: bool) -> bool:
    value = env.get(key)
    if value is None or value == "":
        return default
    normalized = value.strip().lower()
    if normalized in {"1", "true", "yes", "y", "on", "enable", "enabled"}:
        return True
    if normalized in {"0", "false", "no", "n", "off", "disable", "disabled"}:
        return False
    raise HitTargetProvisionError(f"{key} must be boolean true/false, got {value!r}")


def set_if_present(doc: dict[str, Any], key: str, value: Any) -> None:
    if value is not None and value != "":
        doc[key] = value


def split_palette(value: str | None) -> list[str]:
    if not value:
        return ["#009600", "#BE8200", "#BE0000"]
    colors = [part.strip() for part in value.split(",") if part.strip()]
    if not colors:
        raise HitTargetProvisionError("HIT_TARGET_HP_PALETTE must contain at least one color")
    return colors


def build_provision_config(env: dict[str, str]) -> dict[str, Any]:
    config_version = env_int(env, "HIT_TARGET_CONFIG_VERSION")
    if config_version is None:
        config_version = int(time.time())

    hp_phase_count = env_int(env, "HIT_TARGET_HP_PHASE_COUNT", 3)
    hits_per_phase = env_int(env, "HIT_TARGET_HITS_PER_PHASE", 5)
    if hp_phase_count is None or hits_per_phase is None:
        raise HitTargetProvisionError("HP phase count and hits per phase are required")

    doc: dict[str, Any] = {
        "type": "provision",
        "schema": 1,
        "config_version": config_version,
        "configured": True,
        "debug_allow_simulate_hit": env_bool(env, "HIT_TARGET_DEBUG_ALLOW_SIMULATE_HIT", False),
        "hp": {
            "phase_count": hp_phase_count,
            "hits_per_phase": hits_per_phase,
            "palette": split_palette(env_first(env, "HIT_TARGET_HP_PALETTE")),
        },
        "visual": {
            "orbit_step_ms": env_int(env, "HIT_TARGET_ORBIT_STEP_MS", 20),
            "orbit_tail_leds": env_int(env, "HIT_TARGET_ORBIT_TAIL_LEDS", 6),
            "cooldown_blink_ms": env_int(env, "HIT_TARGET_COOLDOWN_BLINK_MS", 60),
            "hit_flash_ms": env_int(env, "HIT_TARGET_HIT_FLASH_MS", 50),
            "damage_chip_ms": env_int(env, "HIT_TARGET_DAMAGE_CHIP_MS", 580),
            "phase_backfill_gap_leds": env_int(env, "HIT_TARGET_PHASE_BACKFILL_GAP_LEDS", 1),
            "phase_backfill_scale": env_int(env, "HIT_TARGET_PHASE_BACKFILL_SCALE", 96),
            "hp_hit_pulse_ms": env_int(env, "HIT_TARGET_HP_HIT_PULSE_MS", 180),
            "defeat_blackout_ms": env_int(env, "HIT_TARGET_DEFEAT_BLACKOUT_MS", 90),
            "defeat_rainbow_ms": env_int(env, "HIT_TARGET_DEFEAT_RAINBOW_MS", 900),
            "defeat_rainbow_spins": env_int(env, "HIT_TARGET_DEFEAT_RAINBOW_SPINS", 2),
        },
        "sensor": {
            "piezo_do_pin": env_int(env, "HIT_TARGET_PIEZO_DO_PIN", 27),
            "piezo_ao_pin": env_int(env, "HIT_TARGET_PIEZO_AO_PIN", -1),
            "hit_threshold": env_int(env, "HIT_TARGET_HIT_THRESHOLD", 1400),
            "hit_rearm_threshold": env_int(env, "HIT_TARGET_HIT_REARM_THRESHOLD", 800),
            "hit_cooldown_ms": env_int(env, "HIT_TARGET_HIT_COOLDOWN_MS", 200),
            "hit_rearm_stable_ms": env_int(env, "HIT_TARGET_HIT_REARM_STABLE_MS", 80),
            "hit_rearm_check_ms": env_int(env, "HIT_TARGET_HIT_REARM_CHECK_MS", 15),
            "digital_hit_min_edges": env_int(env, "HIT_TARGET_DIGITAL_HIT_MIN_EDGES", 2),
            "digital_isr_debounce_us": env_int(env, "HIT_TARGET_DIGITAL_ISR_DEBOUNCE_US", 5000),
            "capture_window_ms": env_int(env, "HIT_TARGET_CAPTURE_WINDOW_MS", 80),
        },
        "led": {
            "pin": env_int(env, "HIT_TARGET_LED_PIN", 18),
            "num_leds": env_int(env, "HIT_TARGET_NUM_LEDS", 60),
            "type": env_first(env, "HIT_TARGET_LED_TYPE", default="WS2812B"),
            "color_order": env_first(env, "HIT_TARGET_COLOR_ORDER", default="GRB"),
            "brightness": env_int(env, "HIT_TARGET_LED_BRIGHTNESS", 80),
            "max_ma": env_int(env, "HIT_TARGET_LED_MAX_MA", 1500),
        },
        "reset": {
            "button_pin": env_int(env, "HIT_TARGET_RESET_BUTTON_PIN", 0),
            "button_hold_ms": env_int(env, "HIT_TARGET_RESET_BUTTON_HOLD_MS", 1200),
        },
        "wifi": {
            "ssid": env_first(env, "HIT_TARGET_WIFI_SSID", default=""),
            "password": env_first(env, "HIT_TARGET_WIFI_PASSWORD", default=""),
        },
        "network": {
            "auto_start": env_bool(env, "HIT_TARGET_NETWORK_AUTO_START", True),
            "start_delay_ms": env_int(env, "HIT_TARGET_NETWORK_START_DELAY_MS", 0),
        },
        "mqtt": {
            "host": env_first(env, "HIT_TARGET_MQTT_HOST", default=""),
            "port": env_int(env, "HIT_TARGET_MQTT_PORT", 1883),
            "username": env_first(env, "HIT_TARGET_MQTT_USERNAME", default=""),
            "password": env_first(env, "HIT_TARGET_MQTT_PASSWORD", default=""),
            "root": env_first(env, "HIT_TARGET_MQTT_ROOT", default="battlebang"),
        },
        "ota": {
            "command_center_controlled": env_bool(env, "HIT_TARGET_OTA_COMMAND_CENTER_CONTROLLED", True),
            "auto_check_enabled": env_bool(env, "HIT_TARGET_OTA_AUTO_CHECK", False),
            "channel": env_first(env, "HIT_TARGET_OTA_CHANNEL", default="hit-target"),
            "desired_build": env_int(env, "HIT_TARGET_OTA_DESIRED_BUILD", 0),
            "public_manifest_url": env_first(env, "HIT_TARGET_OTA_PUBLIC_MANIFEST_URL", default=DEFAULT_LATEST_MANIFEST_URL),
            "local_mirror_url": env_first(env, "HIT_TARGET_OTA_LOCAL_MIRROR_URL", default=""),
            "check_interval_s": env_int(env, "HIT_TARGET_OTA_CHECK_INTERVAL_S", 300),
            "apply_only_in_safe_state": env_bool(env, "HIT_TARGET_OTA_APPLY_ONLY_IN_SAFE_STATE", True),
        },
    }

    set_if_present(doc, "target_id", env_first(env, "HIT_TARGET_TARGET_ID"))
    set_if_present(doc, "group", env_first(env, "HIT_TARGET_GROUP"))
    set_if_present(doc, "location", env_first(env, "HIT_TARGET_LOCATION"))
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
        raise HitTargetProvisionError("pyserial is required; use ./.venv-pio/bin/python or install pyserial") from exc

    with serial.Serial(port, baudrate=baud, timeout=wait_s) as ser:  # type: ignore[attr-defined]
        time.sleep(0.2)
        ser.reset_input_buffer()
        ser.write(command.encode("utf-8") + b"\n")
        ser.flush()
        deadline = time.time() + wait_s
        while time.time() < deadline:
            line = ser.readline()
            if not line:
                continue
            sys.stdout.write(line.decode("utf-8", errors="replace"))


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Provision a BattleBang hit_target with Wi-Fi/MQTT/OTA runtime config from src/hit_target/.env.hit_target."
    )
    parser.add_argument("--env-file", type=Path, default=DEFAULT_ENV_FILE, help="default: src/hit_target/.env.hit_target")
    parser.add_argument("--serial-port", help="ESP32 serial port; default/env HIT_TARGET_SERIAL_PORT")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--wait-s", type=float, default=2.0, help="seconds to read serial response after writing")
    parser.add_argument("--print-json", action="store_true", help="print generated provision JSON with secrets redacted")
    parser.add_argument("--print-json-secrets", action="store_true", help="print generated provision JSON including secrets")
    parser.add_argument("--no-serial", action="store_true", help="do not send the provision command to a board")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = make_parser()
    args = parser.parse_args(argv)
    env = merged_env(args.env_file)
    doc = build_provision_config(env)
    if args.print_json:
        print(json.dumps(redacted(doc), ensure_ascii=False, separators=(",", ":")))
    if args.print_json_secrets:
        print(json.dumps(doc, ensure_ascii=False, separators=(",", ":")))

    port = args.serial_port or env_first(env, "HIT_TARGET_SERIAL_PORT")
    if args.no_serial or not port:
        return 0

    payload = json.dumps(doc, ensure_ascii=False, separators=(",", ":"))
    if len(payload) > 4000:
        raise HitTargetProvisionError(f"provision JSON is {len(payload)} bytes; firmware serial line limit is 4096")
    write_serial(port, args.baud, f"provision {payload}", args.wait_s)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except HitTargetProvisionError as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(2)
