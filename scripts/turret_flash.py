#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
import tomllib
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

PROJECT_ROOT = Path(__file__).resolve().parents[1]
TURRET_CONFIG_PATH = PROJECT_ROOT / "src" / "turret" / "turrets.json"
LOCAL_SECRETS_PATH = PROJECT_ROOT / "src" / "turret" / "local_secrets.h"
DEFAULT_PORT_MAP_PATH = PROJECT_ROOT / "src" / "turret" / "upload_targets.toml"
DEFAULT_PIO_PATH = PROJECT_ROOT / ".venv-pio" / "bin" / "pio"
PLACEHOLDER_PREFIXES = ("YOUR_", "192.168.")
REQUIRED_SECRET_KEYS = (
    "TURRET_WIFI_SSID",
    "TURRET_WIFI_PASSWORD",
    "TURRET_MQTT_HOST",
)
OPTIONAL_SECRET_KEYS = (
    "TURRET_MQTT_PORT",
    "TURRET_MQTT_USERNAME",
    "TURRET_MQTT_PASSWORD",
)
DEFINE_RE = re.compile(r'^\s*#define\s+(?P<name>[A-Z0-9_]+)\s+(?P<value>.+?)\s*$')


@dataclass(frozen=True)
class TurretConfig:
    turret_id: str
    configured: bool
    x_cm: float | None = None
    y_cm: float | None = None
    z_cm: float | None = None
    default_target_z_cm: float | None = None
    pitch_offset_deg: float | None = None
    yaw_offset_deg: float | None = None

    @property
    def platformio_env(self) -> str:
        return f"esp32dev_{self.turret_id}"

    def summary(self) -> str:
        if not self.configured:
            return f"{self.turret_id}: configured=false"
        parts = [
            f"x={self.x_cm}cm",
            f"y={self.y_cm}cm",
            f"z={self.z_cm}cm",
            f"default_target_z={self.default_target_z_cm}cm",
        ]
        if self.pitch_offset_deg is not None:
            parts.append(f"pitch_offset={self.pitch_offset_deg}deg")
        if self.yaw_offset_deg is not None:
            parts.append(f"yaw_offset={self.yaw_offset_deg}deg")
        return f"{self.turret_id}: " + ", ".join(parts)


@dataclass(frozen=True)
class FlashTarget:
    turret_id: str
    port: str | None


class FlashConfigError(RuntimeError):
    pass


def resolve_pio_binary() -> str:
    if DEFAULT_PIO_PATH.exists():
        return str(DEFAULT_PIO_PATH)
    return "pio"


def load_turret_configs() -> dict[str, TurretConfig]:
    data = json.loads(TURRET_CONFIG_PATH.read_text(encoding="utf-8"))
    raw_turrets = data.get("turrets", {})
    result: dict[str, TurretConfig] = {}
    for turret_id, entry in raw_turrets.items():
        configured = bool(entry.get("configured", False))
        result[turret_id] = TurretConfig(
            turret_id=turret_id,
            configured=configured,
            x_cm=float(entry["x_cm"]) if "x_cm" in entry else None,
            y_cm=float(entry["y_cm"]) if "y_cm" in entry else None,
            z_cm=float(entry["z_cm"]) if "z_cm" in entry else None,
            default_target_z_cm=float(entry["default_target_z_cm"]) if "default_target_z_cm" in entry else None,
            pitch_offset_deg=float(entry["pitch_offset_deg"]) if "pitch_offset_deg" in entry else None,
            yaw_offset_deg=float(entry["yaw_offset_deg"]) if "yaw_offset_deg" in entry else None,
        )
    return result


def strip_quotes(value: str) -> str:
    value = value.strip()
    if len(value) >= 2 and value[0] == '"' and value[-1] == '"':
        return bytes(value[1:-1], "utf-8").decode("unicode_escape")
    return value


def load_local_secrets() -> dict[str, str]:
    if not LOCAL_SECRETS_PATH.exists():
        return {}
    secrets: dict[str, str] = {}
    for line in LOCAL_SECRETS_PATH.read_text(encoding="utf-8").splitlines():
        match = DEFINE_RE.match(line)
        if not match:
            continue
        secrets[match.group("name")] = strip_quotes(match.group("value"))
    return secrets


def has_real_secret_value(value: str | None) -> bool:
    if value is None:
        return False
    text = value.strip()
    if not text:
        return False
    return not any(text.startswith(prefix) for prefix in PLACEHOLDER_PREFIXES)


def build_secret_env(args: argparse.Namespace, local_secrets: dict[str, str]) -> tuple[dict[str, str], str]:
    env_overrides: dict[str, str] = {}
    source_parts: list[str] = []

    explicit = {
        "TURRET_WIFI_SSID": args.wifi_ssid,
        "TURRET_WIFI_PASSWORD": args.wifi_password,
        "TURRET_MQTT_HOST": args.mqtt_host,
        "TURRET_MQTT_PORT": str(args.mqtt_port) if args.mqtt_port is not None else None,
        "TURRET_MQTT_USERNAME": args.mqtt_username,
        "TURRET_MQTT_PASSWORD": args.mqtt_password,
    }

    for key, value in explicit.items():
        if value is not None:
            env_overrides[key] = value
            source_parts.append(f"{key}=cli")

    for key in REQUIRED_SECRET_KEYS:
        if key in env_overrides:
            continue
        local_value = local_secrets.get(key)
        if has_real_secret_value(local_value):
            env_overrides[key] = local_value
            source_parts.append(f"{key}=local_secrets.h")
        else:
            raise FlashConfigError(
                f"Missing required secret {key}. Set it in src/turret/local_secrets.h or pass --{key.lower().replace('turret_', '').replace('_', '-')}"
            )

    for key in OPTIONAL_SECRET_KEYS:
        if key in env_overrides:
            continue
        local_value = local_secrets.get(key)
        if has_real_secret_value(local_value):
            env_overrides[key] = local_value
            source_parts.append(f"{key}=local_secrets.h")

    if "TURRET_MQTT_PORT" not in env_overrides:
        env_overrides["TURRET_MQTT_PORT"] = "1883"
        source_parts.append("TURRET_MQTT_PORT=default(1883)")

    return env_overrides, ", ".join(source_parts)


def load_port_map(path: Path) -> list[FlashTarget]:
    if not path.exists():
        raise FlashConfigError(f"Port map file not found: {path}")
    with path.open("rb") as f:
        raw = tomllib.load(f)
    turrets = raw.get("turrets")
    if not isinstance(turrets, dict) or not turrets:
        raise FlashConfigError(f"{path} must define a non-empty [turrets] table")
    targets: list[FlashTarget] = []
    for turret_id, port in turrets.items():
        if not isinstance(port, str) or not port.strip():
            raise FlashConfigError(f"Invalid port for {turret_id!r} in {path}")
        targets.append(FlashTarget(turret_id=turret_id.strip(), port=port.strip()))
    return targets


def parse_targets(values: Iterable[str], build_only: bool) -> list[FlashTarget]:
    result: list[FlashTarget] = []
    for raw in values:
        item = raw.strip()
        if not item:
            continue
        turret_id, sep, port = item.partition("=")
        turret_id = turret_id.strip()
        port = port.strip() or None
        if not turret_id:
            raise FlashConfigError(f"Invalid --target value: {raw!r}")
        if not build_only and not sep:
            raise FlashConfigError(f"Upload target must include a port: {raw!r} (expected turret_5=/dev/cu.usbserial-1120)")
        result.append(FlashTarget(turret_id=turret_id, port=port))
    return result


def list_ports(pio_bin: str) -> list[dict[str, str]]:
    completed = subprocess.run(
        [pio_bin, "device", "list", "--json-output"],
        cwd=PROJECT_ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    data = json.loads(completed.stdout or "[]")
    ports: list[dict[str, str]] = []
    for item in data:
        if not isinstance(item, dict):
            continue
        port = str(item.get("port", "")).strip()
        if not port:
            continue
        ports.append(
            {
                "port": port,
                "description": str(item.get("description", "")).strip() or "n/a",
                "hwid": str(item.get("hwid", "")).strip() or "n/a",
            }
        )
    return ports


def ensure_targets_valid(
    targets: list[FlashTarget],
    turret_configs: dict[str, TurretConfig],
    detected_ports: set[str],
    build_only: bool,
    skip_port_check: bool,
) -> None:
    if not targets:
        raise FlashConfigError("No targets selected. Use --target turret_5=/dev/... or --map-file src/turret/upload_targets.toml")

    seen: set[str] = set()
    for target in targets:
        if target.turret_id in seen:
            raise FlashConfigError(f"Duplicate turret target: {target.turret_id}")
        seen.add(target.turret_id)

        turret = turret_configs.get(target.turret_id)
        if turret is None:
            raise FlashConfigError(f"Unknown turret id: {target.turret_id}")
        if not turret.configured:
            raise FlashConfigError(
                f"{target.turret_id} is not configured in {TURRET_CONFIG_PATH}. Fill x_cm/y_cm/z_cm/default_target_z_cm and set configured=true first."
            )
        for field_name in ("x_cm", "y_cm", "z_cm", "default_target_z_cm"):
            if getattr(turret, field_name) is None:
                raise FlashConfigError(f"{target.turret_id} missing {field_name} in {TURRET_CONFIG_PATH}")

        if build_only:
            continue
        if not target.port:
            raise FlashConfigError(f"{target.turret_id} missing upload port")
        if not skip_port_check and target.port not in detected_ports:
            raise FlashConfigError(
                f"Port {target.port} for {target.turret_id} is not currently detected. Run list-ports or use --skip-port-check if you intentionally want to bypass the check."
            )


def mask_secret(value: str | None) -> str:
    if value is None:
        return ""
    if len(value) <= 2:
        return "*" * len(value)
    return value[0] + "*" * (len(value) - 2) + value[-1]


def run_flash(
    pio_bin: str,
    targets: list[FlashTarget],
    turret_configs: dict[str, TurretConfig],
    env_overrides: dict[str, str],
    build_only: bool,
    dry_run: bool,
) -> None:
    process_env = os.environ.copy()
    process_env.update(env_overrides)

    print("[turret_flash] using PlatformIO:", pio_bin, flush=True)
    print(
        "[turret_flash] network config:",
        f"ssid={env_overrides.get('TURRET_WIFI_SSID')}",
        f"mqtt_host={env_overrides.get('TURRET_MQTT_HOST')}",
        f"mqtt_port={env_overrides.get('TURRET_MQTT_PORT')}",
        f"mqtt_username={env_overrides.get('TURRET_MQTT_USERNAME', '')}",
        f"mqtt_password={mask_secret(env_overrides.get('TURRET_MQTT_PASSWORD', ''))}",
        flush=True,
    )

    for target in targets:
        turret = turret_configs[target.turret_id]
        command = [pio_bin, "run", "-e", turret.platformio_env]
        if not build_only:
            command.extend(["-t", "upload", "--upload-port", target.port or ""])

        print("\n[turret_flash] target:", turret.summary(), flush=True)
        if target.port:
            print(f"[turret_flash] port: {target.port}", flush=True)
        print("[turret_flash] command:", " ".join(shlex.quote(part) for part in command), flush=True)

        if dry_run:
            continue

        subprocess.run(command, cwd=PROJECT_ROOT, env=process_env, check=True)


def print_config(turret_configs: dict[str, TurretConfig], local_secrets: dict[str, str]) -> None:
    print(f"turret config file: {TURRET_CONFIG_PATH}")
    print(f"local secrets file: {LOCAL_SECRETS_PATH} ({'present' if LOCAL_SECRETS_PATH.exists() else 'missing'})")
    print()
    for turret_id in sorted(turret_configs):
        print(turret_configs[turret_id].summary())
    print()
    for key in REQUIRED_SECRET_KEYS + OPTIONAL_SECRET_KEYS:
        value = local_secrets.get(key)
        if key.endswith("PASSWORD"):
            value = mask_secret(value)
        print(f"{key}={value!r}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Build/upload BattleBang turret firmware by turret ID and USB port.")
    parser.add_argument("--pio", default=resolve_pio_binary(), help="Path to the PlatformIO binary (default: ./.venv-pio/bin/pio or pio)")

    subparsers = parser.add_subparsers(dest="command", required=True)

    subparsers.add_parser("list-ports", help="List currently detected serial ports via PlatformIO")
    subparsers.add_parser("show-config", help="Print turret coordinate config and local secrets presence")

    flash = subparsers.add_parser("flash", help="Build or build+upload one or more turrets")
    flash.add_argument("--target", action="append", default=[], help="turret target, e.g. turret_5=/dev/cu.usbserial-1120. For --build-only, port may be omitted: turret_5")
    flash.add_argument("--map-file", type=Path, default=None, help="TOML file with [turrets] turret_id='/dev/...' mappings")
    flash.add_argument("--build-only", action="store_true", help="Build only; do not upload")
    flash.add_argument("--dry-run", action="store_true", help="Validate inputs and print the exact pio commands without running them")
    flash.add_argument("--skip-port-check", action="store_true", help="Skip detected-port validation")
    flash.add_argument("--wifi-ssid")
    flash.add_argument("--wifi-password")
    flash.add_argument("--mqtt-host")
    flash.add_argument("--mqtt-port", type=int)
    flash.add_argument("--mqtt-username")
    flash.add_argument("--mqtt-password")

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    turret_configs = load_turret_configs()
    local_secrets = load_local_secrets()

    if args.command == "show-config":
        print_config(turret_configs, local_secrets)
        return 0

    if args.command == "list-ports":
        ports = list_ports(args.pio)
        if not ports:
            print("No serial ports detected by PlatformIO.")
            return 0
        for item in ports:
            print(f"{item['port']}\t{item['description']}\t{item['hwid']}", flush=True)
        return 0

    try:
        targets: list[FlashTarget] = []
        targets.extend(parse_targets(args.target, build_only=args.build_only))
        if args.map_file is not None:
            targets.extend(load_port_map(args.map_file))
        elif not args.target and DEFAULT_PORT_MAP_PATH.exists():
            targets.extend(load_port_map(DEFAULT_PORT_MAP_PATH))

        ports = [] if args.skip_port_check else list_ports(args.pio)
        ensure_targets_valid(
            targets=targets,
            turret_configs=turret_configs,
            detected_ports={item["port"] for item in ports},
            build_only=args.build_only,
            skip_port_check=args.skip_port_check,
        )
        env_overrides, source_summary = build_secret_env(args, local_secrets)
    except FlashConfigError as exc:
        print(f"[turret_flash] error: {exc}", file=sys.stderr)
        return 2
    except subprocess.CalledProcessError as exc:
        print(f"[turret_flash] failed to inspect ports: {exc}", file=sys.stderr)
        return exc.returncode or 1

    print(f"[turret_flash] secrets source: {source_summary}", flush=True)
    if not args.skip_port_check and ports:
        print("[turret_flash] detected ports:", flush=True)
        for item in ports:
            print(f"  - {item['port']} ({item['description']})", flush=True)

    try:
        run_flash(
            pio_bin=args.pio,
            targets=targets,
            turret_configs=turret_configs,
            env_overrides=env_overrides,
            build_only=args.build_only,
            dry_run=args.dry_run,
        )
    except subprocess.CalledProcessError as exc:
        print(f"[turret_flash] command failed with exit code {exc.returncode}", file=sys.stderr)
        return exc.returncode or 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
