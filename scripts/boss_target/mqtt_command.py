#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import socket
import time
from pathlib import Path
from typing import Any

PROJECT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_ENV_FILE = PROJECT_ROOT / "src" / "boss_target" / ".env.boss_target"


class BossTargetMqttError(RuntimeError):
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
        if len(value) >= 2 and value[0] == value[-1] and value[0] in {"'", '"'}:
            value = value[1:-1]
        if key:
            values[key] = value
    return values


def merged_env(env_file: Path) -> dict[str, str]:
    values = parse_dotenv(env_file)
    for key, value in os.environ.items():
        if key.startswith("BOSS_TARGET_") or key.startswith("BATTLEBANG_BOSS_TARGET_"):
            values[key] = value
    return values


def env_first(env: dict[str, str], *keys: str, default: str | None = None) -> str | None:
    for key in keys:
        value = env.get(key)
        if value is not None and value != "":
            return value
    return default


def env_int(env: dict[str, str], key: str, default: int) -> int:
    value = env.get(key)
    if value is None or value == "":
        return default
    return int(value, 0)


def mqtt_string(value: str) -> bytes:
    data = value.encode("utf-8")
    if len(data) > 65535:
        raise BossTargetMqttError("MQTT string too long")
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
        raise BossTargetMqttError("MQTT broker closed connection before packet")
    multiplier = 1
    remaining = 0
    while True:
        raw = sock.recv(1)
        if not raw:
            raise BossTargetMqttError("MQTT broker closed connection while reading packet length")
        encoded = raw[0]
        remaining += (encoded & 127) * multiplier
        if (encoded & 128) == 0:
            break
        multiplier *= 128
        if multiplier > 128 * 128 * 128:
            raise BossTargetMqttError("malformed MQTT remaining length")
    payload = b""
    while len(payload) < remaining:
        chunk = sock.recv(remaining - len(payload))
        if not chunk:
            raise BossTargetMqttError("MQTT broker closed connection while reading packet payload")
        payload += chunk
    return first[0], payload


def publish_mqtt(
    host: str,
    port: int,
    topic: str,
    payload: str,
    username: str | None = None,
    password: str | None = None,
    timeout_s: float = 5.0,
    retain: bool = False,
) -> None:
    client_id = f"bb-boss-cli-{int(time.time())}"
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
            raise BossTargetMqttError(f"MQTT connect rejected packet=0x{packet_type:02x} body={body!r}")
        sock.sendall(publish_packet)
        sock.sendall(disconnect_packet)


def clean_root(root: str) -> str:
    return root.strip("/") or "battlebang"


def topic_for(root: str, identifier: str, suffix: str) -> str:
    identifier = identifier.strip()
    if not identifier:
        raise BossTargetMqttError("missing boss id; use --boss-id or BOSS_TARGET_BOSS_ID")
    return f"{clean_root(root)}/boss_targets/{identifier}/{suffix}"


def all_ota_topic(root: str) -> str:
    return f"{clean_root(root)}/boss_targets/all/ota"


def load_json_arg(payload: str | None, json_file: Path | None) -> dict[str, Any]:
    if payload and json_file:
        raise BossTargetMqttError("use only one of --payload or --json-file")
    if json_file:
        return json.loads(json_file.read_text(encoding="utf-8"))
    if payload:
        return json.loads(payload)
    return {}


def command_payload(args: argparse.Namespace) -> dict[str, Any]:
    if args.action == "simulate-hit":
        doc: dict[str, Any] = {"command": "simulate_hit"}
        if args.target_index is not None:
            doc["target_index"] = args.target_index
        return doc
    return {"command": args.action.replace("-", "_")}


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Publish boss_target MQTT commands/config/OTA payloads without external MQTT deps.")
    parser.add_argument("action", choices=("start", "reset", "status", "simulate-hit", "config", "ota", "all-ota"))
    parser.add_argument("--env-file", type=Path, default=DEFAULT_ENV_FILE)
    parser.add_argument("--host", help="MQTT broker host; default/env BOSS_TARGET_MQTT_HOST")
    parser.add_argument("--port", type=int, help="MQTT broker port; default/env BOSS_TARGET_MQTT_PORT or 1883")
    parser.add_argument("--root", help="MQTT root; default/env BOSS_TARGET_MQTT_ROOT or battlebang")
    parser.add_argument("--username", help="MQTT username; default/env BOSS_TARGET_MQTT_USERNAME")
    parser.add_argument("--password", help="MQTT password; default/env BOSS_TARGET_MQTT_PASSWORD")
    parser.add_argument("--boss-id", help="boss target id; default/env BOSS_TARGET_BOSS_ID or BOSS_TARGET_TARGET_ID")
    parser.add_argument("--target-index", type=int, help="simulate-hit target index; omit to hit active target")
    parser.add_argument("--payload", help="raw JSON payload for config/ota actions")
    parser.add_argument("--json-file", type=Path, help="JSON payload file for config/ota actions")
    parser.add_argument("--retain", action="store_true")
    parser.add_argument("--dry-run", action="store_true", help="print topic and payload without publishing")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = make_parser().parse_args(argv)
    env = merged_env(args.env_file)
    host = args.host or env_first(env, "BOSS_TARGET_MQTT_HOST", default="")
    if not host and not args.dry_run:
        raise BossTargetMqttError("missing MQTT host; use --host or BOSS_TARGET_MQTT_HOST")
    port = args.port or env_int(env, "BOSS_TARGET_MQTT_PORT", 1883)
    root = args.root or env_first(env, "BOSS_TARGET_MQTT_ROOT", default="battlebang") or "battlebang"
    username = args.username if args.username is not None else env_first(env, "BOSS_TARGET_MQTT_USERNAME", default="")
    password = args.password if args.password is not None else env_first(env, "BOSS_TARGET_MQTT_PASSWORD", default="")
    boss_id = args.boss_id or env_first(env, "BOSS_TARGET_BOSS_ID", "BOSS_TARGET_TARGET_ID", default="") or ""

    if args.action in {"config", "ota", "all-ota"}:
        doc = load_json_arg(args.payload, args.json_file)
        if not doc:
            raise BossTargetMqttError(f"{args.action} requires --payload or --json-file")
        suffix = "ota" if args.action in {"ota", "all-ota"} else "config"
        topic = all_ota_topic(root) if args.action == "all-ota" else topic_for(root, boss_id, suffix)
    else:
        doc = command_payload(args)
        topic = topic_for(root, boss_id, "command")

    payload = json.dumps(doc, ensure_ascii=False, separators=(",", ":"))
    if args.dry_run:
        print(topic)
        print(payload)
        return 0
    publish_mqtt(host, port, topic, payload, username=username or None, password=password or None, retain=args.retain)
    print(f"published {len(payload)} bytes to {topic}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except BossTargetMqttError as exc:
        print(f"error: {exc}")
        raise SystemExit(2)
