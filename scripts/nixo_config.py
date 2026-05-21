"""Inject optional Nixo PlatformIO build macros from shell environment.

Examples:
  pio run -e esp32dev_nixo
  NIXO_ID=nixo_go2_05 NIXO_WIFI_SSID=... NIXO_MQTT_HOST=... pio run -e esp32dev_nixo
"""

from __future__ import annotations

import os

from SCons.Script import Import  # type: ignore

Import("env")


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
}

defines: list[tuple[str, str]] = []
for env_name, macro_name in string_env_macros.items():
    value = os.environ.get(env_name)
    if value is not None:
        defines.append((macro_name, c_string(value)))

for env_name, macro_name in int_env_macros.items():
    value = os.environ.get(env_name)
    if value is not None:
        defines.append((macro_name, str(int(value))))

if defines:
    env.Append(CPPDEFINES=defines)

print("[nixo_config] esp32dev_nixo: MQTT identity/secrets loaded from src/nIxo/local_secrets.h and env overrides")
