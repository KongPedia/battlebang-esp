from __future__ import annotations

from dataclasses import dataclass
from enum import Enum, auto


class Mode(Enum):
    IDLE = auto()
    TARGET = auto()
    DEAD = auto()


class FireState(Enum):
    IDLE = auto()
    ACTIVE = auto()


@dataclass
class TurretStateSpec:
    """
    Python reference model for the state-transition rules currently implemented in
    src/turret/main.cpp.

    This intentionally mirrors the behavior of:
    - enterIdleMode()
    - enterDeadMode()
    - abortAndEnterIdleMode()
    - abortAndEnterDeadMode()
    - applyTargetCommand()
    - commandFire()
    """

    auto_fire_on_target: bool = True
    mode: Mode = Mode.IDLE
    fire_state: FireState = FireState.IDLE
    pending_fire_when_aim_reached: bool = False
    manual_fire_queued: bool = False
    fire_triggered_for_current_target: bool = False

    def clear_pending_fire_flags(self) -> None:
        self.pending_fire_when_aim_reached = False
        self.manual_fire_queued = False
        self.fire_triggered_for_current_target = False

    def abort_fire_sequence_and_safe_off(self) -> None:
        self.fire_state = FireState.IDLE
        self.clear_pending_fire_flags()

    def enter_idle_mode(self) -> None:
        self.mode = Mode.IDLE
        self.clear_pending_fire_flags()

    def enter_dead_mode(self) -> None:
        self.mode = Mode.DEAD
        self.clear_pending_fire_flags()

    def abort_and_enter_idle_mode(self) -> None:
        self.abort_fire_sequence_and_safe_off()
        self.enter_idle_mode()

    def abort_and_enter_dead_mode(self) -> None:
        self.abort_fire_sequence_and_safe_off()
        self.enter_dead_mode()

    def apply_target_command(self) -> str:
        if self.fire_state != FireState.IDLE:
            return "ignored_during_firing"

        self.mode = Mode.TARGET
        self.fire_triggered_for_current_target = False
        self.manual_fire_queued = False
        self.pending_fire_when_aim_reached = self.auto_fire_on_target
        return "entered_target"

    def command_fire(self, aim_reached: bool = True) -> str:
        if self.fire_state != FireState.IDLE:
            return "ignored_active_sequence"

        if self.mode == Mode.DEAD:
            return "ignored_in_dead_mode"

        if self.mode == Mode.TARGET and not aim_reached:
            self.manual_fire_queued = True
            self.pending_fire_when_aim_reached = False
            self.fire_triggered_for_current_target = False
            return "queued_until_aim_reached"

        self.fire_triggered_for_current_target = True
        self.manual_fire_queued = False
        self.pending_fire_when_aim_reached = False
        self.fire_state = FireState.ACTIVE
        return "started_immediately"


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
    assert turret.pending_fire_when_aim_reached is True


def test_target_to_dead_is_allowed_and_aborts_fire() -> None:
    turret = TurretStateSpec(mode=Mode.TARGET, fire_state=FireState.ACTIVE)

    turret.abort_and_enter_dead_mode()

    assert turret.mode == Mode.DEAD
    assert turret.fire_state == FireState.IDLE
    assert turret.pending_fire_when_aim_reached is False
    assert turret.manual_fire_queued is False


def test_target_to_idle_is_allowed_and_aborts_fire() -> None:
    turret = TurretStateSpec(mode=Mode.TARGET, fire_state=FireState.ACTIVE)

    turret.abort_and_enter_idle_mode()

    assert turret.mode == Mode.IDLE
    assert turret.fire_state == FireState.IDLE


def test_new_target_overrides_dead_mode_without_special_unfreeze_step() -> None:
    turret = TurretStateSpec(mode=Mode.DEAD)

    turret.apply_target_command()

    assert turret.mode == Mode.TARGET


def test_target_is_ignored_only_while_actively_firing() -> None:
    turret = TurretStateSpec(mode=Mode.DEAD, fire_state=FireState.ACTIVE)

    result = turret.apply_target_command()

    assert result == "ignored_during_firing"
    assert turret.mode == Mode.DEAD


def test_fire_is_still_blocked_in_dead_mode() -> None:
    turret = TurretStateSpec(mode=Mode.DEAD)

    result = turret.command_fire()

    assert result == "ignored_in_dead_mode"
    assert turret.fire_state == FireState.IDLE


def test_fire_from_target_can_be_queued_until_aim_is_reached() -> None:
    turret = TurretStateSpec(mode=Mode.TARGET)

    result = turret.command_fire(aim_reached=False)

    assert result == "queued_until_aim_reached"
    assert turret.manual_fire_queued is True
    assert turret.fire_triggered_for_current_target is False
