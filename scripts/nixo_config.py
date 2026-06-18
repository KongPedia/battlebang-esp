"""Inject optional Nixo PlatformIO build macros from shell environment.

Examples:
  pio run -e esp32dev_nixo_go2_05
  NIXO_ID=nixo_go2_05 NIXO_WIFI_SSID=... NIXO_MQTT_HOST=... pio run -e esp32dev_nixo_go2_05
"""

from __future__ import annotations

import json
import os
import re
from pathlib import Path

from SCons.Script import Import  # type: ignore

Import("env")

PIO_ENV = env.subst("$PIOENV")
PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))


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


def relay_variant_name() -> str:
    env_value = clean_string_value(os.environ.get("NIXO_RELAY_VARIANT", ""))
    if env_value:
        return env_value
    option_value = clean_string_value(project_option("custom_nixo_variant"))
    return option_value or "relay_1ch"


def load_relay_variant(name: str) -> dict:
    if not re.fullmatch(r"[A-Za-z0-9_-]+", name):
        raise ValueError(f"invalid Nixo relay variant name: {name!r}")
    config_path = PROJECT_DIR / "src" / "nIxo" / "variants" / name / "config.json"
    if not config_path.exists():
        raise FileNotFoundError(f"Nixo relay variant config not found: {config_path}")
    with config_path.open() as f:
        data = json.load(f)
    data.setdefault("name", name)
    validate_relay_variant(name, data)
    return data


def validate_relay_variant(name: str, data: dict) -> None:
    relay_channels = int(data.get("relay_channels", 1))
    relay1_pin = int(data["relay1_pin"])
    relay2_pin = int(data.get("relay2_pin", -1))

    if relay_channels not in (1, 2):
        raise ValueError(f"Nixo relay variant {name!r} must use 1 or 2 channels")
    if relay1_pin < 0:
        raise ValueError(f"Nixo relay variant {name!r} requires relay1_pin >= 0")
    if relay_channels == 1 and relay2_pin >= 0:
        raise ValueError(f"Nixo relay variant {name!r} declares 1 channel but enables relay2_pin={relay2_pin}")
    if relay_channels == 2 and relay2_pin < 0:
        raise ValueError(f"Nixo relay variant {name!r} declares 2 channels but relay2_pin is disabled")
    if relay2_pin >= 0 and relay1_pin == relay2_pin:
        raise ValueError(f"Nixo relay variant {name!r} relay pins must be different")


def append_int_define(defines_map: dict[str, str], macro_name: str, value: object) -> None:
    defines_map[macro_name] = str(int(value))


def append_c_string_define(defines_map: dict[str, str], macro_name: str, value: object) -> None:
    text = clean_string_value(str(value))
    if text:
        defines_map[macro_name] = c_string(text)


def append_literal_define(defines_map: dict[str, str], macro_name: str, value: object) -> None:
    text = clean_string_value(str(value))
    if text:
        if text not in {"HIGH", "LOW", "0", "1"}:
            raise ValueError(f"unsupported literal for {macro_name}: {text!r}")
        defines_map[macro_name] = text


string_env_macros = {
    "NIXO_ID": "NIXO_BUILD_ID",
    "NIXO_WIFI_SSID": "NIXO_BUILD_WIFI_SSID",
    "NIXO_WIFI_PASSWORD": "NIXO_BUILD_WIFI_PASSWORD",
    "NIXO_MQTT_HOST": "NIXO_BUILD_MQTT_HOST",
    "NIXO_MQTT_USERNAME": "NIXO_BUILD_MQTT_USERNAME",
    "NIXO_MQTT_PASSWORD": "NIXO_BUILD_MQTT_PASSWORD",
    "NIXO_MQTT_TOPIC_PREFIX": "NIXO_BUILD_MQTT_TOPIC_PREFIX",
    "NIXO_RELAY1_ROLE": "NIXO_BUILD_RELAY1_ROLE",
    "NIXO_RELAY2_ROLE": "NIXO_BUILD_RELAY2_ROLE",
}
int_env_macros = {
    "NIXO_MQTT_PORT": "NIXO_BUILD_MQTT_PORT",
    "NIXO_FIRE_DEFAULT_DURATION_MS": "NIXO_BUILD_FIRE_DEFAULT_DURATION_MS",
    "NIXO_FIRE_MIN_DURATION_MS": "NIXO_BUILD_FIRE_MIN_DURATION_MS",
    "NIXO_FIRE_MAX_DURATION_MS": "NIXO_BUILD_FIRE_MAX_DURATION_MS",
    "NIXO_FIRE_COOLDOWN_MS": "NIXO_BUILD_FIRE_COOLDOWN_MS",
    "NIXO_PREFIRE_DELAY_MS": "NIXO_BUILD_PREFIRE_DELAY_MS",
    "NIXO_RELAY_CHANNELS": "NIXO_BUILD_RELAY_CHANNELS",
    "NIXO_RELAY1_PIN": "NIXO_BUILD_RELAY1_PIN",
    "NIXO_RELAY2_PIN": "NIXO_BUILD_RELAY2_PIN",
    "NIXO_RELAY_DELAY1_MS": "NIXO_BUILD_RELAY_DELAY1_MS",
}

defines_map: dict[str, str] = {}
go2_id = detect_go2_id()
variant_name = relay_variant_name()
variant = load_relay_variant(variant_name)

append_c_string_define(defines_map, "NIXO_BUILD_RELAY_VARIANT_NAME", variant.get("name", variant_name))
append_int_define(defines_map, "NIXO_BUILD_RELAY_CHANNELS", variant.get("relay_channels", 1))
append_int_define(defines_map, "NIXO_BUILD_RELAY1_PIN", variant["relay1_pin"])
append_int_define(defines_map, "NIXO_BUILD_RELAY2_PIN", variant.get("relay2_pin", -1))
append_c_string_define(defines_map, "NIXO_BUILD_RELAY1_ROLE", variant.get("relay1_role", "relay1"))
append_c_string_define(defines_map, "NIXO_BUILD_RELAY2_ROLE", variant.get("relay2_role", "relay2"))
if "relay_delay1_ms" in variant:
    append_int_define(defines_map, "NIXO_BUILD_RELAY_DELAY1_MS", variant["relay_delay1_ms"])
if "relay_on_level" in variant:
    append_literal_define(defines_map, "NIXO_BUILD_RELAY_ON_LEVEL", variant["relay_on_level"])
if "relay_off_level" in variant:
    append_literal_define(defines_map, "NIXO_BUILD_RELAY_OFF_LEVEL", variant["relay_off_level"])

nixo_id_env = clean_string_value(os.environ.get("NIXO_ID", ""))
if not nixo_id_env:
    defines_map["NIXO_BUILD_ID"] = c_string(f"nixo_{go2_id}")

for env_name, macro_name in string_env_macros.items():
    value = os.environ.get(env_name)
    if value is None:
        continue
    cleaned = clean_string_value(value)
    if cleaned:
        defines_map[macro_name] = c_string(cleaned)

for env_name, macro_name in int_env_macros.items():
    value = os.environ.get(env_name)
    if value is not None and value.strip():
        defines_map[macro_name] = str(int(value))

for env_name, macro_name in {
    "NIXO_RELAY_ON_LEVEL": "NIXO_BUILD_RELAY_ON_LEVEL",
    "NIXO_RELAY_OFF_LEVEL": "NIXO_BUILD_RELAY_OFF_LEVEL",
}.items():
    value = os.environ.get(env_name)
    if value is not None and value.strip():
        append_literal_define(defines_map, macro_name, value)

defines = list(defines_map.items())
if defines:
    env.Append(CPPDEFINES=defines)

print(
    f"[nixo_config] {PIO_ENV}: go2_id={go2_id} nixo_id=nixo_{go2_id} "
    f"relay_variant={variant_name} relay1={defines_map.get('NIXO_BUILD_RELAY1_PIN')} "
    f"relay2={defines_map.get('NIXO_BUILD_RELAY2_PIN')} "
    "MQTT identity/secrets loaded from src/nIxo/local_secrets.h and env overrides"
)
