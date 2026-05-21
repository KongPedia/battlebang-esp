#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tomllib
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

PROJECT_ROOT = Path(__file__).resolve().parents[1]
GO2_CONFIG_PATH = PROJECT_ROOT / "src" / "go2" / "robots.json"
LOCAL_SECRETS_PATH = PROJECT_ROOT / "src" / "go2" / "local_secrets.h"
LEGACY_LOCAL_SECRETS_PATH = PROJECT_ROOT / "src" / "local_secrets.h"
DEFAULT_PORT_MAP_PATH = PROJECT_ROOT / "src" / "go2" / "upload_targets.toml"
DEFAULT_PIO_PATH = PROJECT_ROOT / ".venv-pio" / "bin" / "pio"
PLACEHOLDER_PREFIXES = ("YOUR_", "COMMAND_CENTER_OR_BROKER_HOST", "192.168.")
REQUIRED_SECRET_KEYS = (
    "ESP_WIFI_SSID",
    "ESP_WIFI_PASSWORD",
    "ESP_MQTT_HOST",
)
OPTIONAL_SECRET_KEYS = (
    "ESP_MQTT_PORT",
    "ESP_MQTT_TOPIC_PREFIX",
)
LEGACY_SECRET_ALIASES = {
    "ESP_WIFI_SSID": ("BATTLEBANG_WIFI_SSID",),
    "ESP_WIFI_PASSWORD": ("BATTLEBANG_WIFI_PASSWORD",),
    "ESP_MQTT_HOST": ("BATTLEBANG_MQTT_HOST",),
    "ESP_MQTT_PORT": ("BATTLEBANG_MQTT_PORT",),
    "ESP_MQTT_TOPIC_PREFIX": ("BATTLEBANG_MQTT_TOPIC_PREFIX",),
}
DEFINE_RE = re.compile(r"^\s*#define\s+(?P<name>[A-Z0-9_]+)\s+(?P<value>.+?)\s*$")


@dataclass(frozen=True)
class Go2Config:
    robot_id: str
    configured: bool
    profile: dict

    @property
    def platformio_env(self) -> str:
        return f"esp32dev_{self.robot_id}"

    def summary(self) -> str:
        if not self.configured:
            return f"{self.robot_id}: configured=false"
        fields = [
            f"hp_max={self.profile.get('hp_max', 'default')}",
            f"hit_threshold={self.profile.get('hit_threshold', 'default')}",
            f"piezo_damage_divisor={self.profile.get('piezo_damage_divisor', 'default')}",
            f"topic_prefix={self.profile.get('mqtt_topic_prefix', 'default')}",
        ]
        return f"{self.robot_id}: " + ", ".join(fields)


@dataclass(frozen=True)
class FlashTarget:
    robot_id: str
    port: str | None


class FlashConfigError(RuntimeError):
    pass


def resolve_pio_binary() -> str:
    if DEFAULT_PIO_PATH.exists():
        return str(DEFAULT_PIO_PATH)
    for executable in ("pio", "platformio"):
        resolved = shutil.which(executable)
        if resolved:
            return resolved
    if shutil.which("uvx"):
        return "uvx platformio"
    return "pio"


def pio_command(pio_bin: str, *args: str) -> list[str]:
    return [*shlex.split(pio_bin), *args]


def deep_merge(base: dict, override: dict) -> dict:
    merged = dict(base)
    for key, value in override.items():
        if isinstance(value, dict) and isinstance(merged.get(key), dict):
            merged[key] = deep_merge(merged[key], value)
        else:
            merged[key] = value
    return merged


def load_go2_configs() -> dict[str, Go2Config]:
    data = json.loads(GO2_CONFIG_PATH.read_text(encoding="utf-8"))
    defaults = data.get("defaults", {})
    raw_robots = data.get("robots", {})
    result: dict[str, Go2Config] = {}
    for robot_id, entry in raw_robots.items():
        profile = deep_merge(defaults, entry if isinstance(entry, dict) else {})
        result[robot_id] = Go2Config(
            robot_id=robot_id,
            configured=bool(profile.get("configured", False)),
            profile=profile,
        )
    return result


def strip_quotes(value: str) -> str:
    value = value.strip()
    if len(value) >= 2 and value[0] == '"' and value[-1] == '"':
        return bytes(value[1:-1], "utf-8").decode("unicode_escape")
    return value


def load_local_secrets() -> tuple[Path | None, dict[str, str]]:
    path = LOCAL_SECRETS_PATH if LOCAL_SECRETS_PATH.exists() else None
    if path is None and LEGACY_LOCAL_SECRETS_PATH.exists():
        path = LEGACY_LOCAL_SECRETS_PATH
    if path is None:
        return None, {}

    secrets: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        match = DEFINE_RE.match(line)
        if not match:
            continue
        secrets[match.group("name")] = strip_quotes(match.group("value"))
    return path, secrets


def has_real_secret_value(value: str | None) -> bool:
    if value is None:
        return False
    text = value.strip()
    if not text:
        return False
    return not any(text.startswith(prefix) for prefix in PLACEHOLDER_PREFIXES)


def get_secret_value(
    local_secrets: dict[str, str], key: str
) -> tuple[str | None, str | None]:
    value = local_secrets.get(key)
    if value is not None:
        return value, key

    for alias in LEGACY_SECRET_ALIASES.get(key, ()):
        value = local_secrets.get(alias)
        if value is not None:
            return value, alias

    return None, None


def build_secret_env(
    args: argparse.Namespace, local_secrets: dict[str, str]
) -> tuple[dict[str, str], str]:
    env_overrides: dict[str, str] = {}
    source_parts: list[str] = []

    explicit = {
        "ESP_WIFI_SSID": args.wifi_ssid,
        "ESP_WIFI_PASSWORD": args.wifi_password,
        "ESP_MQTT_HOST": args.mqtt_host,
        "ESP_MQTT_PORT": str(args.mqtt_port) if args.mqtt_port is not None else None,
        "ESP_MQTT_TOPIC_PREFIX": args.mqtt_topic_prefix,
    }

    for key, value in explicit.items():
        if value is not None:
            env_overrides[key] = value
            source_parts.append(f"{key}=cli")

    for key in REQUIRED_SECRET_KEYS:
        if key in env_overrides:
            continue
        local_value, source_key = get_secret_value(local_secrets, key)
        if has_real_secret_value(local_value):
            env_overrides[key] = local_value or ""
            source_parts.append(
                f"{key}=local_secrets.h"
                if source_key == key
                else f"{key}=local_secrets.h({source_key})"
            )
        else:
            raise FlashConfigError(
                f"Missing required secret {key}. Set it in src/go2/local_secrets.h or pass --{key.lower().replace('esp_', '').replace('_', '-')}"
            )

    for key in OPTIONAL_SECRET_KEYS:
        if key in env_overrides:
            continue
        local_value, source_key = get_secret_value(local_secrets, key)
        if has_real_secret_value(local_value):
            env_overrides[key] = local_value or ""
            source_parts.append(
                f"{key}=local_secrets.h"
                if source_key == key
                else f"{key}=local_secrets.h({source_key})"
            )

    if "ESP_MQTT_PORT" not in env_overrides:
        env_overrides["ESP_MQTT_PORT"] = "1883"
        source_parts.append("ESP_MQTT_PORT=default(1883)")

    return env_overrides, ", ".join(source_parts)


def load_port_map(path: Path) -> list[FlashTarget]:
    if not path.exists():
        raise FlashConfigError(f"Port map file not found: {path}")
    with path.open("rb") as f:
        raw = tomllib.load(f)
    robots = raw.get("robots")
    if not isinstance(robots, dict) or not robots:
        raise FlashConfigError(f"{path} must define a non-empty [robots] table")
    targets: list[FlashTarget] = []
    for robot_id, port in robots.items():
        if not isinstance(port, str) or not port.strip():
            raise FlashConfigError(f"Invalid port for {robot_id!r} in {path}")
        targets.append(FlashTarget(robot_id=robot_id.strip(), port=port.strip()))
    return targets


def parse_targets(values: Iterable[str], build_only: bool) -> list[FlashTarget]:
    result: list[FlashTarget] = []
    for raw in values:
        item = raw.strip()
        if not item:
            continue
        robot_id, sep, port = item.partition("=")
        robot_id = robot_id.strip()
        port = port.strip() or None
        if not robot_id:
            raise FlashConfigError(f"Invalid --target value: {raw!r}")
        if not build_only and not sep:
            raise FlashConfigError(
                f"Upload target must include a port: {raw!r} (expected go2_05=/dev/cu.usbserial-21130)"
            )
        result.append(FlashTarget(robot_id=robot_id, port=port))
    return result


def list_ports(pio_bin: str) -> list[dict[str, str]]:
    completed = subprocess.run(
        pio_command(pio_bin, "device", "list", "--json-output"),
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
    go2_configs: dict[str, Go2Config],
    detected_ports: set[str],
    build_only: bool,
    skip_port_check: bool,
) -> None:
    if not targets:
        raise FlashConfigError(
            "No targets selected. Use --target go2_05=/dev/... or --map-file src/go2/upload_targets.toml"
        )

    seen: set[str] = set()
    for target in targets:
        if target.robot_id in seen:
            raise FlashConfigError(f"Duplicate Go2 target: {target.robot_id}")
        seen.add(target.robot_id)

        robot = go2_configs.get(target.robot_id)
        if robot is None:
            raise FlashConfigError(f"Unknown robot id: {target.robot_id}")
        if not robot.configured:
            raise FlashConfigError(
                f"{target.robot_id} is not configured in {GO2_CONFIG_PATH}"
            )

        if build_only:
            continue
        if not target.port:
            raise FlashConfigError(f"{target.robot_id} missing upload port")
        if not skip_port_check and target.port not in detected_ports:
            raise FlashConfigError(
                f"Port {target.port} for {target.robot_id} is not currently detected. Run list-ports or use --skip-port-check if intentional."
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
    go2_configs: dict[str, Go2Config],
    env_overrides: dict[str, str],
    build_only: bool,
    dry_run: bool,
) -> None:
    process_env = os.environ.copy()
    process_env.update(env_overrides)

    print("[go2_flash] using PlatformIO:", pio_bin, flush=True)
    print(
        "[go2_flash] network config:",
        f"ssid={env_overrides.get('ESP_WIFI_SSID')}",
        f"mqtt_host={env_overrides.get('ESP_MQTT_HOST')}",
        f"mqtt_port={env_overrides.get('ESP_MQTT_PORT')}",
        f"topic_prefix={env_overrides.get('ESP_MQTT_TOPIC_PREFIX', 'profile/default')}",
        f"wifi_password={mask_secret(env_overrides.get('ESP_WIFI_PASSWORD', ''))}",
        flush=True,
    )

    for target in targets:
        robot = go2_configs[target.robot_id]
        command = pio_command(pio_bin, "run", "-e", robot.platformio_env)
        if not build_only:
            command.extend(["-t", "upload", "--upload-port", target.port or ""])

        print("\n[go2_flash] target:", robot.summary(), flush=True)
        if target.port:
            print(f"[go2_flash] port: {target.port}", flush=True)
        print(
            "[go2_flash] command:",
            " ".join(shlex.quote(part) for part in command),
            flush=True,
        )

        if dry_run:
            continue

        subprocess.run(command, cwd=PROJECT_ROOT, env=process_env, check=True)


def print_config(
    go2_configs: dict[str, Go2Config],
    secrets_path: Path | None,
    local_secrets: dict[str, str],
) -> None:
    print(f"go2 config file: {GO2_CONFIG_PATH}")
    print(
        f"local secrets file: {LOCAL_SECRETS_PATH} ({'present' if LOCAL_SECRETS_PATH.exists() else 'missing'})"
    )
    print(
        f"legacy secrets file: {LEGACY_LOCAL_SECRETS_PATH} ({'present' if LEGACY_LOCAL_SECRETS_PATH.exists() else 'missing'})"
    )
    print(f"active secrets file: {secrets_path or 'none'}")
    print()
    for robot_id in sorted(go2_configs):
        print(go2_configs[robot_id].summary())
    print()
    for key in REQUIRED_SECRET_KEYS + OPTIONAL_SECRET_KEYS:
        value, source_key = get_secret_value(local_secrets, key)
        if key.endswith("PASSWORD"):
            value = mask_secret(value)
        alias_note = "" if source_key in (None, key) else f" (from {source_key})"
        print(f"{key}={value!r}{alias_note}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Build/upload BattleBang Go2 hit ESP firmware by robot ID and USB port."
    )
    parser.add_argument(
        "--pio",
        default=resolve_pio_binary(),
        help="Path to PlatformIO binary (default: ./.venv-pio/bin/pio or pio)",
    )

    subparsers = parser.add_subparsers(dest="command", required=True)

    subparsers.add_parser(
        "list-ports", help="List currently detected serial ports via PlatformIO"
    )
    subparsers.add_parser(
        "show-config", help="Print Go2 profile config and local secrets presence"
    )

    flash = subparsers.add_parser(
        "flash", help="Build or build+upload one or more Go2 hit ESP firmwares"
    )
    flash.add_argument(
        "--target",
        action="append",
        default=[],
        help="Go2 target, e.g. go2_05=/dev/cu.usbserial-21130. For --build-only, port may be omitted: go2_05",
    )
    flash.add_argument(
        "--map-file",
        type=Path,
        default=None,
        help="TOML file with [robots] robot_id='/dev/...' mappings",
    )
    flash.add_argument(
        "--build-only", action="store_true", help="Build only; do not upload"
    )
    flash.add_argument(
        "--dry-run",
        action="store_true",
        help="Validate inputs and print exact pio commands without running them",
    )
    flash.add_argument(
        "--skip-port-check", action="store_true", help="Skip detected-port validation"
    )
    flash.add_argument("--wifi-ssid")
    flash.add_argument("--wifi-password")
    flash.add_argument("--mqtt-host")
    flash.add_argument("--mqtt-port", type=int)
    flash.add_argument("--mqtt-topic-prefix")

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    go2_configs = load_go2_configs()
    secrets_path, local_secrets = load_local_secrets()

    if args.command == "show-config":
        print_config(go2_configs, secrets_path, local_secrets)
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
            go2_configs=go2_configs,
            detected_ports={item["port"] for item in ports},
            build_only=args.build_only,
            skip_port_check=args.skip_port_check,
        )
        env_overrides, source_summary = build_secret_env(args, local_secrets)
    except FlashConfigError as exc:
        print(f"[go2_flash] error: {exc}", file=sys.stderr)
        return 2
    except subprocess.CalledProcessError as exc:
        print(f"[go2_flash] failed to inspect ports: {exc}", file=sys.stderr)
        return exc.returncode or 1

    print(f"[go2_flash] secrets source: {source_summary}", flush=True)
    if not args.skip_port_check and ports:
        print("[go2_flash] detected ports:", flush=True)
        for item in ports:
            print(f"  - {item['port']} ({item['description']})", flush=True)

    try:
        run_flash(
            pio_bin=args.pio,
            targets=targets,
            go2_configs=go2_configs,
            env_overrides=env_overrides,
            build_only=args.build_only,
            dry_run=args.dry_run,
        )
    except subprocess.CalledProcessError as exc:
        print(
            f"[go2_flash] command failed with exit code {exc.returncode}",
            file=sys.stderr,
        )
        return exc.returncode or 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
