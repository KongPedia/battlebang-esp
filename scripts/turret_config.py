"""Inject per-turret PlatformIO build macros from src/turret/turrets.json.

Usage:
  pio run -e esp32dev_turret_5
  TURRET_ID=turret_5 pio run -e esp32dev_turret
"""

from __future__ import annotations

import json
import os
import re
from pathlib import Path

from SCons.Script import Exit, Import  # type: ignore

Import("env")

PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))
PIO_ENV = env.subst("$PIOENV")
CONFIG_PATH = PROJECT_DIR / "src" / "turret" / "turrets.json"


def project_option(name: str) -> str:
    try:
        value = env.GetProjectOption(name)
    except Exception:
        return ""
    return "" if value is None else str(value).strip()


def detect_turret_id() -> str:
    env_override = os.environ.get("TURRET_ID", "").strip()
    if env_override:
        return env_override

    option_value = project_option("custom_turret_id")
    if option_value:
        return option_value

    match = re.search(r"(turret_\d+)$", PIO_ENV)
    if match:
        return match.group(1)

    return "turret_5"


def c_string(value: str) -> str:
    # PlatformIO/SCons CPPDEFINES passes values directly to -DNAME=value.
    # Use escaped quotes so the preprocessor sees a real C string literal.
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'\\"{escaped}\\"'


def float_macro(value: object) -> str:
    text = f"{float(value):.6f}".rstrip("0").rstrip(".")
    if "." not in text and "e" not in text.lower():
        text += ".0"
    return f"{text}f"


if not CONFIG_PATH.exists():
    print(f"[turret_config] missing config: {CONFIG_PATH}")
    Exit(1)

with CONFIG_PATH.open("r", encoding="utf-8") as f:
    config = json.load(f)

turret_id = detect_turret_id()
turrets = config.get("turrets", {})
entry = turrets.get(turret_id)
if entry is None:
    print(f"[turret_config] unknown turret_id={turret_id!r}; edit {CONFIG_PATH}")
    print(f"[turret_config] known: {', '.join(sorted(turrets))}")
    Exit(1)

if not entry.get("configured", False):
    print(f"[turret_config] {turret_id} is intentionally blank.")
    print(f"[turret_config] Fill x_cm/y_cm/z_cm/default_target_z_cm in {CONFIG_PATH} before building it.")
    Exit(1)

required = ["x_cm", "y_cm", "z_cm", "default_target_z_cm"]
missing = [key for key in required if key not in entry]
if missing:
    print(f"[turret_config] {turret_id} missing keys: {', '.join(missing)}")
    Exit(1)

defines = [
    ("TURRET_ID", c_string(turret_id)),
    ("TURRET_X_CM", float_macro(entry["x_cm"])),
    ("TURRET_Y_CM", float_macro(entry["y_cm"])),
    ("TURRET_Z_CM", float_macro(entry["z_cm"])),
    ("TURRET_DEFAULT_TARGET_Z_CM", float_macro(entry["default_target_z_cm"])),
]

# Optional calibration knobs. Keep absent unless explicitly set in turrets.json.
optional_float_macros = {
    "pitch_offset_deg": "TURRET_PITCH_OFFSET_DEG",
    "yaw_offset_deg": "TURRET_YAW_OFFSET_DEG",
}
for json_key, macro_name in optional_float_macros.items():
    if json_key in entry:
        defines.append((macro_name, float_macro(entry[json_key])))

# Optional idle yaw center/sweep. Defaults preserve legacy -70..+70 deg.
if "idle_yaw_center_deg" in entry:
    defines.append(("TURRET_IDLE_YAW_CENTER_DEG", float_macro(entry["idle_yaw_center_deg"])))
if "idle_yaw_sweep_deg" in entry:
    defines.append(("TURRET_IDLE_YAW_SWEEP_DEG", float_macro(entry["idle_yaw_sweep_deg"])))
if "idle_yaw_sweep_speed_deg_per_sec" in entry:
    defines.append((
        "TURRET_IDLE_YAW_SWEEP_SPEED_DEG_PER_SEC",
        float_macro(entry["idle_yaw_sweep_speed_deg_per_sec"]),
    ))

# Optional idle pitch center. When present, IDLE uses this pitch as its center.
# Add idle_pitch_sweep_deg to sweep around it by +/- that many degrees; omit or
# set 0 to keep the pitch fixed.
if "idle_pitch_deg" in entry:
    defines.append(("TURRET_IDLE_FIXED_PITCH", "1"))
    defines.append(("TURRET_IDLE_PITCH_DEG", float_macro(entry["idle_pitch_deg"])))
    if "idle_pitch_sweep_deg" in entry:
        defines.append(("TURRET_IDLE_PITCH_SWEEP_DEG", float_macro(entry["idle_pitch_sweep_deg"])))
    if "idle_pitch_sweep_speed_deg_per_sec" in entry:
        defines.append((
            "TURRET_IDLE_PITCH_SWEEP_SPEED_DEG_PER_SEC",
            float_macro(entry["idle_pitch_sweep_speed_deg_per_sec"]),
        ))

# Optional build-time injection from shell environment. local_secrets.h still works
# when these are not set. Example:
#   TURRET_WIFI_SSID=... TURRET_WIFI_PASSWORD=... TURRET_MQTT_HOST=... pio run -e esp32dev_turret_5
string_env_macros = {
    "TURRET_WIFI_SSID": "TURRET_BUILD_WIFI_SSID",
    "TURRET_WIFI_PASSWORD": "TURRET_BUILD_WIFI_PASSWORD",
    "TURRET_MQTT_HOST": "TURRET_BUILD_MQTT_HOST",
    "TURRET_MQTT_USERNAME": "TURRET_BUILD_MQTT_USERNAME",
    "TURRET_MQTT_PASSWORD": "TURRET_BUILD_MQTT_PASSWORD",
}
for env_name, macro_name in string_env_macros.items():
    value = os.environ.get(env_name)
    if value is not None:
        defines.append((macro_name, c_string(value)))

int_env_macros = {
    "TURRET_MQTT_PORT": "TURRET_BUILD_MQTT_PORT",
    "TURRET_MQTT_COORDS_IN_METERS": "TURRET_BUILD_MQTT_COORDS_IN_METERS",
}
for env_name, macro_name in int_env_macros.items():
    value = os.environ.get(env_name)
    if value is not None:
        defines.append((macro_name, str(int(value))))

# Build-wide safety: target only aims; firing is always explicit via MQTT/Serial fire command.
defines.append(("TURRET_FORCE_AUTO_FIRE_ON_TARGET", os.environ.get("TURRET_AUTO_FIRE_ON_TARGET", "0")))

env.Append(CPPDEFINES=defines)
print(
    "[turret_config] "
    f"{PIO_ENV}: {turret_id} @ "
    f"({entry['x_cm']}, {entry['y_cm']}, {entry['z_cm']}) cm, "
    f"default_target_z={entry['default_target_z_cm']} cm"
    + (
        f", idle_pitch={entry['idle_pitch_deg']} deg"
        if "idle_pitch_deg" in entry
        else ""
    )
    + (
        f", idle_pitch_sweep=+/-{entry['idle_pitch_sweep_deg']} deg"
        if "idle_pitch_sweep_deg" in entry
        else ""
    )
    + (
        f", idle_yaw_center={entry['idle_yaw_center_deg']} deg"
        if "idle_yaw_center_deg" in entry
        else ""
    )
    + (
        f", idle_yaw_sweep=+/-{entry['idle_yaw_sweep_deg']} deg"
        if "idle_yaw_sweep_deg" in entry
        else ""
    )
)
