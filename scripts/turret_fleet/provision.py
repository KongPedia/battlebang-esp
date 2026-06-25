#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

PROJECT_ROOT = Path(__file__).resolve().parents[2]
TURRETS_JSON = PROJECT_ROOT / "src" / "turret" / "turrets.json"
DEFAULT_PROFILE_DIR = PROJECT_ROOT / "firmware" / "turret_fleet" / "profiles"
DEFAULT_PATTERN_PRESETS_DIR = PROJECT_ROOT / "firmware" / "turret_fleet" / "pattern_presets"
DEFAULT_ENV_FILE = PROJECT_ROOT / "firmware" / "turret_fleet" / ".env.turret_fleet"
DEFAULT_PIO = PROJECT_ROOT / ".venv-pio" / "bin" / "pio"
DEFAULT_PYTHON = PROJECT_ROOT / ".venv-pio" / "bin" / "python"
SERIAL_WRITE_CHUNK_BYTES = 96
SERIAL_WRITE_CHUNK_DELAY_S = 0.02
SERIAL_BOOT_SETTLE_S = 4.0
SERIAL_PROVISION_RETRIES = 3


class ProvisionError(RuntimeError):
    pass


def parse_bool(value: str | None, default: bool = False) -> bool:
    if value is None:
        return default
    normalized = value.strip().lower()
    if normalized in {"1", "true", "yes", "y", "on"}:
        return True
    if normalized in {"0", "false", "no", "n", "off"}:
        return False
    raise ProvisionError(f"invalid boolean value: {value!r}")


def parse_dotenv(path: Path) -> dict[str, str]:
    if not path.exists():
        return {}
    result: dict[str, str] = {}
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
        result[key] = value
    return result


def merged_env(env_file: Path) -> dict[str, str]:
    values = parse_dotenv(env_file)
    # Shell env wins over the file so one-shot overrides are easy in CI/terminal.
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


def require_env(env: dict[str, str], *keys: str) -> str:
    value = env_first(env, *keys)
    if value is None:
        joined = " or ".join(keys)
        raise ProvisionError(f"missing required dotenv/env value: {joined}")
    return value


def deep_merge(base: dict[str, Any], override: dict[str, Any]) -> dict[str, Any]:
    merged = dict(base or {})
    for key, value in (override or {}).items():
        if isinstance(value, dict) and isinstance(merged.get(key), dict):
            merged[key] = deep_merge(merged[key], value)
        else:
            merged[key] = value
    return merged


def project_path(path: str | Path) -> Path:
    resolved = Path(path)
    if resolved.is_absolute():
        return resolved
    return PROJECT_ROOT / resolved


def load_profile_file(path: Path) -> dict[str, Any]:
    try:
        doc = json.loads(path.read_text(encoding="utf-8"))
    except OSError as exc:
        raise ProvisionError(f"cannot read turret profile file {path}: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise ProvisionError(f"invalid turret profile JSON {path}: {exc}") from exc
    if not isinstance(doc, dict):
        raise ProvisionError("turret profile file must contain a JSON object")
    return doc


def load_patterns_file(path: Path) -> dict[str, Any]:
    try:
        doc = json.loads(path.read_text(encoding="utf-8"))
    except OSError as exc:
        raise ProvisionError(f"cannot read pattern presets file {path}: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise ProvisionError(f"invalid pattern presets JSON {path}: {exc}") from exc
    if not isinstance(doc, dict):
        raise ProvisionError("pattern presets file must contain a JSON object")
    patterns = doc.get("patterns", doc)
    if not isinstance(patterns, dict):
        raise ProvisionError("pattern presets file must contain a patterns object")
    return patterns


def load_turret_table() -> dict[str, Any]:
    return json.loads(TURRETS_JSON.read_text(encoding="utf-8"))


def normalize_turret_id(raw: str) -> str:
    if raw.isdigit():
        return f"turret_{raw}"
    if raw.startswith("turret_"):
        return raw
    raise ProvisionError(f"invalid turret id {raw!r}; use turret_2 or 2")


def select_profile_file(turret_id: str, env: dict[str, str], explicit: Path | None) -> Path | None:
    if explicit is not None:
        return project_path(explicit)
    env_path = env_first(env, "TURRET_FLEET_PROFILE_FILE", "TURRET_FLEET_CONFIG_FILE")
    if env_path:
        return project_path(env_path)
    default_path = DEFAULT_PROFILE_DIR / f"{turret_id}.json"
    return default_path if default_path.exists() else None


def select_pattern_presets_file(turret_id: str, env: dict[str, str], explicit: Path | None) -> Path | None:
    if explicit is not None:
        return project_path(explicit)
    env_path = env_first(env, "TURRET_FLEET_PATTERN_PRESETS_FILE")
    if env_path:
        return project_path(env_path)
    default_path = DEFAULT_PATTERN_PRESETS_DIR / f"{turret_id}.json"
    return default_path if default_path.exists() else None


def build_config(
    turret_id: str,
    env: dict[str, str],
    include_secrets: bool = True,
    profile_file: Path | None = None,
    pattern_presets_file: Path | None = None,
    profile_id: str | None = None,
) -> dict[str, Any]:
    turret_id = normalize_turret_id(turret_id)
    runtime_device_id = env_first(env, "TURRET_FLEET_DEVICE_ID", default=turret_id) or turret_id
    profile_id = normalize_turret_id(
        profile_id or env_first(env, "TURRET_FLEET_PROFILE_ID", default=turret_id) or turret_id
    )
    runtime_group = env_first(env, "TURRET_FLEET_GROUP", default="boss")
    runtime_stage_id = env_first(env, "TURRET_FLEET_STAGE_ID", default="boss_stage_v1")
    runtime_frame_id = env_first(env, "TURRET_FLEET_FRAME_ID", default=runtime_stage_id or "boss_stage_v1")
    table = load_turret_table()
    entry = table.get("turrets", {}).get(profile_id)
    if not isinstance(entry, dict) or not entry.get("configured"):
        raise ProvisionError(f"profile_id {profile_id} is not configured in {TURRETS_JSON}")

    motion_defaults = table.get("motion_defaults", {})
    motion = deep_merge(motion_defaults, entry.get("motion", {}))
    yaw_motion = motion.get("yaw", {})
    pitch_motion = motion.get("pitch", {})

    config_version = int(env_first(env, "TURRET_FLEET_CONFIG_VERSION", default=str(int(time.time()))))
    mqtt_host = require_env(env, "TURRET_FLEET_MQTT_HOST", "TURRET_MQTT_HOST")
    mqtt_port = int(env_first(env, "TURRET_FLEET_MQTT_PORT", "TURRET_MQTT_PORT", default="1883") or "1883")

    wifi_ssid = require_env(env, "TURRET_FLEET_WIFI_SSID", "TURRET_WIFI_SSID")
    wifi_password = require_env(env, "TURRET_FLEET_WIFI_PASSWORD", "TURRET_WIFI_PASSWORD")

    fire_default_ms = int(env_first(env, "TURRET_FLEET_FIRE_DEFAULT_HOLD_MS", default="500") or "500")
    fire_run_us = int(env_first(env, "TURRET_FLEET_FIRE_ESC_RUN_US", default="1700") or "1700")
    fire_relay_profile = env_first(env, "TURRET_FLEET_FIRE_RELAY_PROFILE", default="").strip()
    fire_relay_active_low = parse_bool(env_first(env, "TURRET_FLEET_FIRE_RELAY_ACTIVE_LOW", default="true"), default=True)
    fire_relay_ch1_active_low = parse_bool(
        env_first(env, "TURRET_FLEET_FIRE_RELAY_CH1_ACTIVE_LOW", default=str(fire_relay_active_low).lower()),
        default=fire_relay_active_low,
    )
    fire_relay_ch2_active_low = parse_bool(
        env_first(env, "TURRET_FLEET_FIRE_RELAY_CH2_ACTIVE_LOW", default=str(fire_relay_active_low).lower()),
        default=fire_relay_active_low,
    )
    fire_relay_ch3_active_low = parse_bool(
        env_first(env, "TURRET_FLEET_FIRE_RELAY_CH3_ACTIVE_LOW", default=str(fire_relay_active_low).lower()),
        default=fire_relay_active_low,
    )
    dead_pitch = float(env_first(env, "TURRET_FLEET_DEAD_PITCH_DEG", default=str(pitch_motion.get("dead_deg", 20.0))) or 20.0)
    home_yaw_deg = float(env_first(env, "TURRET_FLEET_HOME_YAW_DEG", default="0.0") or "0.0")
    home_pitch_deg = float(env_first(env, "TURRET_FLEET_HOME_PITCH_DEG", default="0.0") or "0.0")
    yaw_min_deg = float(env_first(env, "TURRET_FLEET_YAW_MIN_DEG", default="-75.0") or "-75.0")
    yaw_max_deg = float(env_first(env, "TURRET_FLEET_YAW_MAX_DEG", default="75.0") or "75.0")
    pitch_min_deg = float(env_first(env, "TURRET_FLEET_PITCH_MIN_DEG", default="-75.0") or "-75.0")
    pitch_max_deg = float(env_first(env, "TURRET_FLEET_PITCH_MAX_DEG", default="75.0") or "75.0")
    network_auto_start = parse_bool(env_first(env, "TURRET_FLEET_NETWORK_AUTO_START", default="true"), default=True)
    yaw_stop_us = int(env_first(env, "TURRET_FLEET_YAW_STOP_US", default="1500") or "1500")
    pitch_stop_us = int(env_first(env, "TURRET_FLEET_PITCH_STOP_US", default="1500") or "1500")
    servo_max_delta_us = int(env_first(env, "TURRET_FLEET_SERVO_MAX_DELTA_US", default="220") or "220")
    yaw_min_drive_us = int(env_first(env, "TURRET_FLEET_YAW_MIN_DRIVE_US", default="150") or "150")
    yaw_plus_max_delta_us = int(env_first(env, "TURRET_FLEET_YAW_PLUS_MAX_DELTA_US", default="0") or "0")
    yaw_minus_max_delta_us = int(env_first(env, "TURRET_FLEET_YAW_MINUS_MAX_DELTA_US", default="0") or "0")
    yaw_plus_min_drive_us = int(env_first(env, "TURRET_FLEET_YAW_PLUS_MIN_DRIVE_US", default="0") or "0")
    yaw_minus_min_drive_us = int(env_first(env, "TURRET_FLEET_YAW_MINUS_MIN_DRIVE_US", default="0") or "0")
    pitch_min_drive_us = int(env_first(env, "TURRET_FLEET_PITCH_MIN_DRIVE_US", default="80") or "80")
    servo_attach_settle_ms = int(env_first(env, "TURRET_FLEET_SERVO_ATTACH_SETTLE_MS", default="350") or "350")
    axis_switch_cooldown_ms = int(env_first(env, "TURRET_FLEET_AXIS_SWITCH_COOLDOWN_MS", default="800") or "800")
    axis_divergence_guard_ms = int(env_first(env, "TURRET_FLEET_AXIS_DIVERGENCE_GUARD_MS", default="3000") or "3000")
    axis_divergence_margin_deg = float(env_first(env, "TURRET_FLEET_AXIS_DIVERGENCE_MARGIN_DEG", default="20.0") or "20.0")

    doc: dict[str, Any] = {
        "type": "provision",
        "schema": 2,
        "config_version": config_version,
        "configured": True,
        "device_id": runtime_device_id,
        "turret_id": turret_id,
        "group": runtime_group,
        "stage_id": runtime_stage_id,
        "floor": int(env_first(env, "TURRET_FLEET_FLOOR", default="1") or "1"),
        "side": env_first(env, "TURRET_FLEET_SIDE", default=""),
        "coordinate_frame": {
            "frame_id": runtime_frame_id,
            "unit": "cm",
            "origin": env_first(env, "TURRET_FLEET_FRAME_ORIGIN", default="boss_stage_center_floor"),
            "x_axis": env_first(env, "TURRET_FLEET_FRAME_X_AXIS", default="stage_forward"),
            "y_axis": env_first(env, "TURRET_FLEET_FRAME_Y_AXIS", default="stage_right"),
            "z_axis": "up",
            "mqtt_target_unit": env_first(env, "TURRET_FLEET_MQTT_TARGET_UNIT", default="m"),
        },
        "pose": {
            "x_cm": float(entry["x_cm"]),
            "y_cm": float(entry["y_cm"]),
            "z_cm": float(entry["z_cm"]),
            "default_target_z_cm": float(entry["default_target_z_cm"]),
        },
        "calibration": {
            "yaw_zero_reference": "faces_frame_origin",
            "yaw_offset_deg": float(entry.get("yaw_offset_deg", 0.0)),
            "pitch_offset_deg": float(entry.get("pitch_offset_deg", 0.0)),
            "home_yaw_deg": home_yaw_deg,
            "home_pitch_deg": home_pitch_deg,
        },
        "motion": {
            "yaw_stop_us": yaw_stop_us,
            "pitch_stop_us": pitch_stop_us,
            "servo_max_delta_us": servo_max_delta_us,
            "yaw_min_drive_us": yaw_min_drive_us,
            "yaw_plus_max_delta_us": yaw_plus_max_delta_us,
            "yaw_minus_max_delta_us": yaw_minus_max_delta_us,
            "yaw_plus_min_drive_us": yaw_plus_min_drive_us,
            "yaw_minus_min_drive_us": yaw_minus_min_drive_us,
            "pitch_min_drive_us": pitch_min_drive_us,
            "servo_attach_settle_ms": servo_attach_settle_ms,
            "axis_switch_cooldown_ms": axis_switch_cooldown_ms,
            "axis_divergence_guard_ms": axis_divergence_guard_ms,
            "axis_divergence_margin_deg": axis_divergence_margin_deg,
            "limits": {
                "yaw_min_deg": yaw_min_deg,
                "yaw_max_deg": yaw_max_deg,
                "pitch_min_deg": pitch_min_deg,
                "pitch_max_deg": pitch_max_deg,
            },
            "home": {
                "yaw_deg": home_yaw_deg,
                "pitch_deg": home_pitch_deg,
            },
            "idle": {
                "yaw_min_deg": float(yaw_motion.get("idle_min_deg", -15.0)),
                "yaw_max_deg": float(yaw_motion.get("idle_max_deg", 15.0)),
                "yaw_speed_deg_s": float(yaw_motion.get("idle_speed_deg_per_sec", 8.0)),
                "pitch_min_deg": float(pitch_motion.get("idle_min_deg", -4.0)),
                "pitch_max_deg": float(pitch_motion.get("idle_max_deg", 3.0)),
                "pitch_speed_deg_s": float(pitch_motion.get("idle_speed_deg_per_sec", 2.0)),
            },
            "dead": {"pitch_deg": dead_pitch},
        },
        "fire": {
            "esc_run_us": fire_run_us,
            "esc_stop_us": int(env_first(env, "TURRET_FLEET_FIRE_ESC_STOP_US", default="1000") or "1000"),
            "default_hold_ms": fire_default_ms,
            "min_hold_ms": 100,
            "max_hold_ms": 60000,
            "relay_step_delay_ms": int(env_first(env, "TURRET_FLEET_FIRE_RELAY_STEP_DELAY_MS", default="250") or "250"),
            "relay_profile": fire_relay_profile,
            "relay_active_low": fire_relay_active_low,
            "relay_ch1_active_low": fire_relay_ch1_active_low,
            "relay_ch2_active_low": fire_relay_ch2_active_low,
            "relay_ch3_active_low": fire_relay_ch3_active_low,
        },
        "network": {
            "auto_start": network_auto_start,
            "start_delay_ms": int(env_first(env, "TURRET_FLEET_NETWORK_START_DELAY_MS", default="10000") or "10000"),
        },
        "mqtt": {
            "host": mqtt_host,
            "port": mqtt_port,
            "root": env_first(env, "TURRET_FLEET_MQTT_ROOT", "TURRET_MQTT_ROOT", default="battlebang"),
        },
        "ota": {
            "command_center_controlled": True,
            "auto_check_enabled": parse_bool(env_first(env, "TURRET_FLEET_OTA_AUTO_CHECK_ENABLED", default="false"), default=False),
            "channel": env_first(env, "TURRET_FLEET_OTA_CHANNEL", default="stable"),
            "desired_build": int(env_first(env, "TURRET_FLEET_OTA_DESIRED_BUILD", default="0") or "0"),
            "public_manifest_url": env_first(
                env,
                "TURRET_FLEET_OTA_PUBLIC_MANIFEST_URL",
                default="https://github.com/KongPedia/battlebang-esp/releases/download/turret-fleet-latest/manifest.json",
            ),
            "local_mirror_url": env_first(env, "TURRET_FLEET_OTA_LOCAL_MIRROR_URL", default=""),
            "check_interval_s": int(env_first(env, "TURRET_FLEET_OTA_CHECK_INTERVAL_S", default="300") or "300"),
            "apply_only_in_safe_state": True,
        },
    }

    if include_secrets:
        doc["wifi"] = {"ssid": wifi_ssid, "password": wifi_password}
        mqtt_username = env_first(env, "TURRET_FLEET_MQTT_USERNAME", "TURRET_MQTT_USERNAME")
        mqtt_password = env_first(env, "TURRET_FLEET_MQTT_PASSWORD", "TURRET_MQTT_PASSWORD")
        if mqtt_username:
            doc["mqtt"]["username"] = mqtt_username
        if mqtt_password:
            doc["mqtt"]["password"] = mqtt_password
    else:
        doc["wifi"] = {"ssid": wifi_ssid, "password": "***"}

    selected_profile_file = select_profile_file(profile_id, env, profile_file)
    if selected_profile_file is not None:
        overlay = load_profile_file(selected_profile_file)
        overlay_turret_id = overlay.get("turret_id")
        if overlay_turret_id is not None and normalize_turret_id(str(overlay_turret_id)) != profile_id:
            raise ProvisionError(
                f"turret profile file {selected_profile_file} is for {overlay_turret_id}, not profile_id {profile_id}"
            )
        doc = deep_merge(doc, overlay)

    selected_pattern_presets_file = select_pattern_presets_file(profile_id, env, pattern_presets_file)
    if selected_pattern_presets_file is not None:
        doc["patterns"] = load_patterns_file(selected_pattern_presets_file)

    # Provision payloads must always identify the selected physical target and
    # carry a fresh/enforced config_version, even when overlaid from reusable
    # non-secret turret profile files.
    doc["type"] = "provision"
    doc["configured"] = True
    doc["device_id"] = runtime_device_id
    doc["turret_id"] = turret_id
    doc["group"] = runtime_group
    doc["stage_id"] = runtime_stage_id
    if not isinstance(doc.get("coordinate_frame"), dict):
        doc["coordinate_frame"] = {}
    doc["coordinate_frame"]["frame_id"] = runtime_frame_id
    doc["config_version"] = config_version

    return doc


def mask_config(doc: dict[str, Any]) -> dict[str, Any]:
    masked = json.loads(json.dumps(doc))
    if isinstance(masked.get("wifi"), dict) and "password" in masked["wifi"]:
        masked["wifi"]["password"] = "***"
    if isinstance(masked.get("mqtt"), dict) and "password" in masked["mqtt"]:
        masked["mqtt"]["password"] = "***"
    return masked


def auto_detect_port(pio_bin: str) -> str:
    completed = subprocess.run(
        [pio_bin, "device", "list", "--json-output"],
        cwd=PROJECT_ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    ports = []
    for item in json.loads(completed.stdout or "[]"):
        port = str(item.get("port", "")).strip()
        if not port:
            continue
        if "Bluetooth" in port or "debug-console" in port:
            continue
        ports.append(port)
    if len(ports) == 1:
        return ports[0]
    if not ports:
        raise ProvisionError("no USB serial port detected")
    raise ProvisionError(f"multiple USB serial ports detected: {', '.join(ports)}")


def write_serial_line(ser: Any, line: str) -> None:
    """Write a command slowly enough for ESP32 serial provisioning.

    Full provision payloads include Wi-Fi/MQTT/profile/pattern JSON and can be
    several kilobytes long.  A single host-side write can overrun the ESP32
    UART RX buffer while the firmware is also booting Wi-Fi/MQTT, which leaves
    the device with a truncated JSON line and a misleading InvalidInput parse
    error.  Chunking keeps first-boot USB provisioning deterministic without
    changing the firmware protocol.
    """
    data = (line + "\n").encode("utf-8")
    for offset in range(0, len(data), SERIAL_WRITE_CHUNK_BYTES):
        ser.write(data[offset : offset + SERIAL_WRITE_CHUNK_BYTES])
        ser.flush()
        time.sleep(SERIAL_WRITE_CHUNK_DELAY_S)


def provision_serial(port: str, doc: dict[str, Any], timeout_s: float) -> dict[str, Any] | None:
    try:
        import serial  # type: ignore
    except Exception as exc:  # pragma: no cover - depends on local env
        raise ProvisionError(f"pyserial is required for provisioning: {exc}") from exc

    payload = json.dumps(doc, separators=(",", ":"))
    status: dict[str, Any] | None = None
    deadline = time.time() + timeout_s
    with serial.Serial(port, 115200, timeout=0.2) as ser:
        # After esptool hard-resets the ESP32, firmware setup prints boot
        # config/status/help and starts Wi-Fi before the main loop begins
        # draining serial commands.  Sending the full provision line too early
        # can overflow the UART RX buffer and produce a misleading
        # "InvalidInput" JSON parse error.  Wait for the shell banner when it
        # is visible; otherwise fall back to a conservative settle delay for
        # already-running devices.
        settle_deadline = time.time() + SERIAL_BOOT_SETTLE_S
        while time.time() < settle_deadline:
            line = ser.readline().decode("utf-8", "replace").strip()
            if "[fleet] serial commands:" in line:
                break
        last_rejection = ""
        applied = False
        for attempt in range(1, SERIAL_PROVISION_RETRIES + 1):
            retry_after_truncated_json = False
            ser.reset_input_buffer()
            ser.reset_output_buffer()
            write_serial_line(ser, f"provision {payload}")
            attempt_deadline = min(deadline, time.time() + max(2.0, timeout_s / SERIAL_PROVISION_RETRIES))
            while time.time() < attempt_deadline:
                line = ser.readline().decode("utf-8", "replace").strip()
                if not line:
                    continue
                if "config rejected" in line:
                    last_rejection = line
                    if "InvalidInput" in line and attempt < SERIAL_PROVISION_RETRIES:
                        print(
                            f"[fleet_provision] retrying serial provision after truncated JSON "
                            f"(attempt {attempt}/{SERIAL_PROVISION_RETRIES})"
                        )
                        time.sleep(0.5)
                        retry_after_truncated_json = True
                        break
                    raise ProvisionError(line)
                if "config applied saved=" in line:
                    print(f"[fleet_provision] {line}")
                    last_rejection = ""
                    applied = True
                    break
                if line.startswith("{"):
                    try:
                        status = json.loads(line)
                    except json.JSONDecodeError:
                        pass
            if applied:
                break
            if retry_after_truncated_json:
                continue
            if time.time() >= deadline:
                break
        if last_rejection:
            raise ProvisionError(last_rejection)

        write_serial_line(ser, "show-status")
        final_deadline = time.time() + 3.0
        while time.time() < final_deadline:
            line = ser.readline().decode("utf-8", "replace").strip()
            if not line:
                continue
            if line.startswith("{"):
                try:
                    status = json.loads(line)
                    break
                except json.JSONDecodeError:
                    pass
    return status


def cmd_build_config(args: argparse.Namespace) -> int:
    env = merged_env(args.env_file)
    doc = build_config(
        args.turret,
        env,
        include_secrets=args.include_secrets,
        profile_file=args.profile_file,
        pattern_presets_file=args.patterns_file,
        profile_id=args.profile_id,
    )
    if args.mask and args.include_secrets:
        doc = mask_config(doc)
    print(json.dumps(doc, ensure_ascii=False, indent=2, sort_keys=True))
    return 0


def cmd_provision(args: argparse.Namespace) -> int:
    env = merged_env(args.env_file)
    doc = build_config(
        args.turret,
        env,
        include_secrets=True,
        profile_file=args.profile_file,
        pattern_presets_file=args.patterns_file,
        profile_id=args.profile_id,
    )
    port = args.port or auto_detect_port(args.pio)
    print(f"[fleet_provision] turret={doc['turret_id']} port={port} config_version={doc['config_version']}")
    print("[fleet_provision] payload:", json.dumps(mask_config(doc), ensure_ascii=False, sort_keys=True))
    if args.dry_run:
        return 0
    status = provision_serial(port, doc, timeout_s=args.timeout)
    if status:
        motion = status.get("motion_state", {})
        print(
            "[fleet_provision] status:",
            f"turret_id={status.get('turret_id')}",
            f"config_version={status.get('config_version')}",
            f"mode={status.get('mode')}",
            f"wifi={status.get('wifi')}",
            f"yaw={motion.get('yaw_current_deg')}",
            f"pitch={motion.get('pitch_current_deg')}",
        )
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Build and serial-provision turret_fleet runtime config from dotenv/env.")
    parser.add_argument("--pio", default=str(DEFAULT_PIO if DEFAULT_PIO.exists() else "pio"))
    sub = parser.add_subparsers(dest="command", required=True)

    build = sub.add_parser("build-config", help="Print fleet runtime provision JSON for a turret")
    build.add_argument("turret")
    build.add_argument("--env-file", type=Path, default=DEFAULT_ENV_FILE)
    build.add_argument("--profile-file", dest="profile_file", type=Path, help="non-secret turret runtime profile; defaults to firmware/turret_fleet/profiles/<turret>.json")
    build.add_argument("--profile-id", help="physical profile id to reuse while provisioning a different runtime turret id, e.g. turret_1 for turret_dev_01")
    build.add_argument("--config-file", dest="profile_file", type=Path, help=argparse.SUPPRESS)
    build.add_argument("--patterns-file", type=Path, help="pattern preset JSON; defaults to firmware/turret_fleet/pattern_presets/<turret>.json")
    build.add_argument("--include-secrets", action="store_true")
    build.add_argument("--mask", action="store_true", default=True)
    build.set_defaults(func=cmd_build_config)

    provision = sub.add_parser("provision", help="Send provision JSON to an already-flashed ESP over USB serial")
    provision.add_argument("turret")
    provision.add_argument("--env-file", type=Path, default=DEFAULT_ENV_FILE)
    provision.add_argument("--profile-file", dest="profile_file", type=Path, help="non-secret turret runtime profile; defaults to firmware/turret_fleet/profiles/<turret>.json")
    provision.add_argument("--profile-id", help="physical profile id to reuse while provisioning a different runtime turret id, e.g. turret_1 for turret_dev_01")
    provision.add_argument("--config-file", dest="profile_file", type=Path, help=argparse.SUPPRESS)
    provision.add_argument("--patterns-file", type=Path, help="pattern preset JSON; defaults to firmware/turret_fleet/pattern_presets/<turret>.json")
    provision.add_argument("--port")
    provision.add_argument("--timeout", type=float, default=10.0)
    provision.add_argument("--dry-run", action="store_true")
    provision.set_defaults(func=cmd_provision)
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        return int(args.func(args) or 0)
    except (ProvisionError, subprocess.CalledProcessError) as exc:
        print(f"[fleet_provision] error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
