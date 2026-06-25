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
try:
    import tomllib
except ModuleNotFoundError:  # Python < 3.11
    tomllib = None
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

PROJECT_ROOT = Path(__file__).resolve().parents[1]
GO2_HARDWARE_PROFILE_PATH = PROJECT_ROOT / "firmware" / "go2" / "hardware_profile.json"
LOCAL_SECRETS_PATH = PROJECT_ROOT / "firmware" / "go2" / "local_secrets.h"
LEGACY_LOCAL_SECRETS_PATH = PROJECT_ROOT / "src" / "local_secrets.h"
DEFAULT_PORT_MAP_PATH = PROJECT_ROOT / "firmware" / "go2" / "upload_targets.toml"
DEFAULT_PIO_PATH = PROJECT_ROOT / ".venv-pio" / "bin" / "pio"
GENERIC_PLATFORMIO_ENV = "esp32dev_go2"
PLACEHOLDER_PREFIXES = ("YOUR_", "COMMAND_CENTER_OR_BROKER_HOST", "192.168.")
OPTIONAL_SECRET_KEYS = (
    "ESP_WIFI_SSID",
    "ESP_WIFI_PASSWORD",
    "ESP_MQTT_HOST",
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
SAFE_RUNTIME_ID_RE = re.compile(r"^[A-Za-z0-9_.-]+$")


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


def load_hardware_profile() -> dict:
    if not GO2_HARDWARE_PROFILE_PATH.exists():
        raise FlashConfigError(f"hardware profile not found: {GO2_HARDWARE_PROFILE_PATH}")
    data = json.loads(GO2_HARDWARE_PROFILE_PATH.read_text(encoding="utf-8"))
    defaults = data.get("defaults")
    if not isinstance(defaults, dict):
        raise FlashConfigError(f"{GO2_HARDWARE_PROFILE_PATH} must contain a defaults object")
    return defaults


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


def get_secret_value(local_secrets: dict[str, str], key: str) -> tuple[str | None, str | None]:
    value = local_secrets.get(key)
    if value is not None:
        return value, key

    for alias in LEGACY_SECRET_ALIASES.get(key, ()):
        value = local_secrets.get(alias)
        if value is not None:
            return value, alias

    return None, None


def build_optional_secret_env(args: argparse.Namespace, local_secrets: dict[str, str]) -> tuple[dict[str, str], str]:
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

    for key in OPTIONAL_SECRET_KEYS:
        if key in env_overrides:
            continue
        local_value, source_key = get_secret_value(local_secrets, key)
        if has_real_secret_value(local_value):
            env_overrides[key] = local_value or ""
            source_parts.append(
                f"{key}=local_secrets.h" if source_key == key else f"{key}=local_secrets.h({source_key})"
            )

    return env_overrides, ", ".join(source_parts) if source_parts else "none (runtime NVS provisioning expected)"


def parse_simple_port_map(text: str, path: Path) -> dict:
    """Parse the small [robots] upload-target TOML subset without Python 3.11 tomllib."""
    robots: dict[str, str] = {}
    section: str | None = None
    for line_number, raw_line in enumerate(text.splitlines(), start=1):
        line = raw_line.split("#", 1)[0].strip()
        if not line:
            continue
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1].strip()
            continue
        if section != "robots":
            continue
        key, sep, value = line.partition("=")
        if not sep:
            raise FlashConfigError(f"Invalid port map line {line_number} in {path}: expected key = value")
        robot_id = key.strip().strip('"\'')
        port = strip_quotes(value.strip())
        if not robot_id or not port:
            raise FlashConfigError(f"Invalid robot port mapping on line {line_number} in {path}")
        robots[robot_id] = port
    return {"robots": robots}


def load_port_map(path: Path) -> list[FlashTarget]:
    if not path.exists():
        raise FlashConfigError(f"Port map file not found: {path}")
    if tomllib is not None:
        with path.open("rb") as f:
            raw = tomllib.load(f)
    else:
        raw = parse_simple_port_map(path.read_text(encoding="utf-8"), path)
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
            raise FlashConfigError(f"Upload target must include a port: {raw!r} (expected go2_03=/dev/cu.usbserial-21130)")
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


def ensure_targets_valid(targets: list[FlashTarget], detected_ports: set[str], build_only: bool, skip_port_check: bool) -> None:
    if not targets:
        raise FlashConfigError("No targets selected. Use --target go2_03=/dev/... or --map-file firmware/go2/upload_targets.toml")

    seen: set[str] = set()
    for target in targets:
        if target.robot_id in seen:
            raise FlashConfigError(f"Duplicate Go2 target label: {target.robot_id}")
        seen.add(target.robot_id)
        if not SAFE_RUNTIME_ID_RE.fullmatch(target.robot_id):
            raise FlashConfigError(f"Invalid runtime robot id label: {target.robot_id!r}")

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


def print_profile_summary(profile: dict) -> None:
    fields = [
        f"hit_cooldown_ms={profile.get('hit_cooldown_ms', 'default')}",
        f"led_pin={profile.get('led_pin', 'default')}",
        f"t1_do_pin={profile.get('t1_do_pin', 'default')}",
        f"topic_prefix={profile.get('mqtt_topic_prefix', 'default')}",
    ]
    print("[go2_flash] hardware profile:", ", ".join(fields), flush=True)


def run_flash(
    pio_bin: str,
    targets: list[FlashTarget],
    env_overrides: dict[str, str],
    build_only: bool,
    dry_run: bool,
    profile: dict,
) -> None:
    process_env = os.environ.copy()
    process_env.update(env_overrides)

    print("[go2_flash] using PlatformIO:", pio_bin, flush=True)
    print("[go2_flash] PlatformIO env:", GENERIC_PLATFORMIO_ENV, flush=True)
    print_profile_summary(profile)
    print(
        "[go2_flash] optional build fallback config:",
        f"ssid={env_overrides.get('ESP_WIFI_SSID', '')}",
        f"mqtt_host={env_overrides.get('ESP_MQTT_HOST', '')}",
        f"mqtt_port={env_overrides.get('ESP_MQTT_PORT', '')}",
        f"topic_prefix={env_overrides.get('ESP_MQTT_TOPIC_PREFIX', 'profile/default')}",
        f"wifi_password={mask_secret(env_overrides.get('ESP_WIFI_PASSWORD', ''))}",
        flush=True,
    )

    if build_only:
        command = pio_command(pio_bin, "run", "-e", GENERIC_PLATFORMIO_ENV)
        print("\n[go2_flash] build once for runtime target labels:", ", ".join(t.robot_id for t in targets), flush=True)
        print("[go2_flash] command:", " ".join(shlex.quote(part) for part in command), flush=True)
        if not dry_run:
            subprocess.run(command, cwd=PROJECT_ROOT, env=process_env, check=True)
        return

    for target in targets:
        command = pio_command(pio_bin, "run", "-e", GENERIC_PLATFORMIO_ENV, "-t", "upload", "--upload-port", target.port or "")
        print(f"\n[go2_flash] upload generic firmware for runtime robot_id={target.robot_id}", flush=True)
        print(f"[go2_flash] port: {target.port}", flush=True)
        print(
            "[go2_flash] command:",
            " ".join(shlex.quote(part) for part in command),
            flush=True,
        )
        print(
            "[go2_flash] after upload, provision identity with:",
            f"python3 scripts/go2/provision.py --robot-id {shlex.quote(target.robot_id)} --serial-port {shlex.quote(target.port or '')}",
            flush=True,
        )
        if dry_run:
            continue
        subprocess.run(command, cwd=PROJECT_ROOT, env=process_env, check=True)


def print_config(profile: dict, secrets_path: Path | None, local_secrets: dict[str, str]) -> None:
    print(f"go2 hardware profile: {GO2_HARDWARE_PROFILE_PATH}")
    print(f"PlatformIO env: {GENERIC_PLATFORMIO_ENV}")
    print("runtime identity: provision robot_id/device_id into NVS; no per-robot build envs")
    print("runtime network/MQTT: provision from firmware/go2/.env.go2 into NVS")
    print(
        f"local secrets file: {LOCAL_SECRETS_PATH} ({'present' if LOCAL_SECRETS_PATH.exists() else 'missing'})"
    )
    print(
        f"legacy secrets file: {LEGACY_LOCAL_SECRETS_PATH} ({'present' if LEGACY_LOCAL_SECRETS_PATH.exists() else 'missing'})"
    )
    print(f"active legacy secrets file: {secrets_path or 'none'}")
    print("local_secrets.h is ignored unless flash --use-local-secrets is passed")
    print()
    print_profile_summary(profile)
    print()
    for key in OPTIONAL_SECRET_KEYS:
        value, source_key = get_secret_value(local_secrets, key)
        if key.endswith("PASSWORD"):
            value = mask_secret(value)
        alias_note = "" if source_key in (None, key) else f" (from {source_key})"
        print(f"{key}={value!r}{alias_note}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Build/upload generic BattleBang Go2 hit ESP firmware; runtime robot_id is provisioned into NVS."
    )
    parser.add_argument(
        "--pio",
        default=resolve_pio_binary(),
        help="Path to PlatformIO binary (default: ./.venv-pio/bin/pio or pio)",
    )

    subparsers = parser.add_subparsers(dest="command", required=True)

    subparsers.add_parser("list-ports", help="List currently detected serial ports via PlatformIO")
    subparsers.add_parser("show-config", help="Print generic Go2 hardware profile and legacy local secrets presence")

    flash = subparsers.add_parser("flash", help="Build or build+upload generic Go2 hit ESP firmware")
    flash.add_argument(
        "--target",
        action="append",
        default=[],
        help="Runtime Go2 id label and port, e.g. go2_03=/dev/cu.usbserial-21130. For --build-only, port may be omitted: go2_03",
    )
    flash.add_argument(
        "--map-file",
        type=Path,
        default=None,
        help="TOML file with [robots] robot_id='/dev/...' mappings",
    )
    flash.add_argument("--build-only", action="store_true", help="Build once only; do not upload")
    flash.add_argument("--dry-run", action="store_true", help="Validate inputs and print exact pio commands without running them")
    flash.add_argument("--skip-port-check", action="store_true", help="Skip detected-port validation")
    flash.add_argument("--wifi-ssid", help="Optional build fallback only; production should provision NVS")
    flash.add_argument("--wifi-password", help="Optional build fallback only; production should provision NVS")
    flash.add_argument("--mqtt-host", help="Optional build fallback only; production should provision NVS")
    flash.add_argument("--mqtt-port", type=int, help="Optional build fallback only; production should provision NVS")
    flash.add_argument("--mqtt-topic-prefix", help="Optional build fallback only; production should provision NVS")
    flash.add_argument(
        "--use-local-secrets",
        action="store_true",
        help="Legacy opt-in: read firmware/go2/local_secrets.h for build-time network fallback. Prefer .env.go2 provisioning.",
    )

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    try:
        profile = load_hardware_profile()
    except FlashConfigError as exc:
        print(f"[go2_flash] error: {exc}", file=sys.stderr)
        return 2

    use_local_secrets = args.command == "show-config" or getattr(args, "use_local_secrets", False)
    secrets_path, local_secrets = load_local_secrets() if use_local_secrets else (None, {})

    if args.command == "show-config":
        print_config(profile, secrets_path, local_secrets)
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
            detected_ports={item["port"] for item in ports},
            build_only=args.build_only,
            skip_port_check=args.skip_port_check,
        )
        env_overrides, source_summary = build_optional_secret_env(args, local_secrets)
    except FlashConfigError as exc:
        print(f"[go2_flash] error: {exc}", file=sys.stderr)
        return 2
    except subprocess.CalledProcessError as exc:
        print(f"[go2_flash] failed to inspect ports: {exc}", file=sys.stderr)
        return exc.returncode or 1

    print(f"[go2_flash] optional secrets source: {source_summary}", flush=True)
    if not args.skip_port_check and ports:
        print("[go2_flash] detected ports:", flush=True)
        for item in ports:
            print(f"  - {item['port']} ({item['description']})", flush=True)

    try:
        run_flash(
            pio_bin=args.pio,
            targets=targets,
            env_overrides=env_overrides,
            build_only=args.build_only,
            dry_run=args.dry_run,
            profile=profile,
        )
    except subprocess.CalledProcessError as exc:
        print(f"[go2_flash] command failed with exit code {exc.returncode}", file=sys.stderr)
        return exc.returncode or 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
