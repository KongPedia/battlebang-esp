from __future__ import annotations

from dataclasses import dataclass
from enum import Enum, auto


class Mode(Enum):
    HOLD = auto()
    IDLE = auto()
    TARGET = auto()
    DEAD = auto()


class FireState(Enum):
    IDLE = auto()
    SPINNING = auto()
    SHUTTING_DOWN = auto()


FIRE_KEEPALIVE_MS = 1000


@dataclass
class TurretStateSpec:
    """
    Reference model for the updated event-driven turret behavior.

    Mirrors the intended semantics in src/turret/main.cpp:
    - boot into HOLD (no auto idle sweep)
    - idle/dead/target only change on explicit command
    - target never auto-fires
    - fire is command-driven and extends a keepalive window
    - IDLE keeps searching while firing and remains IDLE after fire
    """

    mode: Mode = Mode.HOLD
    fire_state: FireState = FireState.IDLE
    manual_fire_queued: bool = False
    fire_triggered_for_current_target: bool = False
    fire_keepalive_until_ms: int = 0
    fire_restart_requested: bool = False
    now_ms: int = 0
    post_fire_mode: Mode = Mode.HOLD

    def clear_pending_fire_flags(self) -> None:
        self.manual_fire_queued = False
        self.fire_triggered_for_current_target = False

    def advance(self, ms: int) -> None:
        self.now_ms += ms

    def enter_hold_mode(self) -> None:
        self.mode = Mode.HOLD
        self.clear_pending_fire_flags()

    def enter_idle_mode(self) -> None:
        self.mode = Mode.IDLE
        self.clear_pending_fire_flags()

    def enter_dead_mode(self) -> None:
        self.mode = Mode.DEAD
        self.clear_pending_fire_flags()

    def abort_and_enter_idle_mode(self) -> None:
        self.fire_state = FireState.IDLE
        self.fire_keepalive_until_ms = 0
        self.fire_restart_requested = False
        self.enter_idle_mode()

    def abort_and_enter_dead_mode(self) -> None:
        self.fire_state = FireState.IDLE
        self.fire_keepalive_until_ms = 0
        self.fire_restart_requested = False
        self.enter_dead_mode()

    def apply_target_command(self) -> str:
        if self.fire_state != FireState.IDLE:
            return "ignored_during_firing"

        self.mode = Mode.TARGET
        self.manual_fire_queued = False
        self.fire_triggered_for_current_target = False
        return "entered_target"

    def _begin_fire_sequence(self) -> None:
        self.post_fire_mode = self.mode
        self.fire_state = FireState.SPINNING
        self.fire_keepalive_until_ms = self.now_ms + FIRE_KEEPALIVE_MS
        self.fire_restart_requested = False

    def command_fire(self, *, aim_reached: bool = True) -> str:
        if self.mode == Mode.DEAD:
            return "ignored_in_dead_mode"

        if self.fire_state == FireState.SPINNING:
            self.fire_keepalive_until_ms = self.now_ms + FIRE_KEEPALIVE_MS
            return "keepalive_refreshed"

        if self.fire_state == FireState.SHUTTING_DOWN:
            self.fire_keepalive_until_ms = self.now_ms + FIRE_KEEPALIVE_MS
            self.fire_restart_requested = True
            return "restart_queued"

        self.manual_fire_queued = False
        self.fire_triggered_for_current_target = True
        self._begin_fire_sequence()
        return "started_immediately"

    def on_fire_keepalive_expired(self) -> str:
        self.fire_state = FireState.SHUTTING_DOWN
        return "shutdown_started"

    def on_fire_shutdown_complete(self) -> str:
        self.fire_state = FireState.IDLE
        self.fire_keepalive_until_ms = 0
        self.clear_pending_fire_flags()

        if self.fire_restart_requested:
            self.fire_restart_requested = False
            self._begin_fire_sequence()
            return "restarted"

        self.mode = self.post_fire_mode
        return "settled"


def test_boot_mode_is_hold_not_idle() -> None:
    turret = TurretStateSpec()

    assert turret.mode == Mode.HOLD


def test_dead_to_idle_is_allowed() -> None:
    turret = TurretStateSpec(mode=Mode.DEAD)

    turret.abort_and_enter_idle_mode()

    assert turret.mode == Mode.IDLE
    assert turret.fire_state == FireState.IDLE


def test_dead_to_target_is_allowed() -> None:
    turret = TurretStateSpec(mode=Mode.DEAD)

    result = turret.apply_target_command()

    assert result == "entered_target"
    assert turret.mode == Mode.TARGET


def test_target_never_auto_fires() -> None:
    turret = TurretStateSpec(mode=Mode.HOLD)

    turret.apply_target_command()

    assert turret.mode == Mode.TARGET
    assert turret.manual_fire_queued is False
    assert turret.fire_state == FireState.IDLE


def test_fire_from_target_starts_immediately_even_before_aim_reached() -> None:
    turret = TurretStateSpec(mode=Mode.TARGET)

    result = turret.command_fire(aim_reached=False)

    assert result == "started_immediately"
    assert turret.manual_fire_queued is False
    assert turret.fire_triggered_for_current_target is True
    assert turret.fire_state == FireState.SPINNING


def test_fire_keepalive_refreshes_while_firing() -> None:
    turret = TurretStateSpec(mode=Mode.TARGET)
    turret.command_fire(aim_reached=True)
    first_deadline = turret.fire_keepalive_until_ms

    turret.advance(1000)
    result = turret.command_fire()

    assert result == "keepalive_refreshed"
    assert turret.fire_keepalive_until_ms == turret.now_ms + FIRE_KEEPALIVE_MS
    assert turret.fire_keepalive_until_ms > first_deadline


def test_fire_does_not_auto_return_to_idle_after_target_mode() -> None:
    turret = TurretStateSpec(mode=Mode.TARGET)
    turret.command_fire(aim_reached=True)

    turret.on_fire_keepalive_expired()
    result = turret.on_fire_shutdown_complete()

    assert result == "settled"
    assert turret.mode == Mode.TARGET


def test_fire_from_idle_keeps_idle_search_after_fire() -> None:
    turret = TurretStateSpec(mode=Mode.IDLE)
    turret.command_fire(aim_reached=True)

    turret.on_fire_keepalive_expired()
    turret.on_fire_shutdown_complete()

    assert turret.mode == Mode.IDLE


def test_fire_in_dead_mode_is_blocked() -> None:
    turret = TurretStateSpec(mode=Mode.DEAD)

    result = turret.command_fire()

    assert result == "ignored_in_dead_mode"
    assert turret.fire_state == FireState.IDLE


def test_fire_restart_can_be_queued_while_shutting_down() -> None:
    turret = TurretStateSpec(mode=Mode.TARGET)
    turret.command_fire(aim_reached=True)
    turret.on_fire_keepalive_expired()

    result = turret.command_fire()
    assert result == "restart_queued"

    result = turret.on_fire_shutdown_complete()
    assert result == "restarted"
    assert turret.fire_state == FireState.SPINNING
