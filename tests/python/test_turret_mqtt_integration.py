from __future__ import annotations

import json
import os
import time
from dataclasses import dataclass

import paho.mqtt.publish as publish
import pytest
import serial


def _env_bool(name: str, default: bool = False) -> bool:
    value = os.getenv(name)
    if value is None:
        return default
    return value.lower() in {"1", "true", "yes", "y", "on"}


@dataclass(frozen=True)
class IntegrationConfig:
    serial_port: str = os.getenv("TURRET_TEST_SERIAL_PORT", "/dev/cu.usbserial-0001")
    baudrate: int = int(os.getenv("TURRET_TEST_BAUDRATE", "115200"))
    broker_host: str = os.getenv("TURRET_TEST_BROKER_HOST", "127.0.0.1")
    broker_port: int = int(os.getenv("TURRET_TEST_BROKER_PORT", "1883"))
    turret_id: str = os.getenv("TURRET_TEST_TURRET_ID", "turret_1")
    timeout_s: float = float(os.getenv("TURRET_TEST_TIMEOUT_S", "8"))
    allow_target_motion: bool = _env_bool("TURRET_TEST_ALLOW_TARGET_MOTION", False)

    @property
    def command_topic(self) -> str:
        return f"battlebang/turrets/{self.turret_id}/command"


CFG = IntegrationConfig()


def _require_hardware() -> None:
    if not _env_bool("TURRET_RUN_HW_TESTS", False):
        pytest.skip("Set TURRET_RUN_HW_TESTS=1 to enable hardware integration tests.")
    if not os.path.exists(CFG.serial_port):
        pytest.skip(f"Serial port not found: {CFG.serial_port}")


def _publish(command: str, *, target: dict[str, float] | None = None) -> None:
    payload: dict[str, object] = {"command": command, "turret_id": CFG.turret_id}
    if target is not None:
        payload["target"] = target

    publish.single(
        CFG.command_topic,
        json.dumps(payload),
        hostname=CFG.broker_host,
        port=CFG.broker_port,
    )


def _wait_for_patterns(
    ser: serial.Serial,
    patterns: list[str],
    *,
    timeout_s: float | None = None,
) -> str:
    deadline = time.time() + (timeout_s or CFG.timeout_s)
    buffer = ""

    while time.time() < deadline:
        chunk = ser.read(ser.in_waiting or 1).decode("utf-8", errors="ignore")
        if not chunk:
            continue
        buffer += chunk
        if all(pattern in buffer for pattern in patterns):
            return buffer

    raise AssertionError(
        f"Timed out waiting for patterns {patterns!r}.\nCollected serial log:\n{buffer}"
    )


@pytest.mark.hardware
@pytest.mark.integration
def test_dead_to_idle_transition_over_mqtt() -> None:
    _require_hardware()

    with serial.Serial(CFG.serial_port, CFG.baudrate, timeout=0.2) as ser:
        ser.reset_input_buffer()

        _publish("dead")
        dead_log = _wait_for_patterns(
            ser,
            [
                f'[MQTT] topic={CFG.command_topic}',
                '"command": "dead"',
                "=== MODE CHANGE: -> DEAD ===",
            ],
        )
        assert "MQTT=UP" in dead_log or "[NET] WiFi=UP MQTT=UP" in dead_log

        ser.reset_input_buffer()
        _publish("idle")
        idle_log = _wait_for_patterns(
            ser,
            [
                f'[MQTT] topic={CFG.command_topic}',
                '"command": "idle"',
                "=== MODE CHANGE: -> IDLE ===",
            ],
        )
        assert "MQTT=UP" in idle_log or "[NET] WiFi=UP MQTT=UP" in idle_log


@pytest.mark.hardware
@pytest.mark.integration
def test_dead_to_target_transition_over_mqtt() -> None:
    _require_hardware()

    if not CFG.allow_target_motion:
        pytest.skip(
            "Set TURRET_TEST_ALLOW_TARGET_MOTION=1 only on a safe bench "
            "with auto-fire disabled or mechanically safe to move."
        )

    with serial.Serial(CFG.serial_port, CFG.baudrate, timeout=0.2) as ser:
        ser.reset_input_buffer()

        _publish("dead")
        _wait_for_patterns(ser, ['"command": "dead"', "=== MODE CHANGE: -> DEAD ==="])

        ser.reset_input_buffer()
        _publish(
            "target",
            target={
                "x": 1.25,
                "y": -0.40,
                "z": 0.70,
            },
        )
        target_log = _wait_for_patterns(
            ser,
            [
                f'[MQTT] topic={CFG.command_topic}',
                '"command": "target"',
                "========== TARGET UPDATE ==========",
                "Mode               : TARGET",
                "Source             : MQTT(target)",
            ],
        )
        assert "Target X [cm]" in target_log
        assert "Target Y [cm]" in target_log
