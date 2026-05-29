from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_legacy_turret_boots_hold_not_idle_sweep() -> None:
    main = read("src/turret/main.cpp")
    setup_body = main.split("void setup()", 1)[1].split("void loop()", 1)[0]

    assert "clearPendingFireFlags();\n  enterHoldMode();" in setup_body
    assert "clearPendingFireFlags();\n  enterIdleMode();" not in setup_body


def test_fleet_control_boots_wait_command_then_initial_local_home() -> None:
    control = read("src/turret_fleet/control/turret_control.cpp")
    main = read("src/turret_fleet/main.cpp")
    mqtt_h = read("src/turret_fleet/mqtt/mqtt_bus.h")

    assert 'mode_ = config.configured ? "WAIT_COMMAND" : "UNCONFIGURED";' in control
    assert 'mode_ = config.configured ? "IDLE" : "UNCONFIGURED";' not in control
    assert "runs initial local home without MQTT dependency" in control
    assert "void TurretControl::enterBootInitialTarget(bool motionAllowed)" in control
    assert "BOOT_HOME(home_0_0)" in control
    assert 'mode_ = "HOME"' in control
    assert "Motion tracking" in control
    assert "Auto fire on target: DISABLED" in control
    assert 'startNetwork("boot_forced")' in main
    assert 'runBootInitialTargetIfNeeded("setup_local_boot")' in main
    assert 'runBootInitialTargetIfNeeded("mqtt_ready_fallback")' in main
    assert "mqtt.connected()" in main
    assert "control.enterBootInitialTarget(bootInitialTargetMotionAllowed)" in main
    assert "boot_initial_target_inhibited" in main
    assert "bool connected();" in mqtt_h


def test_default_runtime_config_has_no_compiled_broker_and_schema_2() -> None:
    header = read("src/turret_fleet/config/runtime_config.h")

    assert "uint16_t schema = 2;" in header
    assert 'String mqttHost = "";' in header
    assert "uint16_t mqttPort = 1883;" in header
    assert 'String frameId = "boss_stage_v1";' in header
    assert 'String mqttTargetUnit = "m";' in header


def test_serial_supports_first_provisioning_and_debug_commands() -> None:
    main = read("src/turret_fleet/main.cpp")

    assert 'Serial.println("  provision {json}");' in main
    assert 'Serial.println("  command {json}");' in main
    assert 'Serial.println("  start-network");' in main
    assert 'line.startsWith("provision ")' in main
    assert 'line == "show-status" || line == "status" || line == "debug"' in main
    assert "printStatus(\"serial_debug\")" in main


def test_fleet_dotenv_upload_provisioning_supports_turret_2_without_committing_secrets() -> None:
    helper = read("bin/turret")
    provision = read("scripts/turret_fleet/provision.py")
    gitignore = read(".gitignore")
    env_example = read("src/turret_fleet/.env.turret_fleet.example")

    assert "fleet-upload" in helper
    assert "fleet-provision" in helper
    assert "esp32dev_turret_fleet" in helper
    assert "src/turret_fleet/.env.turret_fleet" in gitignore
    assert "TURRET_FLEET_WIFI_PASSWORD=YOUR_WIFI_PASSWORD" in env_example
    assert "TURRET_FLEET_NETWORK_AUTO_START=true" in env_example
    assert "Keep false while power/servo brownout" not in env_example
    assert "TURRET_FLEET_YAW_STOP_US=1500" in env_example
    assert "TURRET_FLEET_PITCH_STOP_US=1500" in env_example
    assert "TURRET_FLEET_AXIS_SWITCH_COOLDOWN_MS=800" in env_example
    assert "def build_config" in provision
    assert "normalize_turret_id" in provision
    assert '"turret_id": turret_id' in provision
    assert "TURRET_FLEET_MQTT_HOST" in provision
    assert 'default="true"' in provision
    assert '"yaw_stop_us": yaw_stop_us' in provision
    assert "provision {payload}" in provision


def test_fleet_target_contract_rejects_frame_mismatch_and_converts_meters() -> None:
    control = read("src/turret_fleet/control/turret_control.cpp")

    assert "frame_id mismatch" in control
    assert "return mqttTargetsInMeters(config_) ? value * 100.0f : value;" in control
    assert "Auto fire on target: DISABLED" in control
    assert "computeYawDeg" in control
    assert "computePitchDeg" in control
    assert "setTrackedTarget(clampedYawDeg_, clampedPitchDeg_)" in control
    assert "ensureYawAttached" in control
    assert "ensurePitchAttached" in control
    assert "detachYawOutput" in control
    assert "detachPitchOutput" in control


def test_fleet_supports_direct_yaw_pitch_aim_for_axis_debugging() -> None:
    control = read("src/turret_fleet/control/turret_control.cpp")
    header = read("src/turret_fleet/control/turret_control.h")

    assert "applyDirectAimCommand" in header
    assert 'strcmp(command, "aim") == 0' in control
    assert 'strcmp(command, "manual_aim") == 0' in control
    assert "setTrackedTarget(clampedYawDeg_, clampedPitchDeg_)" in control
    assert 'doc["yaw_deg"].is<float>()' in control
    assert 'doc["pitch_deg"].is<float>()' in control
    assert "Direct aim is local turret yaw/pitch" in control


def test_fleet_supports_mqtt_home_init_command_for_local_zeroing() -> None:
    control = read("src/turret_fleet/control/turret_control.cpp")
    header = read("src/turret_fleet/control/turret_control.h")
    helper = read("scripts/turret_fleet/mqtt_command.py")

    assert "applyHomeCommand" in header
    assert 'strcmp(command, "home") == 0' in control
    assert 'strcmp(command, "init") == 0' in control
    assert 'strcmp(command, "initiate") == 0' in control
    assert "Home/init is local turret yaw/pitch; no target coordinate solve" in control
    assert "setTrackedTarget(clampedYawDeg_, clampedPitchDeg_)" in control
    assert 'mode_ = "HOME"' in control
    assert 'for action in ("idle", "dead", "hold", "wait", "home", "init", "initiate", "recover")' in helper
    assert 'elif action in {"init", "initiate"}' in helper
    assert 'command = "home"' in helper


def test_fleet_supports_bounded_jog_for_yaw_wrap_debugging() -> None:
    control = read("src/turret_fleet/control/turret_control.cpp")
    header = read("src/turret_fleet/control/turret_control.h")
    helper = read("scripts/turret_fleet/mqtt_command.py")

    assert "applyJogCommand" in header
    assert 'strcmp(command, "jog") == 0' in control
    assert "debug_jog" in control
    assert "delta_raw_wrap" in control
    assert "wrap_possible" in control
    assert "bounded debug jog still allowed" in control
    assert "kUnsafeManualCalibrationMode ? 400 : axisMaxDelta" in control
    assert "kUnsafeManualCalibrationMode ? 1200UL : 100UL" in control
    assert "detach_after" in control
    assert "servo left attached at stop PWM for debug hold" in control
    assert "yaw feedback rail guard" in control
    assert "kYawContinuousFeedback" in control
    assert "kKeepMotionServosAttachedAtStop" in control
    assert "yaw boot probe blocked: feedback outside calibrated 150deg safe envelope" in control
    assert "kJogAttachSettleMs" in control
    assert 'sub.add_parser("jog"' in helper
    assert '"command": "jog"' in helper


def test_mqtt_status_exposes_alignment_and_safe_state_fields() -> None:
    bus = read("src/turret_fleet/mqtt/mqtt_bus.cpp")
    control = read("src/turret_fleet/control/turret_control.cpp")

    assert "control_->appendStatus" in bus
    for field in [
        'doc["frame_id"]',
        'doc["pattern_state"]',
        'doc["fire_state"]',
        'doc["fire_sequence"]',
        'doc["last_error"]',
        'doc.createNestedObject("fire_output_state")',
        'fire["esc_command_us"]',
        'fire["relay_ch2_on"]',
        'fire["pending_fire"]',
        'fire["aim_stable_ms"]',
        'doc.createNestedObject("motion_state")',
        'doc.createNestedObject("motion_config")',
        'motion["yaw_raw"]',
        'motion["pitch_current_deg"]',
        'motion["yaw_goal_deg"]',
        'motion["target_slew_active"]',
        'motionConfig["yaw_stop_us"]',
        'motionConfig["pitch_stop_us"]',
        'motionConfig["servo_max_delta_us"]',
        'motionConfig["yaw_max_delta_us"]',
        'motionConfig["pitch_max_delta_us"]',
        'motionConfig["axis_switch_cooldown_ms"]',
        'motionConfig["yaw_soft_low_raw"]',
        'motionConfig["yaw_soft_high_raw"]',
        'motionConfig["pitch_soft_low_raw"]',
        'motionConfig["pitch_soft_high_raw"]',
        'motion["selected_axis"]',
        'motion["locked_axis"]',
        'motion["safety_inhibited"]',
        'motion["yaw_tracking_suppressed"]',
        'aim.createNestedObject("last_target_cm")',
        'aim["solved_yaw_deg"]',
        'aim["clamped_pitch_deg"]',
    ]:
        assert field in control


def test_mqtt_config_update_can_change_wifi_or_broker_after_first_provisioning() -> None:
    bus = read("src/turret_fleet/mqtt/mqtt_bus.cpp")

    assert "wifiChanged" in bus
    assert "mqttChanged" in bus
    assert "wifi_->begin(*config_)" in bus
    assert "reconfigure();" in bus


def test_network_autostart_is_forced_after_local_boot_initial_target() -> None:
    main = read("src/turret_fleet/main.cpp")
    config_h = read("src/turret_fleet/config/runtime_config.h")
    config_cpp = read("src/turret_fleet/config/runtime_config.cpp")
    wifi = read("src/turret_fleet/net/wifi_manager.cpp")

    assert "uint32_t networkStartDelayMs = 10000;" in config_h
    assert "bool networkAutoStart = true;" in config_h
    assert 'network["auto_start"]' in config_cpp
    assert 'network["start_delay_ms"]' in config_cpp
    assert "boot auto-network is forced" in main
    assert 'line == "start-network"' in main
    assert 'line == "stop-network"' in main
    assert 'startNetwork("boot_forced")' in main
    assert 'startNetwork("boot_retry")' in main.split("void loop()", 1)[1]
    assert "mqtt.connected()" in main.split("void loop()", 1)[1]
    setup_body = main.split("void setup()", 1)[1].split("void loop()", 1)[0]
    assert setup_body.index('runBootInitialTargetIfNeeded("setup_local_boot")') < setup_body.index('startNetwork("boot_forced")')
    assert "prearmMotion" not in main
    assert "network_start_before_wifi" not in main
    assert "wifi.begin(config);" in main
    assert "connecting to configured SSID" in wifi
    assert "Serial.println(config.wifiSsid)" not in wifi


def test_fleet_idle_and_dead_attach_motion_and_set_targets() -> None:
    control = read("src/turret_fleet/control/turret_control.cpp")
    config = read("src/turret_fleet/config/runtime_config.h")

    assert "float deadPitchDeg = 12.0f;" in config
    assert "float idleYawSpeedDegS = 8.0f;" in config
    assert 'mode_ = "IDLE"' in control
    assert 'updateIdleSweep(dtS)' in control
    assert "config_.idleYawMinDeg" in control
    assert 'mode_ = "DEAD"' in control
    assert 'pitchGoalDeg_ = clampPitchCommand(config_.deadPitchDeg' in control
    assert "yawTargetDeg_ = yawCurrentDeg_" in control
    assert "pitchTargetDeg_ = pitchCurrentDeg_" in control
    assert 'mode_ == "WAIT_COMMAND" || mode_ == "UNCONFIGURED"' in control
    assert 'motion["tracking_active"] = (mode_ == "HOME" || mode_ == "TARGET" || mode_ == "PATTERN")' in control


def test_fleet_fire_drives_real_relay_esc_outputs_and_allows_500ms_pulse() -> None:
    control = read("src/turret_fleet/control/turret_control.cpp")
    header = read("src/turret_fleet/control/turret_control.h")
    config = read("src/turret_fleet/config/runtime_config.h")

    assert "uint16_t fireEscRunUs = 1700;" in config
    assert "uint32_t fireDefaultHoldMs = 500;" in config
    assert "uint32_t fireMinHoldMs = 100;" in config
    assert "const int kRelayCh1Pin = 21;" in control
    assert "const int kRelayCh2Pin = 22;" in control
    assert "const int kRelayCh3Pin = 23;" in control
    assert "const int kEscPin = 25;" in control
    assert "Servo esc_;" in header
    assert "FIRE_SEQUENCE_CH2_ON_WAIT" in header
    assert "runEscNow(\"fire-command\")" in control
    assert "relayWrite(kRelayCh2Pin, true)" in control
    assert "config_.fireEscRunUs" in control
    assert "fireKeepAliveUntilMs_ = 0;" in control
    assert "fireSequenceState_ == FIRE_SEQUENCE_RUNNING &&\n      fireKeepAliveUntilMs_ != 0" in control
    assert "fireKeepAliveUntilMs_ = now + fireRequestedHoldMs_;" in control
    assert 'postFireMode_ = (mode_ == "TARGET" || mode_ == "PATTERN" || mode_ == "HOME")' in control
    assert "ESC STOP after hold_ms=" in control
    assert "forceFireOutputsSafeOff();" in control
    assert "fire rejected in DEAD mode" in control
    assert "fire rejected: hardware disabled by config" not in control


def test_explicit_fire_does_not_wait_for_target_aim_stability() -> None:
    control = read("src/turret_fleet/control/turret_control.cpp")
    header = read("src/turret_fleet/control/turret_control.h")

    assert "bool TurretControl::aimReached() const" in control
    assert "aimStableForFire" not in control
    assert "aimStableForFire" not in header
    assert 'mode_ == "TARGET" && !aimStableForFire(now)' not in control
    assert "queued until aim stable" not in control
    assert "pending fire released after aim stable_ms=" not in control
    assert 'startFireSequence(holdMs, source);' in control
    assert "pending fire dropped: hardware disabled" not in control


def test_pitch_deadband_is_tighter_than_aim_reached_tolerance() -> None:
    control = read("src/turret_fleet/control/turret_control.cpp")

    assert "const float kPitchDeadbandPseudo = 10.0f;" in control
    assert "fabs(pitchFinal - pitchCurrentDeg_) <= 2.0f" in control


def test_target_motion_uses_slew_and_pitch_safety_guard() -> None:
    control = read("src/turret_fleet/control/turret_control.cpp")
    header = read("src/turret_fleet/control/turret_control.h")

    assert "setTrackedTarget" in header
    assert "updateTrackedTargetSlew" in header
    assert "const float kTargetYawLeadDeg = 30.0f;" in control
    assert "const float kTargetPitchLeadDeg = 30.0f;" in control
    assert "uint16_t servoMaxDeltaUs = 220;" in read("src/turret_fleet/config/runtime_config.h")
    assert "uint16_t yawMaxDeltaUs = 20;" in read("src/turret_fleet/config/runtime_config.h")
    assert "uint16_t pitchMaxDeltaUs = 20;" in read("src/turret_fleet/config/runtime_config.h")
    assert "uint16_t pitchMinDriveUs = 20;" in read("src/turret_fleet/config/runtime_config.h")
    assert "Drive exactly one axis at a time" in control
    assert "Lock onto one axis" in control
    assert "axis_switch_to_yaw" in control
    assert "axisConvergenceAllowed" in control
    assert "if (kUnsafeManualCalibrationMode) {\n    guardStartErrorDeg = absError;" in control
    assert "Use a single monotonic yaw mapping" in control
    assert "kYawContinuousFeedback = true" in control
    assert "kUnsafeManualCalibrationMode = false" in control
    assert "boot home preview only: unsafe/manual calibration mode" in control
    assert "pitchRawCurrent_ = static_cast<int>(pitchSum / 8);" in control
    assert "const int yawFeedbackRaw = clampi(yawRawCurrent_, 0, 4095);" in control
    assert "const int pitchFeedbackRaw = clampi(pitchRawCurrent_, kPitchLowCut, kPitchHighCut);" in control
    assert "runBootAxisProbe" in control
    assert "outside calibrated 150deg safe envelope" in control
    assert "ensureMotionSafetyForTracking(source)" in control
    assert "ensurePitchSafetyForTracking(source)" in control
    assert "setPitchOnlyTrackedTarget" in control
    assert "PITCH_ONLY_YAW_INHIBITED" in control
    assert "yaw tracking inhibited; pitch-only target tracking active" in control
    assert "yawTrackingSuppressed_" in control
    assert "recoverMotionSoftWindow" in control
    assert "soft-window recovery" in control
    assert "motionReadingsStableInSoftWindow" in control
    assert "feedback stability source=" in control
    assert "kTrackingYawMaxDeltaUs" in control
    assert "kYawSoftRecoverDeltaUs" in control
    assert "kPitchSoftRecoverDeltaUs" in control
    assert "kYawSoftRecoverDriveMs" in control
    assert "yaw recovery skipped at hard-edge feedback" in control
    assert "yaw boot probe blocked: feedback outside calibrated 150deg safe envelope" in control
    assert "positive probe near low soft limit" in control
    assert "negative probe near high soft limit" in control
    assert "yawBefore > yawHomeRaw" in control
    assert "pitchBefore > pitchHomeRaw" in control
    assert "yawSoftLowRaw()" in control
    assert "pitchSoftLowRaw()" in control
    assert "boot probe skipped: already inside calibrated 150deg safe envelope" in control
    assert "kYawSoftMinDeg = -75.0f" in control
    assert "kPitchSoftMaxDeg = 75.0f" in control
    assert "yaw_soft_limit_guard" in control
    assert "kSoftLimitRescueDeltaUs" in control
    assert "yaw_invert_motor" in control
    assert "leadToward(pitchCurrentDeg_, pitchGoalDeg_, kTargetPitchLeadDeg)" in control
    assert "config_.pitchMaxDeg" in control
    assert "targetSlewActive_" in control
    assert "pitch safety guard" in control
    assert "const bool kYawInvertMotor = false;" in control
    assert "const bool kPitchInvertMotor = false;" in control


def test_new_motion_commands_preempt_stale_axis_state_before_tracking() -> None:
    control = read("src/turret_fleet/control/turret_control.cpp")
    header = read("src/turret_fleet/control/turret_control.h")

    assert "prepareForNewMotionCommand" in header
    assert "void TurretControl::prepareForNewMotionCommand(const char* source)" in control
    assert "stopMotionOutputs();\n  updateCurrentAngles();" in control
    assert "yawGoalDeg_ = yawCurrentDeg_" in control
    assert "pitchGoalDeg_ = pitchCurrentDeg_" in control
    assert "targetSlewActive_ = false;" in control
    assert "selectedMotionAxis_ = 'N';" in control
    assert "lockedMotionAxis_ = 'N';" in control
    assert "resetAxisGuard('A');" in control
    assert "preempted active motion for" in control
    assert "prepareForNewMotionCommand(source);\n\n  lastTargetCmX_ = xCm;" in control
    assert "prepareForNewMotionCommand(source);\n\n  solvedYawDeg_ = doc" in control
    assert "forceFireOutputsSafeOff();\n  prepareForNewMotionCommand(source);" in control


def test_fleet_env_includes_esp32servo_for_yaw_pitch_motion() -> None:
    platformio = read("platformio.ini")
    fleet_env = platformio.split("[env:esp32dev_turret_fleet]", 1)[1].split("[env:native]", 1)[0]

    assert "madhephaestus/ESP32Servo" in fleet_env


def test_ota_identity_is_aligned_across_firmware_script_and_examples() -> None:
    firmware = read("src/turret_fleet/app/firmware_info.h")
    script = read("scripts/turret_fleet/make_release_manifest.py")
    provision = read("scripts/turret_fleet/provision.py")
    workflow = read(".github/workflows/turret-fleet-firmware.yml")
    example = json.loads(read("src/turret_fleet/examples/ota-manifest.example.json"))

    assert 'BB_TURRET_FLEET_APP_NAME "battlebang-turret-fleet"' in firmware
    assert 'BB_TURRET_FLEET_HARDWARE "esp32dev-turret-v2"' in firmware
    assert 'BB_TURRET_FLEET_RELEASE_REPO "KongPedia/battlebang-esp"' in firmware
    assert "https://github.com/KongPedia/battlebang-esp/releases/latest/download/manifest.json" in firmware
    assert 'default="battlebang-turret-fleet"' in script
    assert 'default="esp32dev-turret-v2"' in script
    assert "KongPedia/battlebang-esp" in provision
    assert "push:" in workflow
    assert "branches:" in workflow
    assert "- main" in workflow
    assert 'VERSION="0.1.${GITHUB_RUN_NUMBER}-main"' in workflow
    assert 'BUILD="${GITHUB_RUN_NUMBER}"' in workflow
    assert 'default: "KongPedia/battlebang-esp"' in workflow
    assert "DEFAULT_GITHUB_TOKEN: ${{ github.token }}" in workflow
    assert "PUBLIC_RELEASE_REPO_TOKEN: ${{ secrets.PUBLIC_RELEASE_REPO_TOKEN }}" in workflow
    assert "steps.version.outputs.public_release_repo" in workflow
    assert 'if [[ "${PUBLIC_REPO}" == "${GITHUB_REPOSITORY}" ]]; then' in workflow
    assert 'export GH_TOKEN="${DEFAULT_GITHUB_TOKEN}"' in workflow
    assert example["app"] == "battlebang-turret-fleet"
    assert example["hardware"] == "esp32dev-turret-v2"


def test_first_provisioning_example_contains_coordinate_frame_and_ip_broker_placeholder() -> None:
    config = json.loads(read("src/turret_fleet/examples/config.turret_5.json"))

    assert config["schema"] == 2
    assert config["coordinate_frame"]["frame_id"] == "boss_stage_v1"
    assert config["coordinate_frame"]["mqtt_target_unit"] == "m"
    assert config["mqtt"] == {
        "host": "COMMAND_CENTER_IP",
        "port": 1883,
        "root": "battlebang",
    }
    assert config["network"]["auto_start"] is True
    assert config["network"]["start_delay_ms"] == 10000
    assert config["fire"]["esc_run_us"] == 1700
    assert config["fire"]["default_hold_ms"] == 500
    assert config["motion"]["yaw_stop_us"] == 1500
    assert config["motion"]["pitch_stop_us"] == 1500
    assert config["motion"]["limits"] == {
        "yaw_min_deg": -75.0,
        "yaw_max_deg": 75.0,
        "pitch_min_deg": -75.0,
        "pitch_max_deg": 75.0,
    }
    assert config["motion"]["home"] == {"yaw_deg": 0.0, "pitch_deg": 0.0}
    assert config["motion"]["dead"]["pitch_deg"] == 12.0
    assert config["motion"]["idle"]["yaw_min_deg"] == -15.0
    assert config["motion"]["idle"]["yaw_speed_deg_s"] == 8.0
    assert config["ota"]["command_center_controlled"] is True
    assert config["ota"]["auto_check_enabled"] is False


def test_fleet_docs_do_not_reference_old_pitch_pattern_or_old_ota_identity() -> None:
    paths = [
        "src/turret_fleet/docs/implementation-plan.md",
        "src/turret_fleet/docs/mqtt-http-contract.md",
        "src/turret_fleet/docs/usage.md",
        "src/turret_fleet/examples/ota-manifest.example.json",
    ]
    combined = "\n".join(read(path) for path in paths)

    assert "sweep_pitch" not in combined
    assert '"app": "battlebang-turret"' not in combined
    assert '"hardware": "esp32dev"' not in combined
    assert "sweep_vertical" in combined
    assert "battlebang-turret-fleet" in combined
    assert "esp32dev-turret-v2" in combined
    assert "Two-stage" in combined
    assert "Post-OTA boot intentionally stayed in `WAIT_COMMAND`" in combined


def test_serial_and_mqtt_json_buffers_are_heap_backed_to_avoid_loop_stack_overflow() -> None:
    main = read("src/turret_fleet/main.cpp")
    mqtt = read("src/turret_fleet/mqtt/mqtt_bus.cpp")
    config = read("src/turret_fleet/config/runtime_config.cpp")

    assert "DynamicJsonDocument doc(4096);" in main
    assert "DynamicJsonDocument doc(1024);" in main
    assert "DynamicJsonDocument doc(4096);" in mqtt
    assert "DynamicJsonDocument doc(1024);" in mqtt
    assert "DynamicJsonDocument doc(4096);" in config
    assert "StaticJsonDocument<4096>" not in main
    assert "StaticJsonDocument<4096>" not in mqtt
    assert "StaticJsonDocument<4096>" not in config


def test_fleet_applies_power_saving_without_disabling_brownout_detector() -> None:
    main = read("src/turret_fleet/main.cpp")

    assert "btStop();" in main
    assert "setCpuFrequencyMhz(80);" in main
    assert "RTC_CNTL_BROWN_OUT" not in main
    assert "WRITE_PERI_REG" not in main


def test_brownout_boot_locks_motion_and_fire_until_explicit_recovery() -> None:
    main = read("src/turret_fleet/main.cpp")
    control = read("src/turret_fleet/control/turret_control.cpp")
    header = read("src/turret_fleet/control/turret_control.h")
    mqtt = read("src/turret_fleet/mqtt/mqtt_bus.cpp")
    helper = read("scripts/turret_fleet/mqtt_command.py")

    assert "fireRecoveryRequiredAtBoot = loadFireRecoveryMarker();" in main
    assert "recoveryLockoutRequiredAtBoot = loadRecoveryLockoutMarker();" in main
    assert "otaRebootInhibitRequiredAtBoot = consumeOtaRebootMarker();" in main
    assert '#include "ota/reboot_marker.h"' in main
    assert '#include "../ota/reboot_marker.h"' in mqtt
    assert "bootSafetyLockoutRequired = bootResetReason == ESP_RST_BROWNOUT" in main
    assert "!otaRebootInhibitRequiredAtBoot" in main
    assert "fireHardwareEnabled" not in read("src/turret_fleet/config/runtime_config.h")
    assert "hardware_enabled" not in read("scripts/turret_fleet/provision.py")
    assert "--fire-hardware-enabled" not in helper
    assert "control.setBrownoutLockout(bootSafetyLockoutRequired)" in main
    assert "writeOtaRebootMarker(true)" in main
    assert "writeOtaRebootMarker(true)" in mqtt
    assert "post-OTA boot: automatic HOME drive inhibited" in main
    assert 'doc["fire_recovery_required_at_boot"] = fireRecoveryRequiredAtBoot;' in main
    assert 'doc["recovery_lockout_required_at_boot"] = recoveryLockoutRequiredAtBoot;' in main
    assert "setBrownoutLockout" in header
    assert "brownoutLockoutActive() const" in header
    assert "recoverBrownoutLockoutIfSafe" in header
    assert 'bootAutoRecoverySucceeded = control.recoverBrownoutLockoutIfSafe("boot_auto_recover")' in main
    assert 'doc["boot_auto_recovery_attempted"] = bootAutoRecoveryAttempted;' in main
    assert 'doc["boot_auto_recovery_succeeded"] = bootAutoRecoverySucceeded;' in main
    assert 'doc["ota_reboot_inhibit_required_at_boot"] = otaRebootInhibitRequiredAtBoot;' in main
    assert "sanitizeConfigForSafety" not in header
    assert "brownout/fire-reset lockout active: motion/fire blocked until recover succeeds" in control
    assert "writeFireRecoveryMarker(true)" in control
    assert "writeFireRecoveryMarker(false)" in control
    assert 'kRecoveryLockoutMarkerKey = "recover_req"' in control
    assert "writeRecoveryLockoutMarker(true)" in control or "writeRecoveryLockoutMarker(active)" in control
    assert "writeRecoveryLockoutMarker(false)" in control
    assert "commandBlockedByBrownoutLockout" in control
    assert "command rejected after brownout lockout" in control
    assert "fire rejected after brownout lockout" in control
    assert "recover rejected: feedback outside stable soft window" in control
    assert 'motion["brownout_lockout"] = brownoutLockoutActive_;' in control
    assert '"recover"' in helper


def test_ota_polling_is_command_center_approved_and_safe_state_gated() -> None:
    main = read("src/turret_fleet/main.cpp")
    mqtt = read("src/turret_fleet/mqtt/mqtt_bus.cpp")
    helper = read("scripts/turret_fleet/mqtt_command.py")
    publisher = read("scripts/turret_fleet/publish_mqtt_manifest.py")
    bin_helper = read("bin/turret")

    assert "pollConfiguredOta" in main
    assert "commandCenterApprovesPolledOta" in main
    assert "manifest.build != config.otaDesiredBuild" in main
    assert "config.otaCommandCenterControlled" in main
    assert "config.otaApplyOnlyInSafeState && !control.isSafeForOta()" in main
    assert 'if (mode_ == "HOME" || mode_ == "TARGET")' in read("src/turret_fleet/control/turret_control.cpp")
    assert "aimReached() && !targetSlewActive_" in read("src/turret_fleet/control/turret_control.cpp")
    assert "selectedMotionAxis_ == 'N'" in read("src/turret_fleet/control/turret_control.cpp")
    assert "ota_poll_not_approved" in main
    assert 'doc["ota_auto_check_enabled"] = config.otaAutoCheckEnabled;' in main
    assert 'doc["ota_desired_build"] = config.otaDesiredBuild;' in main
    assert 'doc["ota_auto_check_enabled"] = config_->otaAutoCheckEnabled;' in mqtt
    assert "--ota-auto-check-enabled" in helper
    assert 'ota["desired_build"] = args.ota_desired_build' in helper
    assert "DEFAULT_LATEST_MANIFEST_URL" in helper
    assert "ota-update" in helper
    assert "fleet-mqtt turret_2 update --desired-build" in bin_helper
    assert "fleet-ota-publish" in bin_helper
    assert "from mqtt_command import publish_mqtt" in publisher


def test_axis_offsets_are_runtime_configurable_for_software_zeroing() -> None:
    config_h = read("src/turret_fleet/config/runtime_config.h")
    config_cpp = read("src/turret_fleet/config/runtime_config.cpp")
    control = read("src/turret_fleet/control/turret_control.cpp")
    docs = read("src/turret_fleet/README.md") + read("src/turret_fleet/docs/mqtt-http-contract.md")

    assert "float yawAxisOffsetDeg = 0.0f;" in config_h
    assert "float pitchAxisOffsetDeg = 0.0f;" in config_h
    assert "uint16_t yawStopUs = 1500;" in config_h
    assert "uint16_t pitchStopUs = 1500;" in config_h
    assert "float homeYawDeg = 0.0f;" in config_h
    assert "float yawMinDeg = -75.0f;" in config_h
    assert "float pitchMaxDeg = 75.0f;" in config_h
    assert "uint16_t servoMaxDeltaUs = 220;" in config_h
    assert "uint16_t axisSwitchCooldownMs = 800;" in config_h
    assert 'calibration["yaw_axis_offset_deg"]' in config_cpp
    assert 'calibration["home_yaw_deg"]' in config_cpp
    assert 'motion["yaw_stop_us"]' in config_cpp
    assert 'motion["limits"]' in config_cpp
    assert 'motion["home"]' in config_cpp
    assert 'motion["servo_max_delta_us"]' in config_cpp
    assert 'prefs.getUShort("yaw_stop"' in config_cpp
    assert 'prefs.putUShort("yaw_stop"' in config_cpp
    assert 'prefs.getUShort("axis_cool"' in config_cpp
    assert 'prefs.putUShort("axis_cool"' in config_cpp
    assert 'prefs.getFloat("yaw_axis"' in config_cpp
    assert 'prefs.putFloat("yaw_axis"' in config_cpp
    assert 'prefs.getFloat("home_yaw"' in config_cpp
    assert 'prefs.putFloat("home_yaw"' in config_cpp
    assert 'prefs.getFloat("yaw_min_deg"' in config_cpp
    assert 'prefs.putFloat("yaw_min_deg"' in config_cpp
    assert "yawSensorDeg + config_.yawAxisOffsetDeg" in control
    assert "pitchSensorDeg + config_.pitchAxisOffsetDeg" in control
    assert 'aim["yaw_axis_offset_deg"]' in control
    assert "yaw_axis_offset_deg" in docs


def test_fleet_calibration_config_accepts_150deg_safe_envelope() -> None:
    config_cpp = read("src/turret_fleet/config/runtime_config.cpp")

    assert "next.yawMaxDeg - next.yawMinDeg > 150.0f" in config_cpp
    assert "next.pitchMaxDeg - next.pitchMinDeg > 150.0f" in config_cpp
    assert "next.deadPitchDeg < next.pitchMinDeg || next.deadPitchDeg > next.pitchMaxDeg" in config_cpp


def test_fleet_mqtt_helper_builds_direct_commands_and_config_patches() -> None:
    import importlib.util

    path = ROOT / "scripts/turret_fleet/mqtt_command.py"
    spec = importlib.util.spec_from_file_location("mqtt_command", path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    assert module.topic_for("battlebang/", "turret_2", "command") == "battlebang/turrets/turret_2/command"
    args = module.build_parser().parse_args(["--host", "10.2.80.52", "2", "target", "0", "0", "2"])
    suffix, payload = module.build_command_payload(args)
    assert suffix == "command"
    assert payload["command"] == "target"
    assert payload["target"] == {"x": 0.0, "y": 0.0, "z": 2.0}

    args = module.build_parser().parse_args(["--host", "10.2.80.52", "2", "initiate"])
    suffix, payload = module.build_command_payload(args)
    assert suffix == "command"
    assert payload["command"] == "home"

    args = module.build_parser().parse_args(["--host", "10.2.80.52", "2", "recover"])
    suffix, payload = module.build_command_payload(args)
    assert suffix == "command"
    assert payload["command"] == "recover"

    args = module.build_parser().parse_args([
        "--host",
        "10.2.80.52",
        "turret_2",
        "jog",
        "yaw",
        "plus",
        "--delta-us",
        "20",
        "--duration-ms",
        "40",
    ])
    suffix, payload = module.build_command_payload(args)
    assert suffix == "command"
    assert payload["command"] == "jog"
    assert payload["axis"] == "yaw"
    assert payload["direction"] == "plus"
    assert payload["delta_us"] == 20
    assert payload["duration_ms"] == 40

    args = module.build_parser().parse_args([
        "--host",
        "10.2.80.52",
        "turret_2",
        "config",
        "--yaw-axis-offset-deg",
        "9",
        "--dead-pitch-deg",
        "24",
        "--yaw-stop-us",
        "1500",
        "--pitch-stop-us",
        "1500",
        "--axis-switch-cooldown-ms",
        "800",
        "--servo-max-delta-us",
        "220",
        "--yaw-max-delta-us",
        "60",
        "--pitch-max-delta-us",
        "40",
        "--yaw-min-deg",
        "-75",
        "--yaw-max-deg",
        "75",
        "--home-yaw-deg",
        "0",
        "--home-pitch-deg",
        "0",
        "--ota-auto-check-enabled",
        "true",
        "--ota-desired-build",
        "2",
        "--ota-public-manifest-url",
        "https://github.com/KongPedia/battlebang-esp/releases/latest/download/manifest.json",
    ])
    suffix, payload = module.build_command_payload(args)
    assert suffix == "config"
    assert payload["calibration"]["yaw_axis_offset_deg"] == 9.0
    assert "hardware_enabled" not in payload.get("fire", {})
    assert payload["motion"]["dead"]["pitch_deg"] == 24.0
    assert payload["motion"]["yaw_stop_us"] == 1500
    assert payload["motion"]["pitch_stop_us"] == 1500
    assert payload["motion"]["axis_switch_cooldown_ms"] == 800
    assert payload["motion"]["servo_max_delta_us"] == 220
    assert payload["motion"]["yaw_max_delta_us"] == 60
    assert payload["motion"]["pitch_max_delta_us"] == 40
    assert payload["motion"]["limits"] == {"yaw_min_deg": -75.0, "yaw_max_deg": 75.0}
    assert payload["motion"]["home"] == {"yaw_deg": 0.0, "pitch_deg": 0.0}
    assert payload["ota"]["auto_check_enabled"] is True
    assert payload["ota"]["desired_build"] == 2
    assert payload["ota"]["public_manifest_url"].endswith("/manifest.json")
    assert "yaw_stop_us" not in payload["motion"].get("idle", {})

    args = module.build_parser().parse_args([
        "--host",
        "10.2.80.52",
        "turret_2",
        "update",
        "--desired-build",
        "7",
    ])
    suffix, payload = module.build_command_payload(args)
    assert suffix == "config"
    assert payload["type"] == "config"
    assert payload["ota"]["command_center_controlled"] is True
    assert payload["ota"]["auto_check_enabled"] is True
    assert payload["ota"]["desired_build"] == 7
    assert payload["ota"]["channel"] == "stable"
    assert payload["ota"]["public_manifest_url"] == module.DEFAULT_LATEST_MANIFEST_URL
    assert payload["ota"]["local_mirror_url"] == ""
    assert payload["ota"]["check_interval_s"] == 30
    assert payload["ota"]["apply_only_in_safe_state"] is True
