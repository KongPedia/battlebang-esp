#!/usr/bin/env python3
"""Provision or inspect the active Go2+Nixo integrated ESP runtime config.

This helper provisions identity/network/MQTT/OTA policy plus per-device hit and
Nixo fire timing values. Relay pins, polarity, and channel count remain
build/variant-time safety data.
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

PREFIXES: tuple[str, ...] = ("GO2_NIXO", "GO2", "ESP", "BATTLEBANG")
DEFAULT_ENV_FILE = PROJECT_ROOT / "firmware" / "go2_nixo" / ".env.go2_nixo"
DEFAULT_HIT_TOPIC_PREFIX = "battlebang/hit"
DEFAULT_NIXO_COMMAND_TOPIC_PREFIX = "battlebang/nixo"
RELAY_VARIANT_TO_OTA = {
    "relay_1ch": (
        "go2-nixo-1ch",
        "https://github.com/KongPedia/battlebang-esp/releases/download/"
        "go2-nixo-1ch-latest/go2-nixo-1ch-manifest.json",
    ),
    "relay_2ch": (
        "go2-nixo-2ch",
        "https://github.com/KongPedia/battlebang-esp/releases/download/"
        "go2-nixo-2ch-latest/go2-nixo-2ch-manifest.json",
    ),
}


def prefixed_tuning_keys(suffix: str) -> tuple[str, ...]:
    return (
        f"GO2_NIXO_{suffix}",
        f"GO2_{suffix}",
        f"ESP_{suffix}",
        f"BATTLEBANG_{suffix}",
    )


def nixo_tuning_keys(suffix: str) -> tuple[str, ...]:
    return (
        f"GO2_NIXO_{suffix}",
        f"NIXO_{suffix}",
        f"BATTLEBANG_NIXO_{suffix}",
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Build/send Go2-Nixo runtime provision/config JSON over serial.",
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
        help="Go2 robot id/device id; overrides GO2_NIXO_ROBOT_ID/GO2_ROBOT_ID/GO2_ID/ROBOT_ID/ESP_ROBOT_ID/BATTLEBANG_ROBOT_ID",
    )
    parser.add_argument("--nixo-id", help="Nixo command identity; default nixo_<robot-id>")
    parser.add_argument(
        "--relay-variant",
        choices=tuple(sorted(RELAY_VARIANT_TO_OTA)),
        help="flashed relay hardware variant; controls default OTA channel/manifest URL",
    )
    parser.add_argument(
        "--serial-port",
        help="USB serial port; overrides GO2_NIXO_SERIAL_PORT/GO2_SERIAL_PORT/ESP_SERIAL_PORT/BATTLEBANG_SERIAL_PORT",
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
            "GO2_NIXO_ROBOT_ID",
            "GO2_ROBOT_ID",
            "GO2_ID",
            "ROBOT_ID",
            "ESP_ROBOT_ID",
            "BATTLEBANG_ROBOT_ID",
            default="",
        ),
        "--robot-id or GO2_NIXO_ROBOT_ID/GO2_ROBOT_ID/GO2_ID/ROBOT_ID/ESP_ROBOT_ID/BATTLEBANG_ROBOT_ID",
    )


def detect_nixo_id(env: dict[str, str], cli_nixo_id: str | None, robot_id: str) -> str:
    return cli_nixo_id or env_first(
        env,
        "GO2_NIXO_NIXO_ID",
        "NIXO_ID",
        "BATTLEBANG_NIXO_ID",
        default=f"nixo_{robot_id}",
    ) or f"nixo_{robot_id}"


def serial_port(env: dict[str, str], cli_serial_port: str | None) -> str | None:
    return cli_serial_port or env_first(
        env,
        "GO2_NIXO_SERIAL_PORT",
        "GO2_SERIAL_PORT",
        "ESP_SERIAL_PORT",
        "BATTLEBANG_SERIAL_PORT",
        default=None,
    )


def serial_baud(env: dict[str, str], cli_baud: int | None) -> int:
    if cli_baud is not None:
        return cli_baud
    return env_int(
        env,
        ("GO2_NIXO_SERIAL_BAUD", "GO2_SERIAL_BAUD", "ESP_SERIAL_BAUD", "BATTLEBANG_SERIAL_BAUD"),
        115200,
    ) or 115200


def detect_relay_variant(env: dict[str, str], cli_relay_variant: str | None) -> str:
    variant = cli_relay_variant or env_first(
        env,
        "GO2_NIXO_RELAY_VARIANT",
        "NIXO_RELAY_VARIANT",
        "BATTLEBANG_NIXO_RELAY_VARIANT",
        default="relay_1ch",
    )
    if variant not in RELAY_VARIANT_TO_OTA:
        supported = ", ".join(sorted(RELAY_VARIANT_TO_OTA))
        raise ProvisioningError(f"unsupported relay variant {variant!r}; expected one of: {supported}")
    return variant


def build_payload(
    env: dict[str, str],
    action: str,
    robot_id: str,
    nixo_id: str,
    relay_variant: str,
) -> dict[str, Any]:
    ota_channel_default, ota_manifest_default = RELAY_VARIANT_TO_OTA[relay_variant]
    doc = build_common_runtime_doc(
        env,
        prefixes=PREFIXES,
        command_type=action,
        device_id=robot_id,
        group_default="go2_nixo",
        ota_channel_default=ota_channel_default,
        ota_public_manifest_url_default=ota_manifest_default,
    )
    hit_topic_prefix = env_first(
        env,
        "GO2_NIXO_HIT_TOPIC_PREFIX",
        "GO2_HIT_TOPIC_PREFIX",
        "GO2_NIXO_MQTT_TOPIC_PREFIX",
        "GO2_MQTT_TOPIC_PREFIX",
        "ESP_MQTT_TOPIC_PREFIX",
        "BATTLEBANG_MQTT_TOPIC_PREFIX",
        default=DEFAULT_HIT_TOPIC_PREFIX,
    )
    nixo_command_topic_prefix = env_first(
        env,
        "GO2_NIXO_COMMAND_TOPIC_PREFIX",
        "GO2_NIXO_NIXO_COMMAND_TOPIC_PREFIX",
        "NIXO_MQTT_TOPIC_PREFIX",
        "BATTLEBANG_NIXO_MQTT_TOPIC_PREFIX",
        default=DEFAULT_NIXO_COMMAND_TOPIC_PREFIX,
    )
    doc["robot_id"] = robot_id
    doc["hit_topic_prefix"] = hit_topic_prefix
    doc["nixo_id"] = nixo_id
    doc["nixo_command_topic_prefix"] = nixo_command_topic_prefix
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
        "ring_brightness": env_int(env, prefixed_tuning_keys("RING_LED_BRIGHTNESS"), 80),
        "piezo_ao_threshold_raw": env_int(env, prefixed_tuning_keys("PIEZO_AO_THRESHOLD_RAW"), 200),
        "piezo_ao_rearm_raw": env_int(env, prefixed_tuning_keys("PIEZO_AO_REARM_RAW"), 150),
        "piezo_ao_capture_window_ms": env_int(env, prefixed_tuning_keys("PIEZO_AO_CAPTURE_WINDOW_MS"), 30),
        "piezo_ao_debug_period_ms": env_int(env, prefixed_tuning_keys("PIEZO_AO_DEBUG_PERIOD_MS"), 100),
        "piezo_ao_rearm_stable_ms": env_int(env, prefixed_tuning_keys("PIEZO_AO_REARM_STABLE_MS"), 300),
    }
    doc["nixo"] = {
        "id": nixo_id,
        "command_topic_prefix": nixo_command_topic_prefix,
        "fire_default_duration_ms": env_int(env, nixo_tuning_keys("FIRE_DEFAULT_DURATION_MS"), 3000),
        "fire_min_duration_ms": env_int(env, nixo_tuning_keys("FIRE_MIN_DURATION_MS"), 100),
        "fire_max_duration_ms": env_int(env, nixo_tuning_keys("FIRE_MAX_DURATION_MS"), 10000),
        "fire_cooldown_ms": env_int(env, nixo_tuning_keys("FIRE_COOLDOWN_MS"), 1500),
        "prefire_delay_ms": env_int(env, nixo_tuning_keys("PREFIRE_DELAY_MS"), 600),
        "relay_delay1_ms": env_int(env, nixo_tuning_keys("RELAY_DELAY1_MS"), 800),
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
            nixo_id = detect_nixo_id(env, args.nixo_id, robot_id)
            relay_variant = detect_relay_variant(env, args.relay_variant)
            payload = build_payload(env, args.command, robot_id, nixo_id, relay_variant)
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
                "missing serial port: pass --serial-port or set GO2_NIXO_SERIAL_PORT/GO2_SERIAL_PORT/ESP_SERIAL_PORT/BATTLEBANG_SERIAL_PORT; use --no-serial to only print JSON"
            )
        write_serial(port, serial_baud(env, args.baud), command, args.wait_s)
        return 0
    except ProvisioningError as exc:
        parser.exit(2, f"go2_nixo/provision.py: error: {exc}\n")


if __name__ == "__main__":
    raise SystemExit(main())
