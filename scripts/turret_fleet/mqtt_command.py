#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import socket
import sys
import time
from pathlib import Path
from typing import Any

PROJECT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_ENV_FILE = PROJECT_ROOT / "src" / "turret_fleet" / ".env.turret_fleet"


class MqttCommandError(RuntimeError):
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
        if key.startswith("TURRET_") or key.startswith("BATTLEBANG_"):
            values[key] = value
    return values


def env_first(env: dict[str, str], *keys: str, default: str | None = None) -> str | None:
    for key in keys:
        value = env.get(key)
        if value is not None and value != "":
            return value
    return default


def parse_bool_text(value: str) -> bool:
    normalized = value.strip().lower()
    if normalized in {"1", "true", "yes", "y", "on", "enable", "enabled"}:
        return True
    if normalized in {"0", "false", "no", "n", "off", "disable", "disabled"}:
        return False
    raise argparse.ArgumentTypeError(f"expected boolean true/false, got {value!r}")


def normalize_turret_id(raw: str) -> str:
    if raw.isdigit():
        return f"turret_{raw}"
    if raw.startswith("turret_"):
        return raw
    raise MqttCommandError(f"invalid turret id {raw!r}; use turret_2 or 2")


def make_command_id(action: str) -> str:
    return f"cli-{action}-{int(time.time())}"


def topic_for(root: str, turret_id: str, suffix: str) -> str:
    clean_root = root.strip("/") or "battlebang"
    return f"{clean_root}/turrets/{turret_id}/{suffix}"


def config_version(value: int | None) -> int:
    return int(value if value is not None else time.time())


def build_command_payload(args: argparse.Namespace) -> tuple[str, dict[str, Any]]:
    action = args.action

    if action in {"idle", "dead", "hold", "wait", "home", "init", "initiate", "recover"}:
        command_id = args.command_id or make_command_id(action)
        if action == "wait":
            command = "hold"
        elif action in {"init", "initiate"}:
            command = "home"
        else:
            command = action
        return "command", {"command": command, "command_id": command_id}

    if action == "target":
        command_id = args.command_id or make_command_id(action)
        payload: dict[str, Any] = {
            "command": "target",
            "command_id": command_id,
            "target": {"x": args.x, "y": args.y, "z": args.z},
        }
        if args.frame_id:
            payload["frame_id"] = args.frame_id
        return "command", payload

    if action == "aim":
        command_id = args.command_id or make_command_id(action)
        payload = {
            "command": "aim",
            "command_id": command_id,
            "yaw_deg": args.yaw_deg,
            "pitch_deg": args.pitch_deg,
        }
        if args.frame_id:
            payload["frame_id"] = args.frame_id
        return "command", payload

    if action == "jog":
        command_id = args.command_id or make_command_id(action)
        return "command", {
            "command": "jog",
            "command_id": command_id,
            "axis": args.axis,
            "direction": args.direction,
            "delta_us": args.delta_us,
            "duration_ms": args.duration_ms,
        }

    if action == "fire":
        command_id = args.command_id or make_command_id(action)
        return "command", {
            "command": "fire",
            "command_id": command_id,
            "duration_ms": args.duration_ms,
        }

    if action == "config":
        doc: dict[str, Any] = {
            "type": "config",
            "schema": 2,
            "config_version": config_version(args.config_version),
        }
        calibration: dict[str, Any] = {}
        if args.yaw_offset_deg is not None:
            calibration["yaw_offset_deg"] = args.yaw_offset_deg
        if args.pitch_offset_deg is not None:
            calibration["pitch_offset_deg"] = args.pitch_offset_deg
        if args.yaw_axis_offset_deg is not None:
            calibration["yaw_axis_offset_deg"] = args.yaw_axis_offset_deg
        if args.pitch_axis_offset_deg is not None:
            calibration["pitch_axis_offset_deg"] = args.pitch_axis_offset_deg
        if calibration:
            doc["calibration"] = calibration

        motion: dict[str, Any] = {}
        if args.yaw_stop_us is not None:
            motion["yaw_stop_us"] = args.yaw_stop_us
        if args.pitch_stop_us is not None:
            motion["pitch_stop_us"] = args.pitch_stop_us
        if args.servo_max_delta_us is not None:
            motion["servo_max_delta_us"] = args.servo_max_delta_us
        if args.yaw_max_delta_us is not None:
            motion["yaw_max_delta_us"] = args.yaw_max_delta_us
        if args.pitch_max_delta_us is not None:
            motion["pitch_max_delta_us"] = args.pitch_max_delta_us
        if args.yaw_min_drive_us is not None:
            motion["yaw_min_drive_us"] = args.yaw_min_drive_us
        if args.pitch_min_drive_us is not None:
            motion["pitch_min_drive_us"] = args.pitch_min_drive_us
        if args.servo_attach_settle_ms is not None:
            motion["servo_attach_settle_ms"] = args.servo_attach_settle_ms
        if args.axis_switch_cooldown_ms is not None:
            motion["axis_switch_cooldown_ms"] = args.axis_switch_cooldown_ms
        if args.axis_divergence_guard_ms is not None:
            motion["axis_divergence_guard_ms"] = args.axis_divergence_guard_ms
        if args.axis_divergence_margin_deg is not None:
            motion["axis_divergence_margin_deg"] = args.axis_divergence_margin_deg
        limits: dict[str, Any] = {}
        for cli_name, json_name in [
            ("yaw_min_deg", "yaw_min_deg"),
            ("yaw_max_deg", "yaw_max_deg"),
            ("pitch_min_deg", "pitch_min_deg"),
            ("pitch_max_deg", "pitch_max_deg"),
        ]:
            value = getattr(args, cli_name)
            if value is not None:
                limits[json_name] = value
        if limits:
            motion["limits"] = limits
        home: dict[str, Any] = {}
        if args.home_yaw_deg is not None:
            home["yaw_deg"] = args.home_yaw_deg
        if args.home_pitch_deg is not None:
            home["pitch_deg"] = args.home_pitch_deg
        if home:
            motion["home"] = home
        idle: dict[str, Any] = {}
        for cli_name, json_name in [
            ("idle_yaw_min_deg", "yaw_min_deg"),
            ("idle_yaw_max_deg", "yaw_max_deg"),
            ("idle_yaw_speed_deg_s", "yaw_speed_deg_s"),
            ("idle_pitch_min_deg", "pitch_min_deg"),
            ("idle_pitch_max_deg", "pitch_max_deg"),
            ("idle_pitch_speed_deg_s", "pitch_speed_deg_s"),
        ]:
            value = getattr(args, cli_name)
            if value is not None:
                idle[json_name] = value
        if idle:
            motion["idle"] = idle
        if args.dead_pitch_deg is not None:
            motion["dead"] = {"pitch_deg": args.dead_pitch_deg}
        if motion:
            doc["motion"] = motion

        fire: dict[str, Any] = {}
        if args.fire_hardware_enabled is not None:
            fire["hardware_enabled"] = args.fire_hardware_enabled
        if args.fire_default_hold_ms is not None:
            fire["default_hold_ms"] = args.fire_default_hold_ms
        if args.fire_esc_run_us is not None:
            fire["esc_run_us"] = args.fire_esc_run_us
        if args.fire_esc_stop_us is not None:
            fire["esc_stop_us"] = args.fire_esc_stop_us
        if args.fire_relay_step_delay_ms is not None:
            fire["relay_step_delay_ms"] = args.fire_relay_step_delay_ms
        if fire:
            doc["fire"] = fire

        network: dict[str, Any] = {}
        if args.network_auto_start is not None:
            network["auto_start"] = args.network_auto_start
        if args.network_start_delay_ms is not None:
            network["start_delay_ms"] = args.network_start_delay_ms
        if network:
            doc["network"] = network

        ota: dict[str, Any] = {}
        if args.ota_command_center_controlled is not None:
            ota["command_center_controlled"] = args.ota_command_center_controlled
        if args.ota_auto_check_enabled is not None:
            ota["auto_check_enabled"] = args.ota_auto_check_enabled
        if args.ota_channel is not None:
            ota["channel"] = args.ota_channel
        if args.ota_desired_build is not None:
            ota["desired_build"] = args.ota_desired_build
        if args.ota_public_manifest_url is not None:
            ota["public_manifest_url"] = args.ota_public_manifest_url
        if args.ota_local_mirror_url is not None:
            ota["local_mirror_url"] = args.ota_local_mirror_url
        if args.ota_check_interval_s is not None:
            ota["check_interval_s"] = args.ota_check_interval_s
        if args.ota_apply_only_in_safe_state is not None:
            ota["apply_only_in_safe_state"] = args.ota_apply_only_in_safe_state
        if ota:
            doc["ota"] = ota

        if len(doc) == 3:
            raise MqttCommandError("config action needs at least one --*-deg/--fire/--idle/--network/--ota option")
        return "config", doc

    raise MqttCommandError(f"unsupported action: {action}")


def mqtt_string(value: str) -> bytes:
    encoded = value.encode("utf-8")
    if len(encoded) > 65535:
        raise MqttCommandError("MQTT string too long")
    return len(encoded).to_bytes(2, "big") + encoded


def encode_remaining_length(value: int) -> bytes:
    if value < 0 or value > 268435455:
        raise MqttCommandError("invalid MQTT remaining length")
    out = bytearray()
    while True:
        encoded_byte = value % 128
        value //= 128
        if value > 0:
            encoded_byte |= 128
        out.append(encoded_byte)
        if value == 0:
            return bytes(out)


def read_packet(sock: socket.socket) -> tuple[int, bytes]:
    header = sock.recv(1)
    if not header:
        raise MqttCommandError("MQTT broker closed connection before CONNACK")
    multiplier = 1
    remaining = 0
    while True:
        chunk = sock.recv(1)
        if not chunk:
            raise MqttCommandError("MQTT broker closed connection while reading packet length")
        digit = chunk[0]
        remaining += (digit & 127) * multiplier
        if (digit & 128) == 0:
            break
        multiplier *= 128
        if multiplier > 128 * 128 * 128:
            raise MqttCommandError("malformed MQTT remaining length")
    payload = bytearray()
    while len(payload) < remaining:
        chunk = sock.recv(remaining - len(payload))
        if not chunk:
            raise MqttCommandError("MQTT broker closed connection while reading packet payload")
        payload.extend(chunk)
    return header[0], bytes(payload)


def publish_mqtt(
    *,
    host: str,
    port: int,
    topic: str,
    payload: str,
    username: str | None = None,
    password: str | None = None,
    timeout_s: float = 5.0,
) -> None:
    client_id = f"bb-turret-cli-{os.getpid()}-{int(time.time())}"
    connect_flags = 0b00000010  # clean session
    variable = mqtt_string("MQTT") + bytes([4, connect_flags, 0, 60])
    if username:
        connect_flags |= 0b10000000
    if password:
        connect_flags |= 0b01000000
    variable = mqtt_string("MQTT") + bytes([4, connect_flags, 0, 60])
    connect_payload = mqtt_string(client_id)
    if username:
        connect_payload += mqtt_string(username)
    if password:
        connect_payload += mqtt_string(password)
    packet = bytes([0x10]) + encode_remaining_length(len(variable) + len(connect_payload)) + variable + connect_payload

    publish_body = mqtt_string(topic) + payload.encode("utf-8")
    publish_packet = bytes([0x30]) + encode_remaining_length(len(publish_body)) + publish_body

    with socket.create_connection((host, port), timeout=timeout_s) as sock:
        sock.settimeout(timeout_s)
        sock.sendall(packet)
        packet_type, body = read_packet(sock)
        if packet_type != 0x20 or len(body) < 2:
            raise MqttCommandError(f"unexpected MQTT CONNACK packet: type=0x{packet_type:02x} body={body!r}")
        if body[1] != 0:
            raise MqttCommandError(f"MQTT connect rejected rc={body[1]}")
        sock.sendall(publish_packet)
        sock.sendall(bytes([0xE0, 0x00]))



def extract_global_options(argv: list[str]) -> tuple[list[str], dict[str, Any]]:
    value_flags = {
        "--env-file": "env_file",
        "--host": "host",
        "--port": "port",
        "--root": "root",
        "--username": "username",
        "--password": "password",
        "--timeout-s": "timeout_s",
    }
    bool_flags = {"--dry-run": "dry_run"}
    cleaned: list[str] = []
    overrides: dict[str, Any] = {}
    i = 0
    while i < len(argv):
        item = argv[i]
        if item in bool_flags:
            overrides[bool_flags[item]] = True
            i += 1
            continue
        matched_equals = False
        for flag, dest in value_flags.items():
            prefix = flag + "="
            if item.startswith(prefix):
                overrides[dest] = item[len(prefix) :]
                matched_equals = True
                break
        if matched_equals:
            i += 1
            continue
        if item in value_flags:
            if i + 1 >= len(argv):
                raise MqttCommandError(f"{item} requires a value")
            overrides[value_flags[item]] = argv[i + 1]
            i += 2
            continue
        cleaned.append(item)
        i += 1
    return cleaned, overrides

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Publish BattleBang turret_fleet MQTT config/command messages without external MQTT tools."
    )
    parser.add_argument("--env-file", default=str(DEFAULT_ENV_FILE), help="dotenv file; default src/turret_fleet/.env.turret_fleet")
    parser.add_argument("--host", help="MQTT broker host; env TURRET_FLEET_MQTT_HOST/TURRET_MQTT_HOST")
    parser.add_argument("--port", type=int, help="MQTT broker port; default/env 1883")
    parser.add_argument("--root", help="MQTT root; default/env battlebang")
    parser.add_argument("--username", help="MQTT username")
    parser.add_argument("--password", help="MQTT password")
    parser.add_argument("--dry-run", action="store_true", help="print topic/payload only")
    parser.add_argument("--timeout-s", type=float, default=5.0, help="MQTT socket timeout")
    parser.add_argument("turret_id", help="turret id, e.g. turret_2 or 2")

    sub = parser.add_subparsers(dest="action", required=True)
    for action in ("idle", "dead", "hold", "wait", "home", "init", "initiate", "recover"):
        p = sub.add_parser(action)
        p.add_argument("--id", dest="command_id")

    target = sub.add_parser("target", help="world-coordinate target; units follow ESP config mqtt_target_unit, normally meters")
    target.add_argument("x", type=float)
    target.add_argument("y", type=float)
    target.add_argument("z", type=float)
    target.add_argument("--frame-id")
    target.add_argument("--id", dest="command_id")

    aim = sub.add_parser("aim", help="direct local yaw/pitch aim for axis debugging")
    aim.add_argument("yaw_deg", type=float)
    aim.add_argument("pitch_deg", type=float)
    aim.add_argument("--frame-id")
    aim.add_argument("--id", dest="command_id")

    jog = sub.add_parser("jog", help="one bounded debug PWM pulse for safe axis direction/range checks")
    jog.add_argument("axis", choices=["yaw", "pitch"])
    jog.add_argument("direction", choices=["plus", "minus", "cw", "ccw", "+", "-"])
    jog.add_argument("--delta-us", type=int, default=20)
    jog.add_argument("--duration-ms", type=int, default=40)
    jog.add_argument("--id", dest="command_id")

    fire = sub.add_parser("fire", help="explicit live fire command; rejected only in DEAD, brownout lockout, or unconfigured state")
    fire.add_argument("--duration-ms", type=int, default=500)
    fire.add_argument("--id", dest="command_id")

    cfg = sub.add_parser("config", help="publish a partial runtime config patch saved to ESP NVS")
    cfg.add_argument("--config-version", type=int)
    cfg.add_argument("--yaw-offset-deg", type=float, help="world target-solver yaw correction")
    cfg.add_argument("--pitch-offset-deg", type=float, help="world target-solver pitch correction")
    cfg.add_argument("--yaw-axis-offset-deg", type=float, help="local yaw sensor zero correction for aim/motion")
    cfg.add_argument("--pitch-axis-offset-deg", type=float, help="local pitch sensor zero correction for aim/motion")
    cfg.add_argument("--dead-pitch-deg", type=float)
    cfg.add_argument("--yaw-stop-us", type=int, help="continuous-servo neutral/stop pulse for yaw, persisted in NVS")
    cfg.add_argument("--pitch-stop-us", type=int, help="continuous-servo neutral/stop pulse for pitch, persisted in NVS")
    cfg.add_argument("--servo-max-delta-us", type=int, help="legacy max PWM delta from stop for both axes")
    cfg.add_argument("--yaw-max-delta-us", type=int, help="max yaw PWM delta from stop for closed-loop drive")
    cfg.add_argument("--pitch-max-delta-us", type=int, help="max pitch PWM delta from stop for closed-loop drive")
    cfg.add_argument("--yaw-min-drive-us", type=int, help="minimum yaw PWM delta once outside deadband")
    cfg.add_argument("--pitch-min-drive-us", type=int, help="minimum pitch PWM delta once outside deadband")
    cfg.add_argument("--servo-attach-settle-ms", type=int, help="hold neutral PWM after attaching an axis before driving")
    cfg.add_argument("--axis-switch-cooldown-ms", type=int, help="dead time after detaching one axis before attaching the other")
    cfg.add_argument("--axis-divergence-guard-ms", type=int, help="runaway guard window; 0 disables")
    cfg.add_argument("--axis-divergence-margin-deg", type=float, help="allowed goal-error increase before runaway guard trips")
    cfg.add_argument("--yaw-min-deg", type=float, help="minimum local yaw command; default production envelope is -75")
    cfg.add_argument("--yaw-max-deg", type=float, help="maximum local yaw command; default production envelope is +75")
    cfg.add_argument("--pitch-min-deg", type=float, help="minimum local pitch command; default production envelope is -75")
    cfg.add_argument("--pitch-max-deg", type=float, help="maximum local pitch command; default production envelope is +75")
    cfg.add_argument("--home-yaw-deg", type=float, help="boot local home yaw; default 0")
    cfg.add_argument("--home-pitch-deg", type=float, help="boot local home pitch; default 0")
    cfg.add_argument("--idle-yaw-min-deg", type=float)
    cfg.add_argument("--idle-yaw-max-deg", type=float)
    cfg.add_argument("--idle-yaw-speed-deg-s", type=float)
    cfg.add_argument("--idle-pitch-min-deg", type=float)
    cfg.add_argument("--idle-pitch-max-deg", type=float)
    cfg.add_argument("--idle-pitch-speed-deg-s", type=float)
    cfg.add_argument("--fire-hardware-enabled", type=parse_bool_text)
    cfg.add_argument("--fire-default-hold-ms", type=int)
    cfg.add_argument("--fire-esc-run-us", type=int)
    cfg.add_argument("--fire-esc-stop-us", type=int)
    cfg.add_argument("--fire-relay-step-delay-ms", type=int)
    cfg.add_argument("--network-auto-start", type=parse_bool_text)
    cfg.add_argument("--network-start-delay-ms", type=int)
    cfg.add_argument("--ota-command-center-controlled", type=parse_bool_text)
    cfg.add_argument("--ota-auto-check-enabled", type=parse_bool_text)
    cfg.add_argument("--ota-channel")
    cfg.add_argument("--ota-desired-build", type=int)
    cfg.add_argument("--ota-public-manifest-url")
    cfg.add_argument("--ota-local-mirror-url")
    cfg.add_argument("--ota-check-interval-s", type=int)
    cfg.add_argument("--ota-apply-only-in-safe-state", type=parse_bool_text)
    return parser


def main(argv: list[str] | None = None) -> int:
    if argv is None:
        argv = sys.argv[1:]
    # Let operators place global options either before or after the subcommand.
    argv, overrides = extract_global_options(argv)

    parser = build_parser()
    args = parser.parse_args(argv)
    for key, value in overrides.items():
        setattr(args, key, value)
    args.dry_run = bool(getattr(args, "dry_run", False))

    env = merged_env(Path(args.env_file))
    turret_id = normalize_turret_id(args.turret_id)
    root = args.root or env_first(env, "TURRET_FLEET_MQTT_ROOT", "TURRET_MQTT_ROOT", default="battlebang") or "battlebang"

    suffix, payload_doc = build_command_payload(args)
    topic = topic_for(root, turret_id, suffix)
    payload = json.dumps(payload_doc, separators=(",", ":"), ensure_ascii=False)

    print(f"topic={topic}")
    print(f"payload={payload}")
    if args.dry_run:
        return 0

    host = args.host or env_first(env, "TURRET_FLEET_MQTT_HOST", "TURRET_MQTT_HOST")
    if not host:
        parser.error("missing --host or TURRET_FLEET_MQTT_HOST/TURRET_MQTT_HOST")
    port = int(args.port or env_first(env, "TURRET_FLEET_MQTT_PORT", "TURRET_MQTT_PORT", default="1883") or "1883")
    username = args.username or env_first(env, "TURRET_FLEET_MQTT_USERNAME", "TURRET_MQTT_USERNAME")
    password = args.password or env_first(env, "TURRET_FLEET_MQTT_PASSWORD", "TURRET_MQTT_PASSWORD")

    publish_mqtt(
        host=host,
        port=port,
        topic=topic,
        payload=payload,
        username=username,
        password=password,
        timeout_s=float(args.timeout_s),
    )
    print(f"published to {host}:{port}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except MqttCommandError as exc:
        print(f"[mqtt-command] {exc}", file=sys.stderr)
        raise SystemExit(2)
