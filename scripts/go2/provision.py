#!/usr/bin/env python3
"""Provision or inspect the active Go2 hit/LED ESP runtime config.

This host-side helper builds the standard BattleBang runtime-config JSON shape and
sends it to firmware over the local serial management command surface.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Sequence

SCRIPTS_DIR = Path(__file__).resolve().parents[1]
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

from go2_runtime.provisioning import (  # noqa: E402
    PROJECT_ROOT,
    ProvisioningError,
    build_common_runtime_doc,
    env_first,
    env_int,
    make_serial_command,
    merged_env,
    redacted,
    require_value,
    write_serial,
)

PREFIXES: tuple[str, ...] = ("GO2", "ESP", "BATTLEBANG")
DEFAULT_ENV_FILE = PROJECT_ROOT / "firmware" / "go2" / ".env.go2"
DEFAULT_HIT_TOPIC_PREFIX = "battlebang/hit"
DEFAULT_OTA_MANIFEST_URL = (
    "https://github.com/KongPedia/battlebang-esp/releases/download/go2-latest/go2-manifest.json"
)


def prefixed_tuning_keys(suffix: str) -> tuple[str, ...]:
    return (
        f"GO2_{suffix}",
        f"ESP_{suffix}",
        f"BATTLEBANG_{suffix}",
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Build/send Go2 runtime provision/config JSON over serial.",
    )
    parser.add_argument(
        "--env-file",
        type=Path,
        default=DEFAULT_ENV_FILE,
        help=f"dotenv file to read (default: {DEFAULT_ENV_FILE})",
    )
    parser.add_argument(
        "--command",
        choices=("provision", "config", "status", "show-status", "show-config", "clear-config"),
        default="provision",
        help="firmware management command to send",
    )
    parser.add_argument(
        "--robot-id",
        help="Go2 robot id/device id; overrides GO2_ROBOT_ID/GO2_ID/ROBOT_ID/ESP_ROBOT_ID/BATTLEBANG_ROBOT_ID",
    )
    parser.add_argument(
        "--serial-port",
        help="USB serial port; overrides GO2_SERIAL_PORT/ESP_SERIAL_PORT/BATTLEBANG_SERIAL_PORT",
    )
    parser.add_argument(
        "--baud",
        type=int,
        help="serial baud rate; default 115200 or *_SERIAL_BAUD env",
    )
    parser.add_argument("--wait-s", type=float, default=2.0, help="seconds to read serial response after write")
    parser.add_argument("--no-serial", action="store_true", help="build the command but do not write to serial")
    parser.add_argument("--print-json", action="store_true", help="print generated JSON with secrets redacted")
    parser.add_argument(
        "--print-json-secrets",
        action="store_true",
        help="print generated JSON including Wi-Fi/MQTT passwords; do not use in logs",
    )
    parser.add_argument("--print-command", action="store_true", help="print the exact serial command line")
    return parser


def detect_robot_id(env: dict[str, str], cli_robot_id: str | None) -> str:
    return require_value(
        cli_robot_id
        or env_first(
            env,
            "GO2_ROBOT_ID",
            "GO2_ID",
            "ROBOT_ID",
            "ESP_ROBOT_ID",
            "BATTLEBANG_ROBOT_ID",
            default="",
        ),
        "--robot-id or GO2_ROBOT_ID/GO2_ID/ROBOT_ID/ESP_ROBOT_ID/BATTLEBANG_ROBOT_ID",
    )


def serial_port(env: dict[str, str], cli_serial_port: str | None) -> str | None:
    return cli_serial_port or env_first(
        env,
        "GO2_SERIAL_PORT",
        "ESP_SERIAL_PORT",
        "BATTLEBANG_SERIAL_PORT",
        default=None,
    )


def serial_baud(env: dict[str, str], cli_baud: int | None) -> int:
    if cli_baud is not None:
        return cli_baud
    return env_int(env, ("GO2_SERIAL_BAUD", "ESP_SERIAL_BAUD", "BATTLEBANG_SERIAL_BAUD"), 115200) or 115200


def build_payload(env: dict[str, str], action: str, robot_id: str) -> dict[str, Any]:
    doc = build_common_runtime_doc(
        env,
        prefixes=PREFIXES,
        command_type=action,
        device_id=robot_id,
        group_default="go2",
        ota_channel_default="go2",
        ota_public_manifest_url_default=DEFAULT_OTA_MANIFEST_URL,
    )
    hit_topic_prefix = env_first(
        env,
        "GO2_HIT_TOPIC_PREFIX",
        "GO2_MQTT_TOPIC_PREFIX",
        "ESP_MQTT_TOPIC_PREFIX",
        "BATTLEBANG_MQTT_TOPIC_PREFIX",
        default=DEFAULT_HIT_TOPIC_PREFIX,
    )
    doc["robot_id"] = robot_id
    doc["hit_topic_prefix"] = hit_topic_prefix
    doc["hit"] = {
        "robot_id": robot_id,
        "topic_prefix": hit_topic_prefix,
        "cooldown_ms": env_int(env, prefixed_tuning_keys("HIT_COOLDOWN_MS"), 0),
        "offline_queue_capacity": env_int(env, prefixed_tuning_keys("OFFLINE_HIT_QUEUE_CAPACITY"), 32),
        "offline_queue_flush_interval_ms": env_int(
            env,
            prefixed_tuning_keys("OFFLINE_HIT_QUEUE_FLUSH_INTERVAL_MS"),
            50,
        ),
        "led_brightness": env_int(env, prefixed_tuning_keys("LED_BRIGHTNESS"), 120),
        "piezo_ao_threshold_raw": env_int(env, prefixed_tuning_keys("PIEZO_AO_THRESHOLD_RAW"), 200),
        "piezo_ao_rearm_raw": env_int(env, prefixed_tuning_keys("PIEZO_AO_REARM_RAW"), 150),
        "piezo_ao_capture_window_ms": env_int(env, prefixed_tuning_keys("PIEZO_AO_CAPTURE_WINDOW_MS"), 30),
        "piezo_ao_debug_period_ms": env_int(env, prefixed_tuning_keys("PIEZO_AO_DEBUG_PERIOD_MS"), 100),
        "piezo_ao_rearm_stable_ms": env_int(env, prefixed_tuning_keys("PIEZO_AO_REARM_STABLE_MS"), 300),
        "max_hits": env_int(env, (*prefixed_tuning_keys("MAX_HITS"), *prefixed_tuning_keys("HITS_TO_DOWN")), 14),
        "hit_flash_ms": env_int(env, prefixed_tuning_keys("HIT_FLASH_MS"), 900),
    }
    return doc


def should_build_payload(action: str) -> bool:
    return action in {"provision", "config"}


def print_payload(doc: dict[str, Any], include_secrets: bool) -> None:
    printable = doc if include_secrets else redacted(doc)
    print(json.dumps(printable, ensure_ascii=False, sort_keys=True))


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    try:
        env = merged_env(args.env_file, PREFIXES)
        payload: dict[str, Any] | None = None
        if should_build_payload(args.command):
            robot_id = detect_robot_id(env, args.robot_id)
            payload = build_payload(env, args.command, robot_id)
            if args.print_json or args.print_json_secrets:
                print_payload(payload, args.print_json_secrets)

        command = make_serial_command(args.command, payload)
        if args.print_command:
            print(command if args.print_json_secrets else make_serial_command(args.command, redacted(payload) if payload else None))

        if args.no_serial:
            if not (args.print_json or args.print_json_secrets or args.print_command):
                if payload is not None:
                    print_payload(payload, include_secrets=False)
                else:
                    print(command)
            return 0

        port = serial_port(env, args.serial_port)
        if not port:
            raise ProvisioningError(
                "missing serial port: pass --serial-port or set GO2_SERIAL_PORT/ESP_SERIAL_PORT/BATTLEBANG_SERIAL_PORT; use --no-serial to only print JSON"
            )
        write_serial(port, serial_baud(env, args.baud), command, args.wait_s)
        return 0
    except ProvisioningError as exc:
        parser.exit(2, f"go2/provision.py: error: {exc}\n")


if __name__ == "__main__":
    raise SystemExit(main())
