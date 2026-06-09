"""Inject optional Nixo PlatformIO build macros from shell environment.

Examples:
  pio run -e esp32dev_nixo_go2_05
  NIXO_ID=nixo_go2_05 NIXO_WIFI_SSID=... NIXO_MQTT_HOST=... pio run -e esp32dev_nixo_go2_05
"""

from __future__ import annotations

import os
import re

from SCons.Script import Import  # type: ignore

Import("env")

PIO_ENV = env.subst("$PIOENV")


def project_option(name: str) -> str:
    try:
        value = env.GetProjectOption(name)
    except Exception:
        return ""
    return "" if value is None else str(value).strip()


def clean_string_value(value: str) -> str:
    text = value.strip()
    if len(text) >= 2 and text[0] == text[-1] == '"':
        return text[1:-1]
    return text


def detect_go2_id() -> str:
    for env_name in ("GO2_ID", "ROBOT_ID", "ESP_ROBOT_ID", "BATTLEBANG_ROBOT_ID"):
        env_override = os.environ.get(env_name, "").strip()
        if env_override:
            return clean_string_value(env_override)

    option_value = clean_string_value(project_option("custom_robot_id"))
    if option_value:
        return option_value

    match = re.search(r"(go2_\d+)$", PIO_ENV)
    if match:
        return match.group(1)

    return "go2_03"


def c_string(value: str) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'\\"{escaped}\\"'


string_env_macros = {
    "NIXO_ID": "NIXO_BUILD_ID",
    "NIXO_WIFI_SSID": "NIXO_BUILD_WIFI_SSID",
    "NIXO_WIFI_PASSWORD": "NIXO_BUILD_WIFI_PASSWORD",
    "NIXO_MQTT_HOST": "NIXO_BUILD_MQTT_HOST",
    "NIXO_MQTT_USERNAME": "NIXO_BUILD_MQTT_USERNAME",
    "NIXO_MQTT_PASSWORD": "NIXO_BUILD_MQTT_PASSWORD",
    "NIXO_MQTT_TOPIC_PREFIX": "NIXO_BUILD_MQTT_TOPIC_PREFIX",
}
int_env_macros = {
    "NIXO_MQTT_PORT": "NIXO_BUILD_MQTT_PORT",
    "NIXO_FIRE_DEFAULT_DURATION_MS": "NIXO_BUILD_FIRE_DEFAULT_DURATION_MS",
    "NIXO_FIRE_MIN_DURATION_MS": "NIXO_BUILD_FIRE_MIN_DURATION_MS",
    "NIXO_FIRE_MAX_DURATION_MS": "NIXO_BUILD_FIRE_MAX_DURATION_MS",
    "NIXO_FIRE_COOLDOWN_MS": "NIXO_BUILD_FIRE_COOLDOWN_MS",
    "NIXO_PREFIRE_DELAY_MS": "NIXO_BUILD_PREFIRE_DELAY_MS",
}

defines: list[tuple[str, str]] = []
go2_id = detect_go2_id()
nixo_id_env = clean_string_value(os.environ.get("NIXO_ID", ""))
if not nixo_id_env:
    defines.append(("NIXO_BUILD_ID", c_string(f"nixo_{go2_id}")))

for env_name, macro_name in string_env_macros.items():
    value = os.environ.get(env_name)
    if value is None:
        continue
    cleaned = clean_string_value(value)
    if cleaned:
        defines.append((macro_name, c_string(cleaned)))

for env_name, macro_name in int_env_macros.items():
    value = os.environ.get(env_name)
    if value is not None and value.strip():
        defines.append((macro_name, str(int(value))))

if defines:
    env.Append(CPPDEFINES=defines)

print(f"[nixo_config] {PIO_ENV}: go2_id={go2_id} nixo_id=nixo_{go2_id} MQTT identity/secrets loaded from src/nIxo/local_secrets.h and env overrides")
