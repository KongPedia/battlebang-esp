from __future__ import annotations

import json
import os
import sys
import time
from pathlib import Path
from typing import Any, Iterable, Sequence

PROJECT_ROOT = Path(__file__).resolve().parents[2]
MAX_SERIAL_COMMAND_BYTES = 2048


class ProvisioningError(RuntimeError):
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


def merged_env(env_file: Path, prefixes: Sequence[str]) -> dict[str, str]:
    values = parse_dotenv(env_file)
    normalized_prefixes = tuple(prefix if prefix.endswith("_") else f"{prefix}_" for prefix in prefixes)
    for key, value in os.environ.items():
        if key.startswith(normalized_prefixes):
            values[key] = value
    return values


def env_first(env: dict[str, str], *keys: str, default: str | None = None) -> str | None:
    for key in keys:
        value = env.get(key)
        if value is not None and value != "":
            return value
    return default


def prefixed_keys(prefixes: Sequence[str], suffix: str) -> list[str]:
    return [f"{prefix.rstrip('_')}_{suffix}" for prefix in prefixes]


def env_prefixed(env: dict[str, str], prefixes: Sequence[str], suffix: str, default: str | None = None) -> str | None:
    return env_first(env, *prefixed_keys(prefixes, suffix), default=default)


def env_int(env: dict[str, str], keys: Iterable[str], default: int | None = None) -> int | None:
    value = env_first(env, *list(keys))
    if value is None or value == "":
        return default
    try:
        return int(value, 0)
    except ValueError as exc:
        joined = "/".join(keys)
        raise ProvisioningError(f"{joined} must be an integer, got {value!r}") from exc


def env_bool(env: dict[str, str], keys: Iterable[str], default: bool) -> bool:
    value = env_first(env, *list(keys))
    if value is None or value == "":
        return default
    normalized = value.strip().lower()
    if normalized in {"1", "true", "yes", "y", "on", "enable", "enabled"}:
        return True
    if normalized in {"0", "false", "no", "n", "off", "disable", "disabled"}:
        return False
    joined = "/".join(keys)
    raise ProvisioningError(f"{joined} must be boolean true/false, got {value!r}")


def require_value(value: str | None, label: str) -> str:
    if value is None or value == "":
        raise ProvisioningError(f"missing required value: {label}")
    return value


def build_common_runtime_doc(
    env: dict[str, str],
    *,
    prefixes: Sequence[str],
    command_type: str,
    device_id: str,
    group_default: str,
    mqtt_root_default: str = "battlebang",
    ota_channel_default: str,
    ota_public_manifest_url_default: str = "",
) -> dict[str, Any]:
    if command_type not in {"provision", "config"}:
        raise ProvisioningError("command_type must be 'provision' or 'config'")

    config_version = env_int(env, prefixed_keys(prefixes, "CONFIG_VERSION"))
    if config_version is None:
        config_version = int(time.time())

    wifi_ssid = require_value(
        env_prefixed(env, prefixes, "WIFI_SSID", default=""),
        f"one of {', '.join(prefixed_keys(prefixes, 'WIFI_SSID'))}",
    )
    mqtt_host = require_value(
        env_prefixed(env, prefixes, "MQTT_HOST", default=""),
        f"one of {', '.join(prefixed_keys(prefixes, 'MQTT_HOST'))}",
    )

    return {
        "type": command_type,
        "schema": 1,
        "config_version": config_version,
        "configured": True,
        "device_id": device_id,
        "group": env_prefixed(env, prefixes, "GROUP", default=group_default),
        "stage_id": env_prefixed(env, prefixes, "STAGE_ID", default=""),
        "location": env_prefixed(env, prefixes, "LOCATION", default=""),
        "wifi": {
            "ssid": wifi_ssid,
            "password": env_prefixed(env, prefixes, "WIFI_PASSWORD", default=""),
            "auto_start": env_bool(env, prefixed_keys(prefixes, "NETWORK_AUTO_START"), True),
            "start_delay_ms": env_int(env, prefixed_keys(prefixes, "NETWORK_START_DELAY_MS"), 0),
        },
        "mqtt": {
            "host": mqtt_host,
            "port": env_int(env, prefixed_keys(prefixes, "MQTT_PORT"), 1883),
            "username": env_prefixed(env, prefixes, "MQTT_USERNAME", default=""),
            "password": env_prefixed(env, prefixes, "MQTT_PASSWORD", default=""),
            "root": env_prefixed(env, prefixes, "MQTT_ROOT", default=mqtt_root_default),
        },
        "ota": {
            "command_center_controlled": env_bool(
                env, prefixed_keys(prefixes, "OTA_COMMAND_CENTER_CONTROLLED"), True
            ),
            "auto_check_enabled": env_bool(env, prefixed_keys(prefixes, "OTA_AUTO_CHECK"), False),
            "channel": env_prefixed(env, prefixes, "OTA_CHANNEL", default=ota_channel_default),
            "desired_build": env_int(env, prefixed_keys(prefixes, "OTA_DESIRED_BUILD"), 0),
            "public_manifest_url": env_prefixed(
                env, prefixes, "OTA_PUBLIC_MANIFEST_URL", default=ota_public_manifest_url_default
            ),
            "local_mirror_url": env_prefixed(env, prefixes, "OTA_LOCAL_MIRROR_URL", default=""),
            "check_interval_s": env_int(env, prefixed_keys(prefixes, "OTA_CHECK_INTERVAL_S"), 3600),
            "apply_only_in_safe_state": env_bool(
                env, prefixed_keys(prefixes, "OTA_APPLY_ONLY_IN_SAFE_STATE"), True
            ),
        },
    }


def redacted(doc: dict[str, Any]) -> dict[str, Any]:
    clone = json.loads(json.dumps(doc, ensure_ascii=False))
    for section in ("wifi", "mqtt"):
        value = clone.get(section)
        if isinstance(value, dict) and value.get("password"):
            value["password"] = "***"
    return clone


def compact_json(doc: dict[str, Any]) -> str:
    return json.dumps(doc, ensure_ascii=False, separators=(",", ":"))


def make_serial_command(action: str, payload: dict[str, Any] | None = None) -> str:
    if action in {"status", "show-status"}:
        return "show-status"
    if action == "show-config":
        return "show-config"
    if action == "clear-config":
        return "clear-config"
    if action not in {"provision", "config"}:
        raise ProvisioningError(f"unsupported command: {action}")
    if payload is None:
        raise ProvisioningError(f"{action} requires a JSON payload")
    return f"{action} {compact_json(payload)}"


def write_serial(port: str, baud: int, command: str, wait_s: float) -> None:
    if len(command.encode("utf-8")) > MAX_SERIAL_COMMAND_BYTES:
        raise ProvisioningError(
            f"serial command is {len(command.encode('utf-8'))} bytes; firmware line limit is {MAX_SERIAL_COMMAND_BYTES}"
        )
    try:
        import serial  # type: ignore[import-not-found]
    except ImportError as exc:
        raise ProvisioningError("pyserial is required; use ./.venv-pio/bin/python or install pyserial") from exc

    with serial.Serial(port, baudrate=baud, timeout=wait_s) as ser:  # type: ignore[attr-defined]
        time.sleep(0.2)
        ser.reset_input_buffer()
        ser.write(command.encode("utf-8") + b"\n")
        ser.flush()
        deadline = time.time() + wait_s
        while time.time() < deadline:
            line = ser.readline()
            if line:
                sys.stdout.write(line.decode("utf-8", errors="replace"))
