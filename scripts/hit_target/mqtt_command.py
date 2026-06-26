#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import socket
import sys
import time
import urllib.request
from pathlib import Path
from typing import Any

PROJECT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_ENV_FILE = PROJECT_ROOT / "src" / "hit_target" / ".env.hit_target"
DEFAULT_LATEST_MANIFEST_URL = "https://github.com/KongPedia/battlebang-esp/releases/download/hit-target-latest/hit-target-manifest.json"


class HitTargetMqttError(RuntimeError):
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


def parse_bool(value: str) -> bool:
    normalized = value.strip().lower()
    if normalized in {"1", "true", "yes", "y", "on", "enable", "enabled"}:
        return True
    if normalized in {"0", "false", "no", "n", "off", "disable", "disabled"}:
        return False
    raise argparse.ArgumentTypeError(f"expected true/false, got {value!r}")


def mqtt_string(value: str) -> bytes:
    data = value.encode("utf-8")
    if len(data) > 65535:
        raise HitTargetMqttError("MQTT string too long")
    return len(data).to_bytes(2, "big") + data


def encode_remaining_length(length: int) -> bytes:
    out = bytearray()
    while True:
        encoded = length % 128
        length //= 128
        if length > 0:
            encoded |= 128
        out.append(encoded)
        if length == 0:
            return bytes(out)


def read_packet(sock: socket.socket) -> tuple[int, bytes]:
    first = sock.recv(1)
    if not first:
        raise HitTargetMqttError("MQTT broker closed connection before packet")
    multiplier = 1
    remaining = 0
    while True:
        raw = sock.recv(1)
        if not raw:
            raise HitTargetMqttError("MQTT broker closed connection while reading packet length")
        encoded = raw[0]
        remaining += (encoded & 127) * multiplier
        if (encoded & 128) == 0:
            break
        multiplier *= 128
        if multiplier > 128 * 128 * 128:
            raise HitTargetMqttError("malformed MQTT remaining length")
    payload = b""
    while len(payload) < remaining:
        chunk = sock.recv(remaining - len(payload))
        if not chunk:
            raise HitTargetMqttError("MQTT broker closed connection while reading packet payload")
        payload += chunk
    return first[0], payload


def publish_mqtt(host: str,
                 port: int,
                 topic: str,
                 payload: str,
                 username: str | None = None,
                 password: str | None = None,
                 timeout_s: float = 5.0,
                 retain: bool = False) -> None:
    client_id = f"bb-hit-cli-{int(time.time())}"
    connect_flags = 0x02
    payload_parts = [mqtt_string(client_id)]
    if username:
        connect_flags |= 0x80
        if password:
            connect_flags |= 0x40
        payload_parts.append(mqtt_string(username))
        if password:
            payload_parts.append(mqtt_string(password))
    variable = mqtt_string("MQTT") + bytes([4, connect_flags, 0, 60])
    connect_payload = b"".join(payload_parts)
    connect_packet = bytes([0x10]) + encode_remaining_length(len(variable) + len(connect_payload)) + variable + connect_payload
    publish_body = mqtt_string(topic) + payload.encode("utf-8")
    publish_flags = 0x31 if retain else 0x30
    publish_packet = bytes([publish_flags]) + encode_remaining_length(len(publish_body)) + publish_body
    disconnect_packet = bytes([0xE0, 0x00])
    with socket.create_connection((host, port), timeout=timeout_s) as sock:
      sock.settimeout(timeout_s)
      sock.sendall(connect_packet)
      packet_type, body = read_packet(sock)
      if packet_type != 0x20 or len(body) < 2 or body[1] != 0:
          raise HitTargetMqttError(f"MQTT connect rejected packet=0x{packet_type:02x} body={body!r}")
      sock.sendall(publish_packet)
      sock.sendall(disconnect_packet)


def clean_root(root: str) -> str:
    return root.strip("/") or "battlebang"


def is_safe_topic_segment(value: str) -> bool:
    return bool(value) and all(ch.isascii() and (ch.isalnum() or ch in "_.-") for ch in value)


def require_safe_topic_segment(value: str, field: str) -> str:
    normalized = value.strip()
    if not is_safe_topic_segment(normalized):
        raise HitTargetMqttError(f"{field} must use only A-Z, a-z, 0-9, '_', '-', or '.'")
    return normalized


def topic_for(root: str, kind: str, identifier: str | None, suffix: str, all_ota: bool = False) -> str:
    root = clean_root(root)
    if all_ota:
        return f"{root}/hit_targets/all/ota"
    if not identifier:
        raise HitTargetMqttError(f"missing --{kind}-id for {suffix} topic")
    identifier = require_safe_topic_segment(identifier, f"{kind}_id")
    if kind == "device":
        return f"{root}/devices/hit_target/{identifier}/{suffix}"
    return f"{root}/hit_targets/{identifier}/{suffix}"


def linked_device_collection(device_kind: str) -> str:
    normalized = require_safe_topic_segment(device_kind, "linked_device_kind").lower()
    return "turrets" if normalized == "turret" else f"devices/{normalized}"


def linked_device_status_topic(root: str, device_kind: str, device_id: str) -> str:
    device_id = require_safe_topic_segment(device_id, "linked_device_id")
    return f"{clean_root(root)}/{linked_device_collection(device_kind)}/{device_id}/status"


def load_json_arg(payload: str | None, json_file: Path | None) -> dict[str, Any]:
    if payload and json_file:
        raise HitTargetMqttError("use only one of --payload or --json-file")
    if json_file:
        return json.loads(json_file.read_text(encoding="utf-8"))
    if payload:
        return json.loads(payload)
    return {}


def build_config_payload(args: argparse.Namespace) -> dict[str, Any]:
    doc = load_json_arg(args.payload, args.json_file)
    if not doc:
        doc = {"type": "config", "config_version": int(args.config_version or time.time())}
    doc.setdefault("type", "config")
    doc.setdefault("config_version", int(args.config_version or time.time()))
    if args.phase_count is not None or args.hits_per_phase is not None or args.palette:
        hp = dict(doc.get("hp") or {})
        if args.phase_count is not None:
            hp["phase_count"] = args.phase_count
        if args.hits_per_phase is not None:
            hp["hits_per_phase"] = args.hits_per_phase
        if args.palette:
            hp["palette"] = [part.strip() for part in args.palette.split(",") if part.strip()]
        doc["hp"] = hp
    if args.debug_allow_simulate_hit is not None:
        doc["debug_allow_simulate_hit"] = args.debug_allow_simulate_hit
    sensor: dict[str, Any] = dict(doc.get("sensor") or {})
    if args.piezo_do_pin is not None:
        sensor["piezo_do_pin"] = args.piezo_do_pin
    if args.piezo_ao_pin is not None:
        sensor["piezo_ao_pin"] = args.piezo_ao_pin
    if args.hit_threshold is not None:
        sensor["hit_threshold"] = args.hit_threshold
    if args.hit_rearm_threshold is not None:
        sensor["hit_rearm_threshold"] = args.hit_rearm_threshold
    if args.hit_cooldown_ms is not None:
        sensor["hit_cooldown_ms"] = args.hit_cooldown_ms
    if args.hit_rearm_stable_ms is not None:
        sensor["hit_rearm_stable_ms"] = args.hit_rearm_stable_ms
    if args.hit_rearm_check_ms is not None:
        sensor["hit_rearm_check_ms"] = args.hit_rearm_check_ms
    if args.digital_hit_min_edges is not None:
        sensor["digital_hit_min_edges"] = args.digital_hit_min_edges
    if args.digital_isr_debounce_us is not None:
        sensor["digital_isr_debounce_us"] = args.digital_isr_debounce_us
    if args.capture_window_ms is not None:
        sensor["capture_window_ms"] = args.capture_window_ms
    if sensor:
        doc["sensor"] = sensor
    visual: dict[str, Any] = dict(doc.get("visual") or {})
    if args.damage_chip_ms is not None:
        visual["damage_chip_ms"] = args.damage_chip_ms
    if args.phase_backfill_gap_leds is not None:
        visual["phase_backfill_gap_leds"] = args.phase_backfill_gap_leds
    if args.phase_backfill_scale is not None:
        visual["phase_backfill_scale"] = args.phase_backfill_scale
    if args.defeat_blackout_ms is not None:
        visual["defeat_blackout_ms"] = args.defeat_blackout_ms
    if args.defeat_rainbow_ms is not None:
        visual["defeat_rainbow_ms"] = args.defeat_rainbow_ms
    if args.defeat_rainbow_spins is not None:
        visual["defeat_rainbow_spins"] = args.defeat_rainbow_spins
    if visual:
        doc["visual"] = visual
    activation: dict[str, Any] = dict(doc.get("activation") or {})
    if args.activation_mode:
        activation["mode"] = "linked_device" if args.activation_mode == "linked_turret" else args.activation_mode
    if args.linked_device_kind is not None:
        activation["linked_device_kind"] = args.linked_device_kind
    linked_device_id = args.linked_device_id
    if args.linked_turret_id is not None:
        activation["linked_device_kind"] = "turret"
        linked_device_id = args.linked_turret_id
    if linked_device_id is not None:
        activation["linked_device_id"] = linked_device_id
    if args.activation_stale_ms is not None:
        activation["stale_ms"] = args.activation_stale_ms
    if activation:
        doc["activation"] = activation
    led: dict[str, Any] = dict(doc.get("led") or {})
    if args.led_pin is not None:
        led["pin"] = args.led_pin
    if args.num_leds is not None:
        led["num_leds"] = args.num_leds
    if args.led_type:
        led["type"] = args.led_type
    if args.color_order:
        led["color_order"] = args.color_order
    if args.led_brightness is not None:
        led["brightness"] = args.led_brightness
    if args.led_max_ma is not None:
        led["max_ma"] = args.led_max_ma
    if led:
        doc["led"] = led
    reset: dict[str, Any] = dict(doc.get("reset") or {})
    if args.reset_button_pin is not None:
        reset["button_pin"] = args.reset_button_pin
    if args.reset_button_hold_ms is not None:
        reset["button_hold_ms"] = args.reset_button_hold_ms
    if reset:
        doc["reset"] = reset
    ota: dict[str, Any] = dict(doc.get("ota") or {})
    if args.ota_auto_check is not None:
        ota["auto_check_enabled"] = args.ota_auto_check
    if args.ota_desired_build is not None:
        ota["desired_build"] = args.ota_desired_build
    if args.ota_manifest_url:
        ota["public_manifest_url"] = args.ota_manifest_url
    if args.ota_command_center_controlled is not None:
        ota["command_center_controlled"] = args.ota_command_center_controlled
    if ota:
        doc["ota"] = ota
    return doc


def build_command_payload(args: argparse.Namespace) -> dict[str, Any]:
    if args.payload or args.json_file:
        return load_json_arg(args.payload, args.json_file)
    return {"command": args.command}


def build_bench_open_payload(args: argparse.Namespace) -> dict[str, Any]:
    doc: dict[str, Any] = {
        "type": "config",
        "config_version": int(args.config_version or time.time()),
        "activation": {
            "mode": "always_on",
            "linked_device_kind": "turret",
            "linked_device_id": "",
            "stale_ms": args.activation_stale_ms,
        },
    }
    if args.allow_simulate_hit or args.simulate_hits > 0:
        doc["debug_allow_simulate_hit"] = True
    return doc


def build_bench_close_payload(args: argparse.Namespace) -> dict[str, Any]:
    linked_device_kind = (args.linked_device_kind or "turret").strip() or "turret"
    linked_device_id = (args.linked_device_id or args.linked_turret_id or "").strip()
    if not linked_device_id:
        raise HitTargetMqttError("bench-close requires --linked-device-id, e.g. turret_4")
    return {
        "type": "config",
        "config_version": int(args.config_version or time.time()),
        "activation": {
            "mode": "linked_device",
            "linked_device_kind": linked_device_kind,
            "linked_device_id": linked_device_id,
            "stale_ms": args.activation_stale_ms,
        },
        "debug_allow_simulate_hit": bool(args.allow_simulate_hit),
    }


def build_linked_device_status_payload(args: argparse.Namespace) -> dict[str, Any]:
    preset = args.preset
    presets: dict[str, dict[str, Any]] = {
        "active": {
            "mode": "PATTERN",
            "command_state": "busy",
            "ready_for_next_command": False,
            "pattern_state": "ACTIVE",
            "fire_state": "SAFE_OFF",
            "reason": "bench_active",
        },
        "idle": {
            "mode": "WAIT_COMMAND",
            "command_state": "ready",
            "ready_for_next_command": True,
            "pattern_state": "IDLE",
            "fire_state": "SAFE_OFF",
            "reason": "bench_idle",
        },
        "dead": {
            "mode": "DEAD",
            "command_state": "dead",
            "ready_for_next_command": False,
            "pattern_state": "IDLE",
            "fire_state": "SAFE_OFF",
            "reason": "bench_dead",
        },
        "home": {
            "mode": "HOME",
            "command_state": "busy",
            "ready_for_next_command": False,
            "pattern_state": "IDLE",
            "fire_state": "SAFE_OFF",
            "reason": "bench_home",
        },
        "target": {
            "mode": "TARGET",
            "command_state": "busy",
            "ready_for_next_command": False,
            "pattern_state": "IDLE",
            "fire_state": "SAFE_OFF",
            "reason": "bench_target",
        },
        "firing": {
            "mode": "FIRING",
            "command_state": "busy",
            "ready_for_next_command": False,
            "pattern_state": "FIRE",
            "fire_state": "FIRING",
            "reason": "bench_firing",
        },
        "blocked": {
            "mode": "WAIT_COMMAND",
            "command_state": "blocked_brownout",
            "ready_for_next_command": False,
            "pattern_state": "IDLE",
            "fire_state": "SAFE_OFF",
            "reason": "bench_blocked",
        },
    }
    doc = dict(presets[preset])
    device_kind = (getattr(args, "device_kind", "") or "turret").strip().lower() or "turret"
    device_id = (getattr(args, "device_id", "") or getattr(args, "turret_id", "")).strip()
    doc["device_id"] = device_id
    if device_kind == "turret":
        doc["turret_id"] = device_id
    if args.mode:
        doc["mode"] = args.mode
    if args.command_state:
        doc["command_state"] = args.command_state
    if args.pattern_state:
        doc["pattern_state"] = args.pattern_state
    if args.fire_state:
        doc["fire_state"] = args.fire_state
    if args.ready is not None:
        doc["ready_for_next_command"] = args.ready
    if args.active_command_id:
        doc["active_command_id"] = args.active_command_id
    return doc


def build_ota_payload(args: argparse.Namespace) -> dict[str, Any]:
    if args.manifest_url:
        req = urllib.request.Request(args.manifest_url, headers={"User-Agent": "battlebang-hit-target-mqtt-helper"})
        with urllib.request.urlopen(req, timeout=args.timeout_s) as response:
            return json.loads(response.read().decode("utf-8"))
    if args.manifest_file:
        return json.loads(args.manifest_file.read_text(encoding="utf-8"))
    return load_json_arg(args.payload, args.json_file)


def add_common(sub: argparse.ArgumentParser) -> None:
    sub.add_argument("--env-file", type=Path, default=Path(os.environ.get("HIT_TARGET_ENV_FILE", DEFAULT_ENV_FILE)))
    sub.add_argument("--host", help="MQTT broker host; default/env HIT_TARGET_MQTT_HOST")
    sub.add_argument("--port", type=int, help="MQTT broker port; default/env 1883")
    sub.add_argument("--root", help="MQTT root; default/env battlebang")
    sub.add_argument("--username", help="MQTT username")
    sub.add_argument("--password", help="MQTT password")
    sub.add_argument("--target-id", help="target id, e.g. hit_target_AABBCCDDEEFF")
    sub.add_argument("--device-id", help="device id, e.g. esp32-AABBCCDDEEFF")
    sub.add_argument("--timeout-s", type=float, default=5.0)
    sub.add_argument("--print-only", action="store_true", help="print topic/payload without publishing")


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Publish BattleBang hit_target MQTT config/command/OTA messages.")
    subparsers = parser.add_subparsers(dest="action", required=True)

    config = subparsers.add_parser("config", help="publish runtime config patch")
    add_common(config)
    config.add_argument("--payload", help="raw JSON object")
    config.add_argument("--json-file", type=Path)
    config.add_argument("--config-version", type=int)
    config.add_argument("--phase-count", type=int)
    config.add_argument("--hits-per-phase", type=int)
    config.add_argument("--palette", help="comma-separated #RRGGBB colors")
    config.add_argument("--debug-allow-simulate-hit", type=parse_bool)
    config.add_argument("--piezo-do-pin", type=int)
    config.add_argument("--piezo-ao-pin", type=int)
    config.add_argument("--hit-threshold", type=int)
    config.add_argument("--hit-rearm-threshold", type=int)
    config.add_argument("--hit-cooldown-ms", type=int)
    config.add_argument("--hit-rearm-stable-ms", type=int)
    config.add_argument("--hit-rearm-check-ms", type=int)
    config.add_argument("--digital-hit-min-edges", type=int)
    config.add_argument("--digital-isr-debounce-us", type=int)
    config.add_argument("--capture-window-ms", type=int)
    config.add_argument("--damage-chip-ms", type=int)
    config.add_argument("--phase-backfill-gap-leds", type=int)
    config.add_argument("--phase-backfill-scale", type=int)
    config.add_argument("--defeat-blackout-ms", type=int)
    config.add_argument("--defeat-rainbow-ms", type=int)
    config.add_argument("--defeat-rainbow-spins", type=int)
    config.add_argument("--activation-mode", choices=["always_on", "linked_device", "linked_turret"])
    config.add_argument("--linked-device-kind")
    config.add_argument("--linked-device-id")
    config.add_argument("--linked-turret-id", help=argparse.SUPPRESS)
    config.add_argument("--activation-stale-ms", type=int)
    config.add_argument("--led-pin", type=int)
    config.add_argument("--num-leds", type=int)
    config.add_argument("--led-type")
    config.add_argument("--color-order")
    config.add_argument("--led-brightness", type=int)
    config.add_argument("--led-max-ma", type=int)
    config.add_argument("--reset-button-pin", type=int)
    config.add_argument("--reset-button-hold-ms", type=int)
    config.add_argument("--ota-auto-check", type=parse_bool)
    config.add_argument("--ota-desired-build", type=int)
    config.add_argument("--ota-manifest-url", default=DEFAULT_LATEST_MANIFEST_URL)
    config.add_argument("--ota-command-center-controlled", type=parse_bool)

    command = subparsers.add_parser("command", help="publish reset/status/enable/disable/simulate_hit")
    add_common(command)
    command.add_argument("command", choices=["reset", "status", "enable", "disable", "simulate_hit"])
    command.add_argument("--payload", help="raw JSON object override")
    command.add_argument("--json-file", type=Path)

    bench_open = subparsers.add_parser(
        "bench-open",
        help="open a turret-free hit window by switching activation.mode to always_on",
    )
    add_common(bench_open)
    bench_open.add_argument("--config-version", type=int)
    bench_open.add_argument("--activation-stale-ms", type=int, default=3000)
    bench_open.add_argument("--allow-simulate-hit", action="store_true")
    bench_open.add_argument("--no-reset", action="store_true", help="do not reset HP after opening the bench window")
    bench_open.add_argument("--simulate-hits", type=int, default=0, help="publish N simulate_hit commands after opening")
    bench_open.add_argument("--hit-interval-s", type=float, default=0.25)

    bench_hit = subparsers.add_parser(
        "bench-hit",
        help="publish simulate_hit command(s); use bench-open --allow-simulate-hit first",
    )
    add_common(bench_hit)
    bench_hit.add_argument("--count", type=int, default=1)
    bench_hit.add_argument("--interval-s", type=float, default=0.25)

    bench_close = subparsers.add_parser(
        "bench-close",
        help="restore linked_device activation after a device-free bench test",
    )
    add_common(bench_close)
    bench_close.add_argument("--config-version", type=int)
    bench_close.add_argument("--linked-device-kind", default="turret")
    bench_close.add_argument("--linked-device-id")
    bench_close.add_argument("--linked-turret-id", help=argparse.SUPPRESS)
    bench_close.add_argument("--activation-stale-ms", type=int, default=3000)
    bench_close.add_argument("--allow-simulate-hit", action="store_true")

    linked_device_status = subparsers.add_parser(
        "linked-device-status",
        help="publish fake linked-device status for LED/vulnerability tests without a real device",
    )
    add_common(linked_device_status)
    linked_device_status.add_argument("device_id", help="linked device id configured on the hit target, e.g. turret_4")
    linked_device_status.add_argument("--device-kind", default="turret")
    linked_device_status.add_argument(
        "preset",
        choices=["active", "idle", "dead", "home", "target", "firing", "blocked"],
        help="status preset; active/PATTERN turns LED/vulnerability on, idle/dead/blocked turn it off",
    )
    linked_device_status.add_argument("--duration-s", type=float, default=20.0, help="seconds to keep publishing")
    linked_device_status.add_argument("--interval-s", type=float, default=0.5, help="freshness heartbeat interval")
    linked_device_status.add_argument("--retain", action="store_true", help="publish retained status; use carefully on shared brokers")
    linked_device_status.add_argument("--mode")
    linked_device_status.add_argument("--command-state")
    linked_device_status.add_argument("--pattern-state")
    linked_device_status.add_argument("--fire-state")
    linked_device_status.add_argument("--ready", type=parse_bool)
    linked_device_status.add_argument("--active-command-id")

    turret_status = subparsers.add_parser(
        "turret-status",
        help=argparse.SUPPRESS,
    )
    add_common(turret_status)
    turret_status.add_argument("turret_id")
    turret_status.add_argument("preset", choices=["active", "idle", "dead", "home", "target", "firing", "blocked"])
    turret_status.add_argument("--duration-s", type=float, default=20.0)
    turret_status.add_argument("--interval-s", type=float, default=0.5)
    turret_status.add_argument("--retain", action="store_true")
    turret_status.add_argument("--mode")
    turret_status.add_argument("--command-state")
    turret_status.add_argument("--pattern-state")
    turret_status.add_argument("--fire-state")
    turret_status.add_argument("--ready", type=parse_bool)
    turret_status.add_argument("--active-command-id")

    ota = subparsers.add_parser("ota", help="publish an OTA manifest JSON")
    add_common(ota)
    ota.add_argument("--all", action="store_true", help="publish to {root}/hit_targets/all/ota")
    ota.add_argument("--manifest-url", default=DEFAULT_LATEST_MANIFEST_URL)
    ota.add_argument("--manifest-file", type=Path)
    ota.add_argument("--payload", help="raw JSON object override")
    ota.add_argument("--json-file", type=Path)
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = make_parser()
    args = parser.parse_args(argv)
    env = merged_env(args.env_file)
    host = args.host or env_first(env, "HIT_TARGET_MQTT_HOST")
    if not host:
        parser.error("missing --host or HIT_TARGET_MQTT_HOST")
    port = int(args.port or env_first(env, "HIT_TARGET_MQTT_PORT", default="1883") or "1883")
    root = args.root or env_first(env, "HIT_TARGET_MQTT_ROOT", default="battlebang") or "battlebang"
    username = args.username or env_first(env, "HIT_TARGET_MQTT_USERNAME")
    password = args.password or env_first(env, "HIT_TARGET_MQTT_PASSWORD")

    if args.action in {"linked-device-status", "turret-status"}:
        if args.action == "turret-status":
            args.device_kind = "turret"
            args.device_id = args.turret_id
        topic = linked_device_status_topic(root, args.device_kind, args.device_id)
        payload = json.dumps(build_linked_device_status_payload(args), ensure_ascii=False, separators=(",", ":"))
        duration_s = max(0.0, float(args.duration_s))
        interval_s = max(0.05, float(args.interval_s))
        count = 1 if duration_s == 0 else max(1, int(duration_s / interval_s) + 1)
        for index in range(count):
            if args.print_only:
                print(topic)
                print(payload)
            else:
                publish_mqtt(
                    host,
                    port,
                    topic,
                    payload,
                    username=username,
                    password=password,
                    timeout_s=args.timeout_s,
                    retain=args.retain,
                )
                print(f"published topic={topic} bytes={len(payload)} preset={args.preset} index={index + 1}/{count}")
            if index + 1 < count:
                time.sleep(interval_s)
        return 0

    target_id = args.target_id or env_first(env, "HIT_TARGET_TARGET_ID")
    device_id = args.device_id or env_first(env, "HIT_TARGET_DEVICE_ID")
    kind = "target" if target_id else "device"
    identifier = target_id or device_id

    def publish_doc(suffix: str, payload_doc: dict[str, Any], *, all_ota: bool = False) -> None:
        topic = topic_for(root, kind, identifier, suffix, all_ota=all_ota)
        payload = json.dumps(payload_doc, ensure_ascii=False, separators=(",", ":"))
        if args.print_only:
            print(topic)
            print(payload)
            return
        publish_mqtt(host, port, topic, payload, username=username, password=password, timeout_s=args.timeout_s)
        print(f"published topic={topic} bytes={len(payload)}")

    if args.action == "bench-open":
        publish_doc("config", build_bench_open_payload(args))
        if not args.no_reset:
            publish_doc("command", {"command": "reset"})
        for _ in range(max(0, int(args.simulate_hits))):
            if args.hit_interval_s > 0:
                time.sleep(args.hit_interval_s)
            publish_doc("command", {"command": "simulate_hit"})
        return 0

    if args.action == "bench-hit":
        for index in range(max(1, int(args.count))):
            if index > 0 and args.interval_s > 0:
                time.sleep(args.interval_s)
            publish_doc("command", {"command": "simulate_hit"})
        return 0

    if args.action == "bench-close":
        publish_doc("config", build_bench_close_payload(args))
        return 0

    if args.action == "command":
        publish_doc("command", build_command_payload(args))
        return 0
    if args.action == "config":
        publish_doc("config", build_config_payload(args))
        return 0

    payload_doc = build_ota_payload(args)
    topic = topic_for(root, kind, identifier, "ota", all_ota=args.all)
    payload = json.dumps(payload_doc, ensure_ascii=False, separators=(",", ":"))
    if args.print_only:
        print(topic)
        print(payload)
        return 0
    publish_mqtt(host, port, topic, payload, username=username, password=password, timeout_s=args.timeout_s)
    print(f"published topic={topic} bytes={len(payload)}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (HitTargetMqttError, OSError, json.JSONDecodeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(2)
