#!/usr/bin/env python3
"""BattleBang live lane_sweep director for turret_1, turret_3, and turret_4.

Normal cycle:
1. Build one random shuffled bag of the alive turrets.
2. Publish lane_sweep loop=1 to the next turret in that bag.
3. Wait until that turret completes before commanding the next turret.
4. Repeat forever with a newly shuffled bag each cycle.

Death handling:
- If any turret reports mode=DEAD, or its linked hit target reports destroyed/HP<=0,
  skip that turret so the script never waits forever on a dead target.
- If only one turret remains alive, command only that turret with lane_sweep loop=1
  and immediately repeat after completion.
- If all are dead, no commands are sent; the script polls status periodically.
"""
from __future__ import annotations

import argparse
import json
import random
import sys
import time
from pathlib import Path
from typing import Any

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

from e2e_mqtt_test import E2EError, MqttSession  # noqa: E402
from mqtt_command import (  # noqa: E402
    DEFAULT_ENV_FILE,
    MqttCommandError,
    env_first,
    merged_env,
    normalize_turret_id,
    topic_for,
)

TERMINAL_MODES = {"WAIT_COMMAND", "DEAD"}
TERMINAL_PATTERN_STATES = {"IDLE", "DONE", ""}
DEFAULT_TURRETS = ["turret_1", "turret_3", "turret_4"]


def clean_root(root: str) -> str:
    return root.strip("/") or "battlebang"


class StatusMonitor:
    def __init__(self, *, root: str, turrets: list[str]) -> None:
        self.root = clean_root(root)
        self.turrets = set(turrets)
        self.turret_topics = {topic_for(self.root, turret_id, "status"): turret_id for turret_id in turrets}
        self.latest_turret: dict[str, dict[str, Any]] = {}
        self.latest_hit_target: dict[str, dict[str, Any]] = {}

    def subscribe_all(self, client: MqttSession) -> None:
        for topic in self.turret_topics:
            client.subscribe(topic)
        client.subscribe(f"{self.root}/hit_targets/+/status")

    def process_publish(self, topic: str, payload: str) -> dict[str, Any] | None:
        try:
            doc = json.loads(payload)
        except json.JSONDecodeError:
            return None
        turret_id = self.turret_topics.get(topic)
        if turret_id is not None:
            self.latest_turret[turret_id] = doc
            return doc
        if topic.startswith(f"{self.root}/hit_targets/") and topic.endswith("/status"):
            linked = str(doc.get("linked_device_id") or doc.get("linked_turret_id") or "")
            if linked in self.turrets:
                self.latest_hit_target[linked] = doc
                return doc
        return None

    def pump_once(self, client: MqttSession, *, timeout_s: float = 0.5) -> tuple[str, dict[str, Any]] | None:
        item = client.read_publish(deadline=time.time() + timeout_s)
        if item is None:
            return None
        topic, payload = item
        doc = self.process_publish(topic, payload)
        if doc is None:
            return None
        return topic, doc

    def warmup(self, client: MqttSession, *, duration_s: float) -> None:
        deadline = time.time() + duration_s
        while time.time() < deadline:
            self.pump_once(client, timeout_s=min(0.5, max(0.0, deadline - time.time())))

    def dead_reason(self, turret_id: str) -> str | None:
        turret = self.latest_turret.get(turret_id, {})
        if str(turret.get("mode") or "") == "DEAD":
            return "turret_status mode=DEAD"
        hit_target = self.latest_hit_target.get(turret_id, {})
        if bool(hit_target.get("destroyed")):
            target_id = hit_target.get("target_id") or hit_target.get("device_id") or "unknown"
            hp = hit_target.get("hp_remaining")
            return f"hit_target target_id={target_id} destroyed=true hp_remaining={hp}"
        hp = hit_target.get("hp_remaining")
        try:
            if hp is not None and int(hp) <= 0:
                target_id = hit_target.get("target_id") or hit_target.get("device_id") or "unknown"
                return f"hit_target target_id={target_id} hp_remaining={hp}"
        except (TypeError, ValueError):
            return None
        return None

    def is_dead(self, turret_id: str) -> bool:
        return self.dead_reason(turret_id) is not None

    def alive_in_order(self, turrets: list[str]) -> list[str]:
        return [turret_id for turret_id in turrets if not self.is_dead(turret_id)]

    def dead_in_order(self, turrets: list[str]) -> list[str]:
        return [turret_id for turret_id in turrets if self.is_dead(turret_id)]


def build_payload(args: argparse.Namespace, turret_id: str, cycle: int, *, loop: int) -> dict[str, Any]:
    now_ms = int(time.time() * 1000)
    command_id = f"seq-lane-sweep-{turret_id}-{cycle}-{now_ms}"
    params: dict[str, Any] = {
        "loop": loop,
        "move_timeout_ms": args.move_timeout_ms,
        "return_to": args.return_to,
        "ping_pong": True,
    }
    if args.dwell_ms is not None:
        params["dwell_ms"] = args.dwell_ms
    if args.fire_ms is not None:
        params["fire_ms"] = args.fire_ms
    return {
        "command": "pattern",
        "command_id": command_id,
        "pattern_id": "lane_sweep",
        "pattern_instance_id": command_id,
        "frame_id": args.frame_id,
        "ttl_ms": args.ttl_ms,
        "issued_at_ms": now_ms,
        "params": params,
    }


def status_belongs_to_command(doc: dict[str, Any], command_id: str) -> bool:
    return (
        doc.get("active_command_id") == command_id
        or doc.get("last_command_id") == command_id
        or doc.get("pattern_instance_id") == command_id
    )


def is_started_status(doc: dict[str, Any], command_id: str) -> bool:
    if status_belongs_to_command(doc, command_id):
        return True
    return doc.get("mode") == "PATTERN" and doc.get("pattern_id") == "lane_sweep"


def is_done_status(doc: dict[str, Any], command_id: str) -> bool:
    if not status_belongs_to_command(doc, command_id):
        return False
    mode = str(doc.get("mode") or "")
    command_state = str(doc.get("command_state") or "")
    pattern_state = str(doc.get("pattern_state") or "")
    return (
        mode in TERMINAL_MODES
        and command_state in {"ready", "blocked", ""}
        and pattern_state in TERMINAL_PATTERN_STATES
        and not bool(doc.get("command_in_progress"))
        and not bool(doc.get("activation_active"))
        and not bool(doc.get("hit_target_active"))
    )


def summarize_status(doc: dict[str, Any]) -> str:
    fields = [
        f"mode={doc.get('mode')}",
        f"command_state={doc.get('command_state')}",
        f"pattern_state={doc.get('pattern_state')}",
        f"fire_state={doc.get('fire_state')}",
        f"active={doc.get('activation_active')}",
        f"hit_target_active={doc.get('hit_target_active')}",
    ]
    err = doc.get("last_error")
    if err:
        fields.append(f"last_error={err}")
    return " ".join(fields)


def wait_for_command(
    client: MqttSession,
    monitor: StatusMonitor,
    *,
    turret_id: str,
    command_id: str,
    start_timeout_s: float,
    command_timeout_s: float,
) -> str:
    start_deadline = time.time() + start_timeout_s
    deadline = time.time() + command_timeout_s
    started = False
    last_printed: tuple[Any, ...] | None = None
    last_doc: dict[str, Any] = {}

    while time.time() < deadline:
        item = monitor.pump_once(client, timeout_s=1.0)
        if monitor.is_dead(turret_id):
            print(f"dead turret={turret_id} reason={monitor.dead_reason(turret_id)}; stop waiting for command_id={command_id}", flush=True)
            return "dead"
        if item is None:
            if not started and time.time() > start_deadline:
                raise MqttCommandError(f"{turret_id} did not start command {command_id} within {start_timeout_s}s")
            continue
        _topic, doc = item
        if doc.get("turret_id") != turret_id:
            continue
        last_doc = doc
        if not started and is_started_status(doc, command_id):
            started = True
            print(f"started turret={turret_id} command_id={command_id} {summarize_status(doc)}", flush=True)
        if started:
            key = (doc.get("mode"), doc.get("command_state"), doc.get("pattern_state"), doc.get("fire_state"), doc.get("activation_active"))
            if key != last_printed:
                print(f"status turret={turret_id} {summarize_status(doc)}", flush=True)
                last_printed = key
            if is_done_status(doc, command_id):
                print(f"done turret={turret_id} command_id={command_id} {summarize_status(doc)}", flush=True)
                return "done"
        elif time.time() > start_deadline:
            raise MqttCommandError(f"{turret_id} did not start command {command_id}; latest={summarize_status(last_doc)}")
    raise MqttCommandError(f"{turret_id} command {command_id} did not complete within {command_timeout_s}s; latest={summarize_status(last_doc)}")


def publish_one(
    client: MqttSession,
    monitor: StatusMonitor,
    args: argparse.Namespace,
    *,
    root: str,
    turret_id: str,
    cycle: int,
    loop: int,
) -> str:
    if monitor.is_dead(turret_id):
        print(f"skip dead turret={turret_id} reason={monitor.dead_reason(turret_id)}", flush=True)
        return "dead"
    command_topic = topic_for(root, turret_id, "command")
    payload = build_payload(args, turret_id, cycle, loop=loop)
    command_id = str(payload["command_id"])
    print(f"publish turret={turret_id} loop={loop} topic={command_topic} command_id={command_id}", flush=True)
    client.publish_json(command_topic, payload)
    return wait_for_command(
        client,
        monitor,
        turret_id=turret_id,
        command_id=command_id,
        start_timeout_s=args.start_timeout_s,
        command_timeout_s=args.command_timeout_s,
    )


def publish_parallel(
    client: MqttSession,
    monitor: StatusMonitor,
    args: argparse.Namespace,
    *,
    root: str,
    turrets: list[str],
    cycle: int,
    loop: int,
) -> None:
    command_ids: dict[str, str] = {}
    started: set[str] = set()
    done: set[str] = set()
    start_deadline = time.time() + args.start_timeout_s
    deadline = time.time() + args.command_timeout_s
    last_printed: dict[str, tuple[Any, ...]] = {}

    for turret_id in turrets:
        if monitor.is_dead(turret_id):
            print(f"skip dead turret={turret_id} reason={monitor.dead_reason(turret_id)}", flush=True)
            done.add(turret_id)
            continue
        command_topic = topic_for(root, turret_id, "command")
        payload = build_payload(args, turret_id, cycle, loop=loop)
        command_id = str(payload["command_id"])
        command_ids[turret_id] = command_id
        print(f"publish parallel turret={turret_id} loop={loop} topic={command_topic} command_id={command_id}", flush=True)
        client.publish_json(command_topic, payload)

    pending = set(command_ids)
    while pending and time.time() < deadline:
        item = monitor.pump_once(client, timeout_s=1.0)
        for turret_id in list(pending):
            if monitor.is_dead(turret_id):
                print(f"dead turret={turret_id} reason={monitor.dead_reason(turret_id)}; stop waiting for parallel command", flush=True)
                pending.remove(turret_id)
                done.add(turret_id)
        if item is None:
            if pending and time.time() > start_deadline:
                missing = sorted(pending - started)
                if missing:
                    raise MqttCommandError(f"parallel commands did not start: {', '.join(missing)}")
            continue
        _topic, doc = item
        turret_id = str(doc.get("turret_id") or "")
        if turret_id not in pending:
            continue
        command_id = command_ids[turret_id]
        if turret_id not in started and is_started_status(doc, command_id):
            started.add(turret_id)
            print(f"started parallel turret={turret_id} command_id={command_id} {summarize_status(doc)}", flush=True)
        if turret_id in started:
            key = (doc.get("mode"), doc.get("command_state"), doc.get("pattern_state"), doc.get("fire_state"), doc.get("activation_active"))
            if key != last_printed.get(turret_id):
                print(f"status parallel turret={turret_id} {summarize_status(doc)}", flush=True)
                last_printed[turret_id] = key
            if is_done_status(doc, command_id):
                print(f"done parallel turret={turret_id} command_id={command_id} {summarize_status(doc)}", flush=True)
                pending.remove(turret_id)
                done.add(turret_id)
    if pending:
        raise MqttCommandError(f"parallel commands did not complete within {args.command_timeout_s}s: {', '.join(sorted(pending))}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Random one-at-a-time live-fire lane_sweep director for turret_1, turret_3, and turret_4.")
    parser.add_argument("--env-file", default=str(DEFAULT_ENV_FILE), help="default: src/turret_fleet/.env.turret_fleet")
    parser.add_argument("--host", help="MQTT broker host; default from env file")
    parser.add_argument("--port", type=int, help="MQTT broker port; default 1883")
    parser.add_argument("--root", help="MQTT root; default battlebang")
    parser.add_argument("--username", help="MQTT username")
    parser.add_argument("--password", help="MQTT password")
    parser.add_argument("--timeout-s", type=float, default=5.0)
    parser.add_argument("--turret", action="append", dest="turrets", help="repeatable; default turret_1, turret_3, then turret_4")
    parser.add_argument("--frame-id", default="boss_stage_v1")
    parser.add_argument("--single-loop", type=int, default=1, help="loop count for one-at-a-time and solo-dead mode")
    parser.add_argument("--parallel-loop", type=int, default=0, help="loop count for optional parallel phase; 0 disables parallel and keeps one-at-a-time flow")
    parser.add_argument("--sequential-rounds", type=int, default=1, help="number of one-at-a-time shuffled bags before optional parallel")
    parser.add_argument("--random-order", action=argparse.BooleanOptionalAction, default=True, help="shuffle turret order each sequential round; use --no-random-order for listed order")
    parser.add_argument("--random-seed", type=int, help="deterministic shuffle seed for dry-run/testing")
    parser.add_argument("--dwell-ms", type=int, help="override dwell before fire; omit to use turret preset/NVS")
    parser.add_argument("--move-timeout-ms", type=int, default=20000)
    parser.add_argument("--fire-ms", type=int, help="override fire duration; omit to use turret preset/default")
    parser.add_argument("--ttl-ms", type=int, default=120000)
    parser.add_argument("--return-to", choices=["wait_command", "idle"], default="wait_command")
    parser.add_argument("--single-delay-s", type=float, default=0.0, help="optional sleep after each one-at-a-time command completes")
    parser.add_argument("--dead-poll-s", type=float, default=5.0, help="sleep when all turrets are dead/no-command state")
    parser.add_argument("--count", type=int, default=0, help="full director cycles; 0 means forever")
    parser.add_argument("--start-timeout-s", type=float, default=20.0)
    parser.add_argument("--command-timeout-s", type=float, default=180.0)
    parser.add_argument("--dry-run", action="store_true", help="print planned sequence without publishing/subscribing")
    return parser


def sleep_with_pump(client: MqttSession, monitor: StatusMonitor, seconds: float) -> None:
    deadline = time.time() + max(0.0, seconds)
    while time.time() < deadline:
        monitor.pump_once(client, timeout_s=min(0.5, max(0.0, deadline - time.time())))


def sequential_round_order(turrets: list[str], *, random_order: bool, rng: random.Random) -> list[str]:
    order = list(turrets)
    if random_order and len(order) > 1:
        rng.shuffle(order)
    return order


def dry_run(args: argparse.Namespace, root: str, turrets: list[str]) -> None:
    print("dry-run normal cycle:")
    cycle = 1
    rng = random.Random(args.random_seed)
    for round_index in range(args.sequential_rounds):
        order = sequential_round_order(turrets, random_order=args.random_order, rng=rng)
        print(f"  sequential round={round_index + 1} order={','.join(order)}")
        for turret_id in order:
            payload = build_payload(args, turret_id, cycle, loop=args.single_loop)
            print(f"    turret={turret_id} topic={topic_for(root, turret_id, 'command')} payload={json.dumps(payload, ensure_ascii=False, separators=(',', ':'))}")
    if args.parallel_loop > 0:
        for turret_id in turrets:
            payload = build_payload(args, turret_id, cycle, loop=args.parallel_loop)
            print(f"  parallel turret={turret_id} topic={topic_for(root, turret_id, 'command')} payload={json.dumps(payload, ensure_ascii=False, separators=(',', ':'))}")
    else:
        print("  parallel disabled; next live cycle starts another shuffled one-at-a-time bag immediately")
    print("dry-run dead mode: if one turret is dead, only alive turret gets immediate single-loop lane_sweep repeats")


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    env = merged_env(Path(args.env_file))
    host = args.host or env_first(env, "TURRET_FLEET_MQTT_HOST", "TURRET_MQTT_HOST")
    if not host and not args.dry_run:
        raise MqttCommandError("missing --host or TURRET_FLEET_MQTT_HOST/TURRET_MQTT_HOST")
    port = int(args.port or env_first(env, "TURRET_FLEET_MQTT_PORT", "TURRET_MQTT_PORT", default="1883") or "1883")
    root = clean_root(args.root or env_first(env, "TURRET_FLEET_MQTT_ROOT", "TURRET_MQTT_ROOT", default="battlebang") or "battlebang")
    username = args.username or env_first(env, "TURRET_FLEET_MQTT_USERNAME", "TURRET_MQTT_USERNAME")
    password = args.password or env_first(env, "TURRET_FLEET_MQTT_PASSWORD", "TURRET_MQTT_PASSWORD")
    turrets = [normalize_turret_id(t) for t in (args.turrets or DEFAULT_TURRETS)]

    print(
        f"lane_sweep director host={host or '<dry-run>'}:{port} root={root} turrets={','.join(turrets)} "
        f"order={'random' if args.random_order else 'listed'} sequential_rounds={args.sequential_rounds} "
        f"single_loop={args.single_loop} parallel_loop={args.parallel_loop or 'disabled'} count={args.count or 'forever'}",
        flush=True,
    )
    if args.dry_run:
        dry_run(args, root, turrets)
        return 0

    assert host is not None
    try:
        with MqttSession(host=host, port=port, username=username, password=password, timeout_s=args.timeout_s) as client:
            monitor = StatusMonitor(root=root, turrets=turrets)
            monitor.subscribe_all(client)
            monitor.warmup(client, duration_s=2.0)
            director_cycle = 0
            rng = random.Random(args.random_seed)
            while True:
                if args.count and director_cycle >= args.count:
                    break
                director_cycle += 1
                alive = monitor.alive_in_order(turrets)
                dead = monitor.dead_in_order(turrets)
                print(f"cycle={director_cycle} alive={','.join(alive) or '-'} dead={','.join(dead) or '-'}", flush=True)

                if len(alive) == 0:
                    print(f"all turrets dead; no command; sleep {args.dead_poll_s}s", flush=True)
                    sleep_with_pump(client, monitor, args.dead_poll_s)
                    continue

                if len(alive) == 1:
                    publish_one(client, monitor, args, root=root, turret_id=alive[0], cycle=director_cycle, loop=args.single_loop)
                    sleep_with_pump(client, monitor, args.single_delay_s)
                    continue

                for round_index in range(args.sequential_rounds):
                    alive_round = monitor.alive_in_order(turrets)
                    if not alive_round:
                        print("no alive turrets left during sequential round", flush=True)
                        break
                    round_order = sequential_round_order(alive_round, random_order=args.random_order, rng=rng)
                    print(f"sequential round {round_index + 1}/{args.sequential_rounds} order={','.join(round_order)}", flush=True)
                    for turret_id in round_order:
                        if monitor.is_dead(turret_id):
                            print(f"skip dead turret={turret_id} reason={monitor.dead_reason(turret_id)}", flush=True)
                            continue
                        publish_one(client, monitor, args, root=root, turret_id=turret_id, cycle=director_cycle, loop=args.single_loop)
                        sleep_with_pump(client, monitor, args.single_delay_s)

                alive = monitor.alive_in_order(turrets)
                if args.parallel_loop <= 0:
                    continue

                if len(alive) < 2:
                    print(f"skip parallel; alive={','.join(alive) or '-'}", flush=True)
                    continue

                print(f"parallel lane_sweep loop={args.parallel_loop} turrets={','.join(alive)}", flush=True)
                publish_parallel(client, monitor, args, root=root, turrets=alive, cycle=director_cycle, loop=args.parallel_loop)
    except E2EError as exc:
        raise MqttCommandError(str(exc)) from exc
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("stopped")
        raise SystemExit(130)
    except MqttCommandError as exc:
        print(f"[repeat-lane-sweep-live] {exc}", file=sys.stderr)
        raise SystemExit(2)
