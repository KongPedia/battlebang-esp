#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import socket
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable

# Reuse the production helper's dotenv parsing and JSON loaders so the E2E
# harness follows the same operator contract as ./bin/turret fleet-mqtt.
SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parents[1]
sys.path.insert(0, str(SCRIPT_DIR))
from mqtt_command import (  # noqa: E402
    DEFAULT_ENV_FILE,
    env_first,
    load_patterns_file,
    load_profile_file,
    merged_env,
    normalize_turret_id,
    topic_for,
)

E2E_PATTERN_IDS = ("lane_sweep", "two_point_bounce", "telegraph_column")


class E2EError(RuntimeError):
    pass


@dataclass
class TestResult:
    name: str
    status: str
    evidence: dict[str, Any] = field(default_factory=dict)
    message: str = ""

    @property
    def passed(self) -> bool:
        return self.status in {"PASS", "SKIP"}


class MqttSession:
    def __init__(
        self,
        *,
        host: str,
        port: int,
        username: str | None = None,
        password: str | None = None,
        timeout_s: float = 5.0,
    ) -> None:
        self.host = host
        self.port = port
        self.username = username
        self.password = password
        self.timeout_s = timeout_s
        self.sock: socket.socket | None = None
        self.packet_id = 1
        self.last_ping_s = 0.0

    def __enter__(self) -> "MqttSession":
        self.connect()
        return self

    def __exit__(self, exc_type: object, exc: object, tb: object) -> None:
        self.close()

    def connect(self) -> None:
        client_id = f"bb-e2e-{os.getpid()}-{int(time.time())}"
        connect_flags = 0b00000010
        if self.username:
            connect_flags |= 0b10000000
        if self.password:
            connect_flags |= 0b01000000
        variable = mqtt_string("MQTT") + bytes([4, connect_flags, 0, 60])
        payload = mqtt_string(client_id)
        if self.username:
            payload += mqtt_string(self.username)
        if self.password:
            payload += mqtt_string(self.password)
        packet = bytes([0x10]) + encode_remaining_length(len(variable) + len(payload)) + variable + payload

        self.sock = socket.create_connection((self.host, self.port), timeout=self.timeout_s)
        self.sock.settimeout(min(self.timeout_s, 1.0))
        self.sock.sendall(packet)
        self.last_ping_s = time.time()
        packet_type, body = self.read_packet(deadline=time.time() + self.timeout_s)
        if packet_type != 0x20 or len(body) < 2 or body[1] != 0:
            raise E2EError(f"MQTT connect rejected: packet_type={packet_type} body={body!r}")

    def close(self) -> None:
        if self.sock is None:
            return
        try:
            self.sock.sendall(bytes([0xE0, 0x00]))
        except OSError:
            pass
        try:
            self.sock.close()
        finally:
            self.sock = None

    def subscribe(self, topic: str) -> None:
        if self.sock is None:
            raise E2EError("MQTT session is not connected")
        packet_id = self.packet_id
        self.packet_id += 1
        variable = packet_id.to_bytes(2, "big")
        payload = mqtt_string(topic) + bytes([0])
        deadline = time.time() + self.timeout_s
        self.sock.sendall(bytes([0x82]) + encode_remaining_length(len(variable) + len(payload)) + variable + payload)
        last_packet_type: int | None = None
        last_body = b""
        while time.time() < deadline:
            packet_type, body = self.read_packet(deadline=deadline)
            if packet_type is None:
                continue
            if packet_type == 0xD0 or (packet_type & 0xF0) == 0x30:
                # Busy live brokers can deliver retained/heartbeat publishes for
                # earlier subscriptions before the SUBACK for this one.  QoS 0
                # status publishes need no response, so keep waiting for SUBACK.
                continue
            last_packet_type = packet_type
            last_body = body
            if packet_type != 0x90:
                continue
            if len(body) < 3:
                break
            ack_packet_id = int.from_bytes(body[:2], "big")
            if ack_packet_id != packet_id:
                continue
            if body[-1] == 0x80:
                break
            return
        raise E2EError(f"MQTT subscribe failed for {topic}: packet_type={last_packet_type} body={last_body!r}")

    def publish_json(self, topic: str, payload: dict[str, Any]) -> None:
        if self.sock is None:
            raise E2EError("MQTT session is not connected")
        body = mqtt_string(topic) + json.dumps(payload, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
        self.sock.sendall(bytes([0x30]) + encode_remaining_length(len(body)) + body)
        self.last_ping_s = time.time()

    def maybe_ping(self) -> None:
        if self.sock is None:
            raise E2EError("MQTT session is not connected")
        if time.time() - self.last_ping_s < 25.0:
            return
        self.sock.sendall(bytes([0xC0, 0x00]))
        self.last_ping_s = time.time()

    def recv_chunk(self, size: int, *, deadline: float) -> bytes | None:
        if self.sock is None:
            raise E2EError("MQTT session is not connected")
        while time.time() < deadline:
            self.maybe_ping()
            try:
                chunk = self.sock.recv(size)
            except ConnectionResetError as exc:
                raise E2EError("MQTT connection reset by broker/device") from exc
            except socket.timeout:
                continue
            if not chunk:
                return b""
            return chunk
        return None

    def read_packet(self, *, deadline: float) -> tuple[int | None, bytes]:
        if self.sock is None:
            raise E2EError("MQTT session is not connected")
        while time.time() < deadline:
            header = self.recv_chunk(1, deadline=deadline)
            if header is None:
                continue
            if not header:
                return None, b""
            remaining = 0
            multiplier = 1
            while True:
                chunk = self.recv_chunk(1, deadline=deadline)
                if chunk is None:
                    return None, b""
                if not chunk:
                    return None, b""
                digit = chunk[0]
                remaining += (digit & 127) * multiplier
                if (digit & 128) == 0:
                    break
                multiplier *= 128
                if multiplier > 128 * 128 * 128:
                    raise E2EError("malformed MQTT remaining length")
            payload = bytearray()
            while len(payload) < remaining:
                chunk = self.recv_chunk(remaining - len(payload), deadline=deadline)
                if chunk is None:
                    return None, b""
                if not chunk:
                    return None, b""
                payload.extend(chunk)
            return header[0], bytes(payload)
        return None, b""

    def read_publish(self, *, deadline: float) -> tuple[str, str] | None:
        while time.time() < deadline:
            packet_type, body = self.read_packet(deadline=deadline)
            if packet_type is None:
                return None
            if packet_type == 0xD0:
                continue
            if (packet_type & 0xF0) != 0x30:
                continue
            if len(body) < 2:
                continue
            topic_len = int.from_bytes(body[:2], "big")
            topic = body[2 : 2 + topic_len].decode("utf-8", "replace")
            payload = body[2 + topic_len :].decode("utf-8", "replace")
            return topic, payload
        return None


def mqtt_string(value: str) -> bytes:
    encoded = value.encode("utf-8")
    if len(encoded) > 65535:
        raise E2EError("MQTT string too long")
    return len(encoded).to_bytes(2, "big") + encoded


def encode_remaining_length(value: int) -> bytes:
    if value < 0 or value > 268435455:
        raise E2EError("invalid MQTT remaining length")
    out = bytearray()
    while True:
        encoded_byte = value % 128
        value //= 128
        if value > 0:
            encoded_byte |= 128
        out.append(encoded_byte)
        if value == 0:
            return bytes(out)


def load_runtime_profile(turret_id: str, profile_file: Path, patterns_file: Path | None) -> dict[str, Any]:
    doc = load_profile_file(profile_file)
    profile_turret_id = doc.get("turret_id")
    if profile_turret_id is not None and normalize_turret_id(str(profile_turret_id)) != turret_id:
        raise E2EError(f"profile {profile_file} is for {profile_turret_id}, not {turret_id}")
    doc["type"] = "config"
    doc["schema"] = doc.get("schema", 2)
    doc["configured"] = True
    doc["turret_id"] = turret_id
    doc["config_version"] = int(time.time())
    if patterns_file is not None:
        doc["patterns"] = load_patterns_file(patterns_file)
    return doc


def status_summary(doc: dict[str, Any]) -> dict[str, Any]:
    motion = doc.get("motion_state", {}) if isinstance(doc.get("motion_state"), dict) else {}
    fire_output = doc.get("fire_output_state", {}) if isinstance(doc.get("fire_output_state"), dict) else {}
    aim = doc.get("aim_state", {}) if isinstance(doc.get("aim_state"), dict) else {}
    return {
        "reason": doc.get("reason"),
        "mode": doc.get("mode"),
        "pattern_state": doc.get("pattern_state"),
        "pattern_step_type": doc.get("pattern_step_type"),
        "fire_state": doc.get("fire_state"),
        "last_error": doc.get("last_error"),
        "last_command_id": doc.get("last_command_id"),
        "yaw": motion.get("yaw_current_deg"),
        "pitch": motion.get("pitch_current_deg"),
        "yaw_goal": motion.get("yaw_goal_deg"),
        "pitch_goal": motion.get("pitch_goal_deg"),
        "yaw_cmd": motion.get("yaw_command_us"),
        "pitch_cmd": motion.get("pitch_command_us"),
        "selected_axis": motion.get("selected_axis"),
        "tracking_active": motion.get("tracking_active"),
        "aim_reached": motion.get("aim_reached"),
        "brownout_lockout": motion.get("brownout_lockout"),
        "safety_inhibited": motion.get("safety_inhibited"),
        "esc_command_us": fire_output.get("esc_command_us"),
        "relay_ch1_on": fire_output.get("relay_ch1_on"),
        "relay_ch2_on": fire_output.get("relay_ch2_on"),
        "relay_ch3_on": fire_output.get("relay_ch3_on"),
        "last_target_input": aim.get("last_target_input"),
    }


def collect_statuses(
    client: MqttSession,
    status_topic: str,
    *,
    timeout_s: float,
    stop_when: Callable[[dict[str, Any], list[dict[str, Any]]], bool] | None = None,
) -> list[dict[str, Any]]:
    deadline = time.time() + timeout_s
    statuses: list[dict[str, Any]] = []
    while time.time() < deadline:
        item = client.read_publish(deadline=min(deadline, time.time() + 1.0))
        if item is None:
            continue
        topic, payload = item
        if topic != status_topic:
            continue
        try:
            doc = json.loads(payload)
        except json.JSONDecodeError:
            continue
        statuses.append(doc)
        if stop_when is not None and stop_when(doc, statuses):
            break
    return statuses


def drain_statuses(client: MqttSession, status_topic: str, duration_s: float = 0.25) -> None:
    collect_statuses(client, status_topic, timeout_s=duration_s)


def command_id(prefix: str) -> str:
    return f"e2e-{prefix}-{int(time.time() * 1000)}"


def publish_command(
    client: MqttSession,
    command_topic: str,
    payload: dict[str, Any],
    *,
    command_name: str,
) -> str:
    cid = payload.get("command_id") or command_id(command_name)
    payload["command_id"] = cid
    client.publish_json(command_topic, payload)
    return str(cid)


def latest_summary(statuses: list[dict[str, Any]]) -> dict[str, Any]:
    return status_summary(statuses[-1]) if statuses else {}


def axis_range(statuses: list[dict[str, Any]], key: str) -> float:
    values: list[float] = []
    for doc in statuses:
        motion = doc.get("motion_state", {}) if isinstance(doc.get("motion_state"), dict) else {}
        value = motion.get(key)
        if isinstance(value, (int, float)):
            values.append(float(value))
    if len(values) < 2:
        return 0.0
    return max(values) - min(values)


def axis_range_while(statuses: list[dict[str, Any]], key: str, predicate: Callable[[dict[str, Any]], bool]) -> float:
    return axis_range([doc for doc in statuses if predicate(doc)], key)


def command_seen(statuses: list[dict[str, Any]], cid: str) -> bool:
    return any(doc.get("last_command_id") == cid for doc in statuses)


def command_statuses(statuses: list[dict[str, Any]], cid: str) -> list[dict[str, Any]]:
    return [doc for doc in statuses if doc.get("last_command_id") == cid]


def fire_active(doc: dict[str, Any]) -> bool:
    fire_output = doc.get("fire_output_state", {}) if isinstance(doc.get("fire_output_state"), dict) else {}
    return (
        doc.get("fire_state") == "FIRING"
        or bool(fire_output.get("relay_ch1_on"))
        or bool(fire_output.get("relay_ch2_on"))
        or bool(fire_output.get("relay_ch3_on"))
        or int(fire_output.get("esc_command_us") or 1000) > int(fire_output.get("esc_stop_us_config") or 1000)
    )


def terminal_safe(doc: dict[str, Any]) -> bool:
    fire_output = doc.get("fire_output_state", {}) if isinstance(doc.get("fire_output_state"), dict) else {}
    return (
        doc.get("fire_state") == "SAFE_OFF"
        and doc.get("mode") == "WAIT_COMMAND"
        and doc.get("pattern_state") == "IDLE"
        and not bool(fire_output.get("relay_ch1_on"))
        and not bool(fire_output.get("relay_ch2_on"))
        and not bool(fire_output.get("relay_ch3_on"))
    )


def run_hold(client: MqttSession, command_topic: str, status_topic: str, *, observe_s: float = 4.0) -> TestResult:
    drain_statuses(client, status_topic)
    cid = publish_command(client, command_topic, {"command": "hold"}, command_name="hold")
    statuses = collect_statuses(
        client,
        status_topic,
        timeout_s=observe_s,
        stop_when=lambda doc, _: doc.get("last_command_id") == cid and doc.get("mode") == "WAIT_COMMAND",
    )
    summary = latest_summary(statuses)
    motion = statuses[-1].get("motion_state", {}) if statuses else {}
    passed = command_seen(statuses, cid) and summary.get("mode") == "WAIT_COMMAND" and not motion.get("tracking_active")
    return TestResult("hold", "PASS" if passed else "FAIL", {"command_id": cid, "status": summary})


def run_home(client: MqttSession, command_topic: str, status_topic: str, args: argparse.Namespace) -> TestResult:
    drain_statuses(client, status_topic)
    cid = publish_command(client, command_topic, {"command": "home"}, command_name="home")
    statuses = collect_statuses(
        client,
        status_topic,
        timeout_s=args.home_observe_s,
        stop_when=lambda doc, _: (
            doc.get("last_command_id") == cid
            and doc.get("mode") == "HOME"
            and bool((doc.get("motion_state") or {}).get("aim_reached"))
        ),
    )
    own_statuses = command_statuses(statuses, cid)
    reached = any(bool((doc.get("motion_state") or {}).get("aim_reached")) for doc in own_statuses)
    passed = bool(own_statuses) and reached and not own_statuses[-1].get("last_error")
    return TestResult(
        "home",
        "PASS" if passed else "FAIL",
        {
            "command_id": cid,
            "reached": reached,
            "yaw_motion_deg": round(axis_range(own_statuses, "yaw_current_deg"), 3),
            "pitch_motion_deg": round(axis_range(own_statuses, "pitch_current_deg"), 3),
            "sample_count": len(statuses),
            "command_sample_count": len(own_statuses),
            "last_status": latest_summary(statuses),
        },
    )


def run_target(client: MqttSession, command_topic: str, status_topic: str, args: argparse.Namespace) -> TestResult:
    start_statuses = collect_statuses(client, status_topic, timeout_s=2.0, stop_when=lambda _doc, statuses: bool(statuses))
    start = start_statuses[-1] if start_statuses else {}
    target = {"x": args.target_point[0], "y": args.target_point[1], "z": args.target_point[2]}
    drain_statuses(client, status_topic)
    cid = publish_command(client, command_topic, {"command": "target", "frame_id": args.frame_id, "target": target}, command_name="target")
    statuses = collect_statuses(client, status_topic, timeout_s=args.target_observe_s)
    ack = next((doc for doc in statuses if doc.get("last_command_id") == cid), None)
    yaw_motion = axis_range([start] + statuses, "yaw_current_deg")
    pitch_motion = axis_range([start] + statuses, "pitch_current_deg")
    ack_summary = status_summary(ack) if ack else {}
    passed = (
        ack is not None
        and ack.get("mode") == "TARGET"
        and not ack.get("last_error")
        and yaw_motion >= args.min_motion_deg
        and pitch_motion >= args.min_motion_deg
    )
    return TestResult(
        "target",
        "PASS" if passed else "FAIL",
        {
            "command_id": cid,
            "target": target,
            "ack": ack_summary,
            "yaw_motion_deg": round(yaw_motion, 3),
            "pitch_motion_deg": round(pitch_motion, 3),
            "sample_count": len(statuses),
        },
        "expects both yaw and pitch feedback to move by --min-motion-deg",
    )


def run_idle(client: MqttSession, command_topic: str, status_topic: str, args: argparse.Namespace) -> TestResult:
    drain_statuses(client, status_topic)
    cid = publish_command(client, command_topic, {"command": "idle"}, command_name="idle")
    statuses = collect_statuses(client, status_topic, timeout_s=args.idle_observe_s)
    ack = next((doc for doc in statuses if doc.get("last_command_id") == cid), None)
    summary = status_summary(ack) if ack else {}
    yaw_motion = axis_range(statuses, "yaw_current_deg")
    pitch_motion = axis_range(statuses, "pitch_current_deg")
    passed = ack is not None and ack.get("mode") == "IDLE" and not ack.get("last_error")
    return TestResult(
        "idle",
        "PASS" if passed else "FAIL",
        {
            "command_id": cid,
            "status": summary,
            "yaw_motion_deg": round(yaw_motion, 3),
            "pitch_motion_deg": round(pitch_motion, 3),
            "sample_count": len(statuses),
        },
    )


def run_dead(client: MqttSession, command_topic: str, status_topic: str, args: argparse.Namespace) -> TestResult:
    drain_statuses(client, status_topic)
    cid = publish_command(client, command_topic, {"command": "dead"}, command_name="dead")
    statuses = collect_statuses(
        client,
        status_topic,
        timeout_s=args.dead_observe_s,
        stop_when=lambda doc, _: (
            doc.get("last_command_id") == cid
            and doc.get("mode") == "DEAD"
            and doc.get("fire_state") == "SAFE_OFF"
            and dead_pitch_reached(doc, args.dead_tolerance_deg)
        ),
    )
    ack = next((doc for doc in statuses if doc.get("last_command_id") == cid), None)
    summary = status_summary(ack) if ack else {}
    reached = dead_pitch_reached(ack, args.dead_tolerance_deg) if ack else False
    passed = (
        ack is not None
        and ack.get("mode") == "DEAD"
        and not ack.get("last_error")
        and ack.get("fire_state") == "SAFE_OFF"
        and reached
    )
    return TestResult("dead", "PASS" if passed else "FAIL", {"command_id": cid, "reached": reached, "status": summary})


def dead_pitch_reached(doc: dict[str, Any] | None, tolerance_deg: float) -> bool:
    if not doc:
        return False
    motion = doc.get("motion_state", {}) if isinstance(doc.get("motion_state"), dict) else {}
    pitch = motion.get("pitch_current_deg")
    goal = motion.get("pitch_goal_deg")
    if not isinstance(pitch, (int, float)) or not isinstance(goal, (int, float)):
        return False
    return abs(float(pitch) - float(goal)) <= tolerance_deg


def run_fire(client: MqttSession, command_topic: str, status_topic: str, args: argparse.Namespace) -> TestResult:
    if not args.allow_live_fire:
        return TestResult("fire", "SKIP", message="requires --allow-live-fire")
    drain_statuses(client, status_topic)
    cid = publish_command(client, command_topic, {"command": "fire", "duration_ms": args.fire_ms}, command_name="fire")
    statuses = collect_statuses(
        client,
        status_topic,
        timeout_s=max(args.fire_ms / 1000.0 + args.fire_observe_s, args.fire_observe_s),
        stop_when=lambda doc, seen: (
            doc.get("last_command_id") == cid
            and any(fire_active(s) for s in command_statuses(seen, cid))
            and terminal_safe(doc)
        ),
    )
    own_statuses = command_statuses(statuses, cid)
    fire_seen = any(fire_active(doc) for doc in own_statuses)
    safe_done = any(terminal_safe(doc) for doc in own_statuses[1:])
    passed = command_seen(statuses, cid) and fire_seen and safe_done
    return TestResult(
        "fire",
        "PASS" if passed else "FAIL",
        {
            "command_id": cid,
            "fire_seen": fire_seen,
            "terminal_safe": safe_done,
            "sample_count": len(statuses),
            "last_status": latest_summary(statuses),
        },
    )


def sign_changes(values: list[float]) -> int:
    signs: list[int] = []
    for value in values:
        sign = 1 if value > 0.05 else -1 if value < -0.05 else 0
        if sign and (not signs or signs[-1] != sign):
            signs.append(sign)
    return max(0, len(signs) - 1)


def target_y_values(statuses: list[dict[str, Any]]) -> list[float]:
    values: list[float] = []
    for doc in statuses:
        aim = doc.get("aim_state", {}) if isinstance(doc.get("aim_state"), dict) else {}
        target = aim.get("last_target_input")
        if isinstance(target, dict) and isinstance(target.get("y"), (int, float)):
            values.append(float(target["y"]))
    return values


def configured_pattern_points(patterns_file: Path, pattern_id: str) -> list[dict[str, float]]:
    patterns = load_patterns_file(patterns_file)
    presets = patterns.get("presets") if isinstance(patterns.get("presets"), dict) else {}
    preset = presets.get(pattern_id) if isinstance(presets, dict) else None
    points = preset.get("points") if isinstance(preset, dict) else None
    if not isinstance(points, list) or not points:
        raise E2EError(f"pattern {pattern_id} has no configured points in {patterns_file}")
    out: list[dict[str, float]] = []
    for point in points:
        if not isinstance(point, dict):
            raise E2EError(f"pattern {pattern_id} has invalid point in {patterns_file}")
        out.append({"x": float(point["x"]), "y": float(point["y"]), "z": float(point["z"])})
    return out


def run_pattern_prep(
    client: MqttSession,
    command_topic: str,
    status_topic: str,
    pattern_id: str,
    args: argparse.Namespace,
) -> TestResult:
    points = configured_pattern_points(args.patterns_file, pattern_id)
    target = points[0]
    drain_statuses(client, status_topic)
    cid = publish_command(
        client,
        command_topic,
        {"command": "target", "frame_id": args.frame_id, "target": target},
        command_name=f"prep-{pattern_id}",
    )
    statuses = collect_statuses(
        client,
        status_topic,
        timeout_s=args.pattern_prep_timeout_s,
        stop_when=lambda doc, _: (
            doc.get("last_command_id") == cid
            and doc.get("mode") == "TARGET"
            and bool((doc.get("motion_state") or {}).get("aim_reached"))
        ),
    )
    own_statuses = command_statuses(statuses, cid)
    reached = any(bool((doc.get("motion_state") or {}).get("aim_reached")) for doc in own_statuses)
    passed = bool(own_statuses) and reached and not own_statuses[-1].get("last_error")
    return TestResult(
        f"prepare:{pattern_id}",
        "PASS" if passed else "FAIL",
        {
            "command_id": cid,
            "target": target,
            "reached": reached,
            "yaw_motion_deg": round(axis_range(own_statuses, "yaw_current_deg"), 3),
            "pitch_motion_deg": round(axis_range(own_statuses, "pitch_current_deg"), 3),
            "sample_count": len(statuses),
            "command_sample_count": len(own_statuses),
            "last_status": latest_summary(statuses),
        },
        "pre-position to the first configured point so pattern move_timeout tests the pattern, not recovery from dead pose",
    )


def fire_after_aim_reached(statuses: list[dict[str, Any]]) -> bool:
    reached_index: int | None = None
    for index, doc in enumerate(statuses):
        if (
            bool((doc.get("motion_state") or {}).get("aim_reached"))
            or doc.get("pattern_state") == "DWELL"
            or doc.get("pattern_step_type") == "DWELL"
        ):
            reached_index = index
            break
    if reached_index is None:
        return False
    for index, doc in enumerate(statuses):
        if index >= reached_index and fire_active(doc):
            return True
    return False


def run_pattern(client: MqttSession, command_topic: str, status_topic: str, pattern_id: str, args: argparse.Namespace) -> TestResult:
    if not args.allow_live_fire:
        return TestResult(f"pattern:{pattern_id}", "SKIP", message="patterns use live fire; requires --allow-live-fire")

    drain_statuses(client, status_topic)
    cid = publish_command(
        client,
        command_topic,
        {
            "command": "pattern",
            "frame_id": args.frame_id,
            "pattern_id": pattern_id,
            "pattern_instance_id": f"e2e-{pattern_id}-{int(time.time())}",
            "ttl_ms": args.pattern_ttl_ms,
            "params": {},
        },
        command_name=f"pattern-{pattern_id}",
    )
    statuses = collect_statuses(
        client,
        status_topic,
        timeout_s=args.pattern_timeout_s,
        stop_when=lambda doc, seen: (
            doc.get("last_command_id") == cid
            and any(fire_active(s) for s in command_statuses(seen, cid))
            and terminal_safe(doc)
        ),
    )
    own_statuses = command_statuses(statuses, cid)
    step_types = {doc.get("pattern_step_type") for doc in own_statuses if doc.get("pattern_step_type")}
    states = {doc.get("pattern_state") for doc in own_statuses if doc.get("pattern_state")}
    y_values = target_y_values(own_statuses)
    yaw_motion = axis_range(own_statuses, "yaw_current_deg")
    pitch_motion = axis_range(own_statuses, "pitch_current_deg")
    fire_yaw_motion = axis_range_while(own_statuses, "yaw_current_deg", fire_active)
    fire_pitch_motion = axis_range_while(own_statuses, "pitch_current_deg", fire_active)
    fire_seen = any(fire_active(doc) for doc in own_statuses)
    terminal = any(terminal_safe(doc) for doc in own_statuses)
    last_error_clear = not any(doc.get("last_error") for doc in own_statuses)
    fire_while_moving = any(
        fire_active(doc)
        and doc.get("pattern_step_type") == "FIRE_MOVE"
        and bool((doc.get("motion_state") or {}).get("tracking_active"))
        for doc in own_statuses
    )
    fire_after_reached = fire_after_aim_reached(own_statuses)
    y_sign_changes = sign_changes(y_values)
    roundtrip_seen = y_sign_changes >= 1 or yaw_motion >= args.min_motion_deg
    lane_round_trip_seen = y_sign_changes >= 2 or fire_yaw_motion >= args.lane_fire_yaw_motion_deg

    if pattern_id == "lane_sweep":
        passed = (
            command_seen(statuses, cid)
            and fire_seen
            and terminal
            and fire_while_moving
            and roundtrip_seen
            and lane_round_trip_seen
            and fire_yaw_motion >= args.lane_fire_yaw_motion_deg
            and last_error_clear
        )
    elif pattern_id == "two_point_bounce":
        passed = command_seen(statuses, cid) and fire_seen and terminal and fire_after_reached and roundtrip_seen and last_error_clear
    else:
        passed = command_seen(statuses, cid) and fire_seen and terminal and fire_after_reached and last_error_clear

    return TestResult(
        f"pattern:{pattern_id}",
        "PASS" if passed else "FAIL",
        {
            "command_id": cid,
            "fire_seen": fire_seen,
            "terminal_safe": terminal,
            "last_error_clear": last_error_clear,
            "fire_while_moving": fire_while_moving,
            "fire_after_aim_reached": fire_after_reached,
            "roundtrip_seen": roundtrip_seen,
            "lane_round_trip_seen": lane_round_trip_seen,
            "target_y_sign_changes": y_sign_changes,
            "yaw_motion_deg": round(yaw_motion, 3),
            "pitch_motion_deg": round(pitch_motion, 3),
            "fire_yaw_motion_deg": round(fire_yaw_motion, 3),
            "fire_pitch_motion_deg": round(fire_pitch_motion, 3),
            "states": sorted(str(s) for s in states),
            "step_types": sorted(str(s) for s in step_types),
            "sample_count": len(statuses),
            "command_sample_count": len(own_statuses),
            "last_status": latest_summary(statuses),
        },
    )


def print_result(result: TestResult) -> None:
    print(f"[{result.status}] {result.name}")
    if result.message:
        print(f"  note: {result.message}")
    if result.evidence:
        print("  evidence=" + json.dumps(result.evidence, ensure_ascii=False, sort_keys=True))
    sys.stdout.flush()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run live MQTT E2E checks against a turret_fleet device.")
    parser.add_argument("turret_id", help="turret id, e.g. turret_2 or 2")
    parser.add_argument("--env-file", type=Path, default=DEFAULT_ENV_FILE)
    parser.add_argument("--host", help="MQTT broker host; env TURRET_FLEET_MQTT_HOST/TURRET_MQTT_HOST")
    parser.add_argument("--port", type=int, help="MQTT broker port; default/env 1883")
    parser.add_argument("--root", help="MQTT root; default/env battlebang")
    parser.add_argument("--username")
    parser.add_argument("--password")
    parser.add_argument("--profile-file", type=Path, help="default firmware/turret_fleet/profiles/<turret>.json")
    parser.add_argument("--patterns-file", type=Path, help="default firmware/turret_fleet/pattern_presets/<turret>.json")
    parser.add_argument("--skip-config", action="store_true", help="do not publish profile/pattern config before tests")
    parser.add_argument("--allow-live-fire", action="store_true", help="run fire and pattern tests that energize relays/ESC")
    parser.add_argument("--target-point", nargs=3, type=float, default=[0.0, -0.5, -0.6], metavar=("X", "Y", "Z"))
    parser.add_argument("--frame-id", default="boss_stage_v1")
    parser.add_argument("--fire-ms", type=int, default=2000)
    parser.add_argument("--pattern-ttl-ms", type=int, default=5000)
    parser.add_argument("--target-observe-s", type=float, default=8.0)
    parser.add_argument("--hold-observe-s", type=float, default=8.0)
    parser.add_argument("--home-observe-s", type=float, default=30.0)
    parser.add_argument("--fire-observe-s", type=float, default=16.0)
    parser.add_argument("--idle-observe-s", type=float, default=12.0)
    parser.add_argument("--dead-observe-s", type=float, default=35.0)
    parser.add_argument("--dead-tolerance-deg", type=float, default=5.0)
    parser.add_argument("--pattern-timeout-s", type=float, default=90.0)
    parser.add_argument("--pattern-prep-timeout-s", type=float, default=40.0)
    parser.add_argument("--min-motion-deg", type=float, default=0.5)
    parser.add_argument("--lane-fire-yaw-motion-deg", type=float, default=10.0)
    parser.add_argument("--timeout-s", type=float, default=5.0)
    parser.add_argument("--between-sleep-s", type=float, default=2.0, help="settle time between scenarios/patterns")
    parser.add_argument("--keep-going", action="store_true", help="continue after a failed scenario")
    parser.add_argument("--json-report", type=Path)
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    env = merged_env(args.env_file)
    turret_id = normalize_turret_id(args.turret_id)
    host = args.host or env_first(env, "TURRET_FLEET_MQTT_HOST", "TURRET_MQTT_HOST")
    if not host:
        parser.error("missing --host or TURRET_FLEET_MQTT_HOST/TURRET_MQTT_HOST")
    port = int(args.port or env_first(env, "TURRET_FLEET_MQTT_PORT", "TURRET_MQTT_PORT", default="1883") or "1883")
    root = args.root or env_first(env, "TURRET_FLEET_MQTT_ROOT", "TURRET_MQTT_ROOT", default="battlebang") or "battlebang"
    username = args.username or env_first(env, "TURRET_FLEET_MQTT_USERNAME", "TURRET_MQTT_USERNAME")
    password = args.password or env_first(env, "TURRET_FLEET_MQTT_PASSWORD", "TURRET_MQTT_PASSWORD")
    if args.profile_file is None:
        args.profile_file = PROJECT_ROOT / "firmware" / "turret_fleet" / "profiles" / f"{turret_id}.json"
    if args.patterns_file is None:
        args.patterns_file = PROJECT_ROOT / "firmware" / "turret_fleet" / "pattern_presets" / f"{turret_id}.json"

    command_topic = topic_for(root, turret_id, "command")
    config_topic = topic_for(root, turret_id, "config")
    status_topic = topic_for(root, turret_id, "status")

    results: list[TestResult] = []
    with MqttSession(host=host, port=port, username=username, password=password, timeout_s=args.timeout_s) as client:
        client.subscribe(status_topic)
        initial = collect_statuses(client, status_topic, timeout_s=12.0, stop_when=lambda _doc, seen: bool(seen))
        if not initial:
            raise E2EError(f"no status received on {status_topic}; verify turret is online and broker root is {root!r}")
        initial_summary = latest_summary(initial)
        preflight = TestResult("preflight:status", "PASS", {"status": initial_summary})
        results.append(preflight)
        print_result(preflight)
        if initial_summary.get("brownout_lockout"):
            brownout = TestResult("preflight:brownout", "FAIL", {"status": initial_summary}, "send recover/inspect before E2E")
            results.append(brownout)
            print_result(brownout)
            if not args.keep_going:
                raise E2EError("brownout lockout active")

        if not args.skip_config:
            profile_doc = load_runtime_profile(turret_id, args.profile_file, args.patterns_file)
            client.publish_json(config_topic, profile_doc)
            statuses = collect_statuses(
                client,
                status_topic,
                timeout_s=20.0,
                stop_when=lambda doc, _: doc.get("config_version") == profile_doc["config_version"],
            )
            passed = any(doc.get("config_version") == profile_doc["config_version"] for doc in statuses)
            config_result = TestResult(
                "config:profile+patterns",
                "PASS" if passed else "FAIL",
                {
                    "config_version": profile_doc["config_version"],
                    "profile_file": str(args.profile_file),
                    "patterns_file": str(args.patterns_file),
                    "last_status": latest_summary(statuses),
                },
            )
            results.append(config_result)
            print_result(config_result)
            if not passed and not args.keep_going:
                raise E2EError("profile/pattern config was not acknowledged")

        scenarios = [
            lambda: run_hold(client, command_topic, status_topic, observe_s=args.hold_observe_s),
            lambda: run_target(client, command_topic, status_topic, args),
            lambda: run_hold(client, command_topic, status_topic, observe_s=args.hold_observe_s),
            lambda: run_fire(client, command_topic, status_topic, args),
            lambda: run_idle(client, command_topic, status_topic, args),
            lambda: run_hold(client, command_topic, status_topic, observe_s=args.hold_observe_s),
            lambda: run_pattern_prep(client, command_topic, status_topic, E2E_PATTERN_IDS[0], args),
            lambda: run_pattern(client, command_topic, status_topic, E2E_PATTERN_IDS[0], args),
            lambda: run_hold(client, command_topic, status_topic, observe_s=args.hold_observe_s),
            lambda: run_pattern_prep(client, command_topic, status_topic, E2E_PATTERN_IDS[1], args),
            lambda: run_pattern(client, command_topic, status_topic, E2E_PATTERN_IDS[1], args),
            lambda: run_hold(client, command_topic, status_topic, observe_s=args.hold_observe_s),
            lambda: run_pattern_prep(client, command_topic, status_topic, E2E_PATTERN_IDS[2], args),
            lambda: run_pattern(client, command_topic, status_topic, E2E_PATTERN_IDS[2], args),
            lambda: run_hold(client, command_topic, status_topic, observe_s=args.hold_observe_s),
            lambda: run_dead(client, command_topic, status_topic, args),
            lambda: run_home(client, command_topic, status_topic, args),
            lambda: run_hold(client, command_topic, status_topic, observe_s=args.hold_observe_s),
        ]

        try:
            for index, scenario in enumerate(scenarios):
                result = scenario()
                results.append(result)
                print_result(result)
                if result.status == "FAIL" and not args.keep_going:
                    break
                if args.between_sleep_s > 0 and index < len(scenarios) - 1:
                    time.sleep(args.between_sleep_s)
        finally:
            # Always leave the turret in WAIT_COMMAND with stop PWM after a live run.
            try:
                final_hold = run_hold(client, command_topic, status_topic, observe_s=args.hold_observe_s)
                final_hold.name = "final-hold"
                results.append(final_hold)
                print_result(final_hold)
            except Exception:
                pass

    report = {
        "turret_id": turret_id,
        "host": host,
        "root": root,
        "allow_live_fire": args.allow_live_fire,
        "results": [result.__dict__ for result in results],
    }
    if args.json_report:
        args.json_report.parent.mkdir(parents=True, exist_ok=True)
        args.json_report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"json_report={args.json_report}")

    failed = [result for result in results if result.status == "FAIL"]
    skipped = [result for result in results if result.status == "SKIP"]
    print(f"summary: pass={sum(1 for r in results if r.status == 'PASS')} skip={len(skipped)} fail={len(failed)}")
    return 1 if failed else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except E2EError as exc:
        print(f"[fleet-e2e] error: {exc}", file=sys.stderr)
        raise SystemExit(2)
