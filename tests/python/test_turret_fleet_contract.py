from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PRIVATE_LAB_PREFIX = ".".join(["10", "2", "80"]) + "."


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_turret_fleet_mqtt_topics_use_common_builders() -> None:
    topics = read("firmware/turret_fleet/mqtt/topics.cpp")

    assert "#include <bb_esp_core/mqtt/device_topics.h>" in topics
    assert "normalizeRootOrDefault(config.mqttRoot)" in topics
    assert 'makeDeviceTopicsChecked(root, "turret", config.deviceId' in topics
    assert 'makeAllOtaTopicChecked(root, "turrets"' in topics
    assert 'makeEntityTopicsChecked(\n            root, "turrets", config.turretId' in topics
    assert "topics.turretCommand = entityTopics.command;" in topics


def test_turret_fleet_ota_manifest_uses_common_helper() -> None:
    manifest = read("firmware/turret_fleet/ota/ota_manifest.cpp")
    manifest_h = read("firmware/turret_fleet/ota/ota_manifest.h")
    reboot = read("firmware/turret_fleet/ota/reboot_marker.cpp")

    assert "#include <bb_esp_ota/ota_manifest.h>" in manifest_h
    assert "identity.app = BB_TURRET_FLEET_APP_NAME" in manifest
    assert "identity.hardware = BB_TURRET_FLEET_HARDWARE" in manifest
    assert "identity.build = BB_TURRET_FLEET_BUILD" in manifest
    assert "shouldApplyManifest(manifest, firmwareIdentity(), reason)" in manifest
    assert "bb_esp_ota/reboot_marker.h" in reboot
    assert "writeRebootMarker(kSafetyPrefsNamespace, kOtaRebootMarkerKey, active)" in reboot


def test_legacy_turret_boots_hold_not_idle_sweep() -> None:
    main = read("src/turret/main.cpp")
    setup_body = main.split("void setup()", 1)[1].split("void loop()", 1)[0]

    assert "clearPendingFireFlags();\n  enterHoldMode();" in setup_body
    assert "clearPendingFireFlags();\n  enterIdleMode();" not in setup_body


def test_fleet_control_boots_wait_command_then_initial_local_home() -> None:
    control = read("firmware/turret_fleet/control/turret_control.cpp")
    main = read("firmware/turret_fleet/main.cpp")
    mqtt_h = read("firmware/turret_fleet/mqtt/mqtt_bus.h")

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
    header = read("firmware/turret_fleet/config/runtime_config.h")

    assert "uint16_t schema = 2;" in header
    assert 'String mqttHost = "";' in header
    assert "uint16_t mqttPort = 1883;" in header
    assert 'String frameId = "boss_stage_v1";' in header
    assert 'String mqttTargetUnit = "m";' in header


def test_serial_supports_first_provisioning_and_debug_commands() -> None:
    main = read("firmware/turret_fleet/main.cpp")

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
    env_example = read("firmware/turret_fleet/.env.turret_fleet.example")

    assert "fleet-upload" in helper
    assert "fleet-provision" in helper
    assert "esp32dev_turret_fleet" in helper
    assert "firmware/turret_fleet/.env.turret_fleet" in gitignore
    assert "TURRET_FLEET_WIFI_PASSWORD=YOUR_WIFI_PASSWORD" in env_example
    assert "TURRET_FLEET_MQTT_HOST=COMMAND_CENTER_IP_OR_DNS" in env_example
    assert PRIVATE_LAB_PREFIX not in env_example
    assert "TURRET_FLEET_NETWORK_AUTO_START=true" in env_example
    assert "TURRET_FLEET_DEVICE_ID=" in env_example
    assert "TURRET_FLEET_STAGE_ID=boss_stage_v1" in env_example
    assert "Keep false while power/servo brownout" not in env_example
    assert "TURRET_FLEET_YAW_STOP_US=1500" in env_example
    assert "TURRET_FLEET_PITCH_STOP_US=1500" in env_example
    assert "TURRET_FLEET_YAW_PLUS_MAX_DELTA_US=0" in env_example
    assert "TURRET_FLEET_YAW_MINUS_MAX_DELTA_US=0" in env_example
    assert "TURRET_FLEET_AXIS_SWITCH_COOLDOWN_MS=800" in env_example
    assert "def build_config" in provision
    assert "normalize_turret_id" in provision
    assert "TURRET_FLEET_PROFILE_ID" in provision
    assert "TURRET_FLEET_DEVICE_ID" in provision
    assert '"turret_id": turret_id' in provision
    assert '"device_id": runtime_device_id' in provision
    assert "TURRET_FLEET_MQTT_HOST" in provision
    assert 'default="true"' in provision
    assert '"yaw_stop_us": yaw_stop_us' in provision
    assert '"yaw_plus_max_delta_us": yaw_plus_max_delta_us' in provision
    assert "def write_serial_line" in provision
    assert "SERIAL_WRITE_CHUNK_BYTES = 96" in provision
    assert "SERIAL_BOOT_SETTLE_S = 4.0" in provision
    assert "SERIAL_PROVISION_RETRIES = 3" in provision
    assert '"InvalidInput" JSON parse error' in provision
    assert "retrying serial provision after truncated JSON" in provision
    assert "write_serial_line(ser, f\"provision {payload}\")" in provision


def test_fleet_target_contract_rejects_frame_mismatch_and_converts_meters() -> None:
    control = read("firmware/turret_fleet/control/turret_control.cpp")

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


def test_fleet_rejects_expired_timestamped_commands_when_wall_clock_is_valid() -> None:
    control = read("firmware/turret_fleet/control/turret_control.cpp")
    helper = read("scripts/turret_fleet/mqtt_command.py")

    assert "#include <time.h>" in control
    assert "bool commandExpiredByTtl(JsonDocument& doc, String& reason)" in control
    assert 'doc["issued_at_ms"]' in control
    assert 'doc["timestamp_ms"]' in control
    assert 'doc["expires_at_ms"]' in control
    assert "wallClockLooksValid()" in control
    assert "time(nullptr) > 1700000000" in control
    assert "command rejected: ttl expired" in control
    assert "if (commandExpiredByTtl(doc, ttlRejectReason))" in control
    assert 'lastError_ = "";' in control.split("bool TurretControl::handleCommandJson", 1)[1].split("const char* command", 1)[0]
    assert "lastError_ = ttlRejectReason;" in control
    assert "return false;" in control.split("if (commandExpiredByTtl(doc, ttlRejectReason))", 1)[1].split("if (strcmp(command, \"recover\")", 1)[0]
    assert "publishStatus(applied ? \"command_applied\" : \"command_rejected\")" in read("firmware/turret_fleet/mqtt/mqtt_bus.cpp")
    assert 'payload["issued_at_ms"] = int(time.time() * 1000)' in helper


def test_fleet_supports_direct_yaw_pitch_aim_for_axis_debugging() -> None:
    control = read("firmware/turret_fleet/control/turret_control.cpp")
    header = read("firmware/turret_fleet/control/turret_control.h")

    assert "applyDirectAimCommand" in header
    assert 'strcmp(command, "aim") == 0' in control
    assert 'strcmp(command, "manual_aim") == 0' in control
    assert "setTrackedTarget(clampedYawDeg_, clampedPitchDeg_)" in control
    assert 'doc["yaw_deg"].is<float>()' in control
    assert 'doc["pitch_deg"].is<float>()' in control
    assert "Direct aim is local turret yaw/pitch" in control


def test_fleet_supports_mqtt_home_init_command_for_local_zeroing() -> None:
    control = read("firmware/turret_fleet/control/turret_control.cpp")
    header = read("firmware/turret_fleet/control/turret_control.h")
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
    control = read("firmware/turret_fleet/control/turret_control.cpp")
    header = read("firmware/turret_fleet/control/turret_control.h")
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
    bus = read("firmware/turret_fleet/mqtt/mqtt_bus.cpp")
    control = read("firmware/turret_fleet/control/turret_control.cpp")

    assert "control_->appendStatus" in bus
    for field in [
        'doc["frame_id"]',
        'doc["command_state"]',
        'doc["command_in_progress"]',
        'doc["ready_for_next_command"]',
        'doc["preemptible"]',
        'doc["active_command_id"]',
        'doc["command_policy"]',
        'doc["pattern_state"]',
        'doc["fire_state"]',
        'doc["fire_sequence"]',
        'doc["last_error"]',
        'doc.createNestedObject("fire_output_state")',
        'fire["esc_attached"]',
        'fire["esc_command_us"]',
        'fire["relay_ch2_on"]',
        'fire["relay_ch3_active_low_config"]',
        'fire["relay_profile_config"]',
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
        'motionConfig["yaw_plus_max_delta_us"]',
        'motionConfig["yaw_minus_max_delta_us"]',
        'motionConfig["yaw_plus_min_drive_us"]',
        'motionConfig["yaw_minus_min_drive_us"]',
        'motionConfig["axis_switch_cooldown_ms"]',
        'motionConfig["command_envelope_ratio"]',
        'motionConfig["yaw_command_min_deg"]',
        'motionConfig["yaw_command_max_deg"]',
        'motionConfig["pitch_command_min_deg"]',
        'motionConfig["pitch_command_max_deg"]',
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

    for field in [
        "statusSignature",
        'publishStatus("state_changed")',
        "kStatusChangeCheckMs",
    ]:
        assert field in bus + control


def test_turret_fleet_boot_sends_esc_stop_signal_on_gpio25() -> None:
    control = read("firmware/turret_fleet/control/turret_control.cpp")

    assert "const int kEscPin = 25;" in control
    assert "ensureEscStopSignal(\"boot-ready\");" in control
    assert "Keep the legacy src/turret ESC contract" in control
    assert "ESC STOP signal active on boot-ready" in control
    assert "relay outputs parked safe-off" in control


def test_mqtt_config_update_can_change_wifi_or_broker_after_first_provisioning() -> None:
    bus = read("firmware/turret_fleet/mqtt/mqtt_bus.cpp")

    assert "wifiChanged" in bus
    assert "mqttChanged" in bus
    assert "next.deviceId != config_->deviceId" in bus
    assert "wifi_->begin(*config_)" in bus
    assert "reconfigure();" in bus


def test_network_autostart_is_forced_after_local_boot_initial_target() -> None:
    main = read("firmware/turret_fleet/main.cpp")
    config_h = read("firmware/turret_fleet/config/runtime_config.h")
    config_cpp = read("firmware/turret_fleet/config/runtime_config.cpp")
    wifi = read("firmware/turret_fleet/net/wifi_manager.cpp")
    wifi_h = read("firmware/turret_fleet/net/wifi_manager.h")
    common_wifi = read("lib/bb_esp_net/src/bb_esp_net/wifi_manager.cpp")

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
    assert "normalizeConfiguredRoot(next.mqttRoot, error)" in config_cpp
    assert "#include <bb_esp_core/config/runtime_config_json.h>" in config_cpp
    assert 'validateOtaManifestUrl(\n          next.otaPublicManifestUrl, "ota.public_manifest_url", error)' in config_cpp
    assert 'validateOtaManifestUrl(\n          next.otaLocalMirrorUrl, "ota.local_mirror_url", error)' in config_cpp
    assert "isSafeTopicSegment(next.deviceId)" in config_cpp
    assert 'keys.deviceId = "device_id";' in config_cpp
    assert "isSafeTopicSegment(next.turretId)" in config_cpp
    assert "turret_id must use only A-Z" in config_cpp
    assert "mqtt.root must not contain empty path segments" in read("lib/bb_esp_core/src/bb_esp_core/mqtt/topic_utils.h")
    setup_body = main.split("void setup()", 1)[1].split("void loop()", 1)[0]
    assert setup_body.index('runBootInitialTargetIfNeeded("setup_local_boot")') < setup_body.index('startNetwork("boot_forced")')
    assert "prearmMotion" not in main
    assert "network_start_before_wifi" not in main
    assert "wifi.begin(config);" in main
    assert "#include <bb_esp_net/wifi_manager.h>" in wifi_h
    assert 'WifiManager::WifiManager() : wifi_("[fleet][wifi]")' in wifi
    assert "connecting to configured SSID" in common_wifi
    assert "Serial.println(config.wifiSsid)" not in wifi
    assert "Serial.println(config.wifiSsid)" not in common_wifi


def test_fleet_idle_and_dead_attach_motion_and_set_targets() -> None:
    control = read("firmware/turret_fleet/control/turret_control.cpp")
    config = read("firmware/turret_fleet/config/runtime_config.h")

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
    control = read("firmware/turret_fleet/control/turret_control.cpp")
    header = read("firmware/turret_fleet/control/turret_control.h")
    config = read("firmware/turret_fleet/config/runtime_config.h")

    assert "uint16_t fireEscRunUs = 1700;" in config
    assert "uint32_t fireDefaultHoldMs = 500;" in config
    assert "uint32_t fireMinHoldMs = 100;" in config
    assert "bool fireRelayActiveLow = true;" in config
    assert "bool fireRelayCh3ActiveLow = true;" in config
    assert 'fire["relay_ch3_active_low"]' in read("firmware/turret_fleet/config/runtime_config.cpp")
    assert 'prefs.getBool("fire_r3_al"' in read("firmware/turret_fleet/config/runtime_config.cpp")
    assert "const int kRelayCh1Pin = 21;" in control
    assert "const int kRelayCh2Pin = 22;" in control
    assert "const int kRelayCh3Pin = 23;" in control
    assert "const int kEscPin = 25;" in control
    assert "Servo esc_;" in header
    assert "FIRE_SEQUENCE_CH2_ON_WAIT" in header
    assert "runEscNow(\"fire-command\")" in control
    assert "relayWrite(kRelayCh2Pin, true)" in control
    assert "relayPinActiveLow(kRelayCh3Pin) ? INPUT_PULLUP : INPUT_PULLDOWN" in control
    assert "relayOffLevel(kRelayCh3Pin)" in control
    assert "config_.fireEscRunUs" in control
    assert "fireKeepAliveUntilMs_ = 0;" in control
    assert "fireSequenceState_ == FIRE_SEQUENCE_RUNNING && fireKeepAliveUntilMs_ != 0" in control
    assert "bool deadlineReached(unsigned long now, unsigned long deadlineMs)" in control
    assert "if (deadlineMs == 0) return 0;" in control
    assert "deadlineReached(now, fireHardOffAtMs_)" in control
    assert "deadlineReached(now, fireKeepAliveUntilMs_)" in control
    assert "fireKeepAliveUntilMs_ = deadlineAfter(now, fireRequestedHoldMs_);" in control
    assert 'postFireMode_ = (mode_ == "TARGET" || mode_ == "PATTERN" || mode_ == "HOME")' in control
    assert "ESC STOP after hold_ms=" in control
    assert "forceFireOutputsSafeOff();" in control
    assert "fire rejected in DEAD mode" in control
    assert "fire rejected: hardware disabled by config" not in control


def test_fleet_relay_profile_is_explicit_nvs_contract_not_turret_id_mapping() -> None:
    config = read("firmware/turret_fleet/config/runtime_config.cpp")
    header = read("firmware/turret_fleet/config/runtime_config.h")
    control = read("firmware/turret_fleet/control/turret_control.cpp")
    docs = read("firmware/turret_fleet/config/README.md")

    assert "String fireRelayProfile;" in header
    assert 'profile == "single_channel_ch3_active_high"' in config
    assert 'profile == "two_channel_active_low"' in config
    assert 'fire["relay_profile"]' in config
    assert 'prefs.getString("fire_profile"' in config
    assert 'prefs.putString("fire_profile", config.fireRelayProfile)' in config
    assert 'fire["relay_profile_config"] = config_.fireRelayProfile' in control
    assert "hasOneChannelFireRelay" not in config
    assert "relayPolarityDefaultsForTurret" not in config
    assert "`fire.relay_profile` is the explicit hardware preset saved in NVS" in docs
    assert "`fire.relay_ch*_active_low` values; per-channel values remain authoritative" in docs


def test_explicit_fire_does_not_wait_for_target_aim_stability() -> None:
    control = read("firmware/turret_fleet/control/turret_control.cpp")
    header = read("firmware/turret_fleet/control/turret_control.h")

    assert "bool TurretControl::aimReached() const" in control
    assert "aimStableForFire" not in control
    assert "aimStableForFire" not in header
    assert 'mode_ == "TARGET" && !aimStableForFire(now)' not in control
    assert "queued until aim stable" not in control
    assert "pending fire released after aim stable_ms=" not in control
    assert 'startFireSequence(holdMs, source);' in control
    assert "pending fire dropped: hardware disabled" not in control


def test_pitch_deadband_is_tighter_than_aim_reached_tolerance() -> None:
    control = read("firmware/turret_fleet/control/turret_control.cpp")

    assert "const float kPitchDeadbandPseudo = 10.0f;" in control
    assert "const float kAimReachedToleranceDeg = 3.0f;" in control
    assert "const unsigned long kAimStableBeforeCompleteMs = 300;" in control
    assert 'const bool finiteAimMode = mode_ == "HOME" || mode_ == "TARGET";' in control
    assert "mode_ = \"WAIT_COMMAND\";" in control
    assert "fabs(pitchFinal - pitchCurrentDeg_) <= kAimReachedToleranceDeg" in control


def test_target_motion_uses_slew_and_pitch_safety_guard() -> None:
    control = read("firmware/turret_fleet/control/turret_control.cpp")
    header = read("firmware/turret_fleet/control/turret_control.h")

    assert "setTrackedTarget" in header
    assert "updateTrackedTargetSlew" in header
    assert "const float kTargetYawLeadDeg = 30.0f;" in control
    assert "const float kTargetPitchLeadDeg = 30.0f;" in control
    assert "uint16_t servoMaxDeltaUs = 220;" in read("firmware/turret_fleet/config/runtime_config.h")
    assert "uint16_t yawMaxDeltaUs = 20;" in read("firmware/turret_fleet/config/runtime_config.h")
    assert "uint16_t pitchMaxDeltaUs = 20;" in read("firmware/turret_fleet/config/runtime_config.h")
    assert "uint16_t pitchMinDriveUs = 20;" in read("firmware/turret_fleet/config/runtime_config.h")
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
    assert "const int kTrackingYawMaxDeltaUs = 450;" in control
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
    assert "Seed the intermediate setpoint immediately" in control
    assert "yawTargetDeg_ = leadToward(yawCurrentDeg_, yawGoalDeg_, kTargetYawLeadDeg);" in control
    assert "pitchTargetDeg_ = leadToward(pitchCurrentDeg_, pitchGoalDeg_, kTargetPitchLeadDeg);" in control
    assert "targetSlewActive_ = fabs(yawTargetDeg_ - yawGoalDeg_) > 0.01f ||" in control
    assert "config_.pitchMaxDeg" in control
    assert "targetSlewActive_" in control
    assert "pitch safety guard" in control
    assert "const bool kYawInvertMotor = false;" in control
    assert "const bool kPitchInvertMotor = false;" in control


def test_new_motion_commands_preempt_stale_axis_state_before_tracking() -> None:
    control = read("firmware/turret_fleet/control/turret_control.cpp")
    header = read("firmware/turret_fleet/control/turret_control.h")

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
    firmware = read("firmware/turret_fleet/app/firmware_info.h")
    script = read("scripts/turret_fleet/make_release_manifest.py")
    provision = read("scripts/turret_fleet/provision.py")
    workflow = read(".github/workflows/firmware-ota.yml")
    example = json.loads(read("firmware/turret_fleet/examples/ota-manifest.example.json"))

    assert 'BB_TURRET_FLEET_APP_NAME "battlebang-turret-fleet"' in firmware
    assert 'BB_TURRET_FLEET_HARDWARE "esp32dev-turret-v2"' in firmware
    assert 'BB_TURRET_FLEET_RELEASE_REPO "KongPedia/battlebang-esp"' in firmware
    assert '"https://github.com/" BB_TURRET_FLEET_RELEASE_REPO' in firmware
    assert '"/releases/download/turret-fleet-latest/manifest.json"' in firmware
    assert 'default="battlebang-turret-fleet"' in script
    assert 'default="esp32dev-turret-v2"' in script
    assert "KongPedia/battlebang-esp" in provision
    assert "push:" in workflow
    assert "pull_request:" not in workflow
    assert 'GITHUB_EVENT_NAME" == "pull_request"' not in workflow
    assert "branches:" in workflow
    assert "- main" in workflow
    assert 'VERSION="0.2.${GITHUB_RUN_NUMBER}-main"' in workflow
    assert 'BUILD="$((1000 + GITHUB_RUN_NUMBER))"' in workflow
    assert "update_stable_latest" in workflow
    assert 'UPDATE_STABLE_LATEST="true"' in workflow
    assert 'default: "KongPedia/battlebang-esp"' in workflow
    assert "DEFAULT_GITHUB_TOKEN: ${{ github.token }}" in workflow
    assert "PUBLIC_RELEASE_REPO_TOKEN: ${{ secrets.PUBLIC_RELEASE_REPO_TOKEN }}" in workflow
    assert "steps.version.outputs.public_release_repo" in workflow
    assert 'if [[ "${PUBLIC_REPO}" == "${GITHUB_REPOSITORY}" ]]; then' in workflow
    assert 'export GH_TOKEN="${DEFAULT_GITHUB_TOKEN}"' in workflow
    assert "turret-fleet-latest" in workflow
    assert "turret-fleet-v" in workflow
    assert '"manifest_name": "manifest.json"' in workflow
    assert '--latest=false' in workflow
    assert example["app"] == "battlebang-turret-fleet"
    assert example["hardware"] == "esp32dev-turret-v2"


def test_first_provisioning_example_contains_coordinate_frame_and_ip_broker_placeholder() -> None:
    config = json.loads(read("firmware/turret_fleet/examples/config.turret_5.json"))

    assert config["schema"] == 2
    assert config["stage_id"] == "boss_stage_v1"
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
        "firmware/turret_fleet/docs/implementation-plan.md",
        "firmware/turret_fleet/docs/mqtt-http-contract.md",
        "firmware/turret_fleet/docs/usage.md",
        "firmware/turret_fleet/examples/ota-manifest.example.json",
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


def test_btb_726_readable_pattern_catalog_has_three_player_attack_patterns() -> None:
    plan = read("firmware/turret_fleet/docs/btb-726-readable-patterns-plan.md")
    examples = {
        "lane_sweep": json.loads(read("firmware/turret_fleet/examples/pattern.lane_sweep.json")),
        "two_point_bounce": json.loads(read("firmware/turret_fleet/examples/pattern.two_point_bounce.json")),
        "telegraph_column": json.loads(read("firmware/turret_fleet/examples/pattern.telegraph_column.json")),
    }
    preset_config = json.loads(read("firmware/turret_fleet/pattern_presets/turret_2.json"))
    presets = preset_config["presets"]

    requirements = plan.split("## Requirements Summary", 1)[1].split("## Current Code Facts", 1)[0]
    assert "pattern catalog is intentionally reduced to three" in requirements.lower()
    assert "sweep_vertical" not in requirements
    assert "point_burst" not in requirements
    assert "calibration_no_fire" in plan
    assert "operator utility" in plan

    for pattern_id, payload in examples.items():
        assert payload["command"] == "pattern"
        assert payload["pattern_id"] == pattern_id
        assert payload["frame_id"] == "boss_stage_v1"
        assert payload["pattern_instance_id"].startswith("example-")
        assert isinstance(payload["params"].get("points"), list)
        assert 100 <= payload["params"].get("dwell_ms", 0) <= 5000
        assert 100 <= payload["params"].get("fire_ms", 0) <= 5000
        assert 500 <= payload["params"].get("move_timeout_ms", 0) <= 60000

    assert examples["two_point_bounce"]["params"].get("loop") == 2
    assert examples["telegraph_column"]["params"].get("phase_offset_ms") >= 0
    assert set(presets) == set(examples)
    assert not (ROOT / "firmware/turret_fleet/examples/config.patterns.turret_2.json").exists()
    expected_move_timeout_ms = {
        "lane_sweep": 20000,
        "two_point_bounce": 60000,
        "telegraph_column": 10000,
    }
    for pattern_id, preset in presets.items():
        assert preset["move_timeout_ms"] == expected_move_timeout_ms[pattern_id]
        for point in preset["points"]:
            assert point["x"] == 0.0
    assert 500 <= presets["lane_sweep"]["dwell_ms"] <= 2000
    assert presets["lane_sweep"]["fire_ms"] == 2000
    assert presets["two_point_bounce"]["dwell_ms"] == 1000
    assert presets["two_point_bounce"]["fire_ms"] == 1500
    assert presets["telegraph_column"]["dwell_ms"] == 1000
    assert presets["telegraph_column"]["fire_ms"] == 1500
    lane_points = presets["lane_sweep"]["points"]
    assert len(lane_points) == 2
    assert lane_points[0]["y"] > 0
    assert lane_points[1]["y"] < 0
    for point in lane_points:
        assert isinstance(point["z"], int | float)
    assert presets["lane_sweep"]["loop"] == 1
    assert presets["two_point_bounce"]["points"] == [
        {"x": 0.0, "y": 0.75, "z": -0.6},
        {"x": 0.0, "y": -0.5, "z": -0.6},
    ]
    assert presets["telegraph_column"]["random"] is True
    assert presets["telegraph_column"]["points"] == [
        {"x": 0.0, "y": 0.0, "z": -0.6},
        {"x": 0.0, "y": 0.5, "z": -0.6},
        {"x": 0.0, "y": -0.5, "z": -0.6},
    ]


def test_turret_fleet_profiles_define_four_turret_layout_and_preset_files() -> None:
    expected = {
        "turret_1": {"floor": 2, "side": "upper_left", "pose": {"x_cm": -300.0, "y_cm": 0.0, "z_cm": 134.5}},
        "turret_2": {"floor": 2, "side": "upper_right", "pose": {"x_cm": -300.0, "y_cm": 200.0, "z_cm": 134.5}},
        "turret_3": {"floor": 1, "side": "lower_left", "pose": {"x_cm": -300.0, "y_cm": 0.0, "z_cm": 34.5}},
        "turret_4": {"floor": 1, "side": "lower_right", "pose": {"x_cm": -300.0, "y_cm": 200.0, "z_cm": 34.5}},
    }

    for turret_id, layout in expected.items():
        config = json.loads(read(f"firmware/turret_fleet/profiles/{turret_id}.json"))
        presets = json.loads(read(f"firmware/turret_fleet/pattern_presets/{turret_id}.json"))
        expected_limits = {
            "yaw_min_deg": -50.0,
            "yaw_max_deg": 50.0,
            "pitch_min_deg": -75.0 if turret_id == "turret_3" else -60.0,
            "pitch_max_deg": 70.0,
        }

        assert config["schema"] == 2
        assert config["configured"] is True
        assert config["turret_id"] == turret_id
        assert config["floor"] == layout["floor"]
        assert config["side"] == layout["side"]
        assert config["coordinate_frame"]["x_axis"] == "stage_forward"
        assert config["coordinate_frame"]["y_axis"] == "stage_right"
        assert config["coordinate_frame"]["mqtt_target_unit"] == "m"
        assert {key: config["pose"][key] for key in ("x_cm", "y_cm", "z_cm")} == layout["pose"]
        assert config["motion"]["limits"] == expected_limits
        assert config["motion"]["home"] == {"yaw_deg": 0.0, "pitch_deg": 0.0}
        assert config["motion"]["axis_divergence_guard_ms"] == 3000
        assert config["motion"]["axis_divergence_margin_deg"] == 10.0
        assert config["motion"]["command_envelope_ratio"] == 0.65
        assert config["motion"]["pitch_max_delta_us"] == 140
        assert config["motion"]["pitch_min_drive_us"] == 90
        expected_fire_polarity = {
            "relay_profile": "two_channel_active_low" if turret_id == "turret_4" else "single_channel_ch3_active_high",
            "relay_active_low": True,
            "relay_ch1_active_low": True,
            "relay_ch2_active_low": True,
            "relay_ch3_active_low": turret_id == "turret_4",
        }
        for key, value in expected_fire_polarity.items():
            assert config["fire"][key] == value
        for key in (
            "yaw_plus_max_delta_us",
            "yaw_minus_max_delta_us",
            "yaw_plus_min_drive_us",
            "yaw_minus_min_drive_us",
        ):
            assert key in config["motion"]
        if layout["side"].endswith("right"):
            assert 280 <= config["motion"]["yaw_plus_max_delta_us"] <= 420
            assert 260 <= config["motion"]["yaw_minus_max_delta_us"] <= 420
            assert 240 <= config["motion"]["yaw_plus_min_drive_us"] <= config["motion"]["yaw_plus_max_delta_us"]
            assert 220 <= config["motion"]["yaw_minus_min_drive_us"] <= config["motion"]["yaw_minus_max_delta_us"]
        else:
            assert config["motion"]["yaw_plus_max_delta_us"] == config["motion"]["yaw_max_delta_us"]
            assert config["motion"]["yaw_minus_max_delta_us"] == config["motion"]["yaw_max_delta_us"]
            assert config["motion"]["yaw_plus_min_drive_us"] == config["motion"]["yaw_min_drive_us"]
            assert config["motion"]["yaw_minus_min_drive_us"] == config["motion"]["yaw_min_drive_us"]
        assert "wifi" not in config
        assert "host" not in config.get("mqtt", {})
        assert "password" not in config.get("mqtt", {})
        assert set(presets["presets"]) == {"lane_sweep", "two_point_bounce", "telegraph_column"}
        for preset_id, preset in presets["presets"].items():
            assert preset["fire_ms"] == (2000 if preset_id == "lane_sweep" else 1500)
            for point in preset["points"]:
                assert point["x"] == 0.0
        assert presets["presets"]["lane_sweep"]["move_timeout_ms"] == 20000
        assert 500 <= presets["presets"]["lane_sweep"]["dwell_ms"] <= 2000
        assert presets["presets"]["lane_sweep"]["fire_ms"] == 2000
        lane_points = presets["presets"]["lane_sweep"]["points"]
        assert len(lane_points) == 2
        assert lane_points[0]["y"] > 0
        assert lane_points[1]["y"] < 0
        for point in lane_points:
            assert isinstance(point["z"], int | float)


def test_turret_fleet_pattern_engine_runs_btb_726_readable_mvp_steps() -> None:
    control = read("firmware/turret_fleet/control/turret_control.cpp")
    header = read("firmware/turret_fleet/control/turret_control.h")
    pattern_h = read("firmware/turret_fleet/control/patterns/pattern_plan.h")
    pattern_cpp = read("firmware/turret_fleet/control/patterns/pattern_plan.cpp")
    pattern_module = pattern_h + "\n" + pattern_cpp

    for token in [
        "PATTERN_LANE_SWEEP",
        "PATTERN_TWO_POINT_BOUNCE",
        "PATTERN_TELEGRAPH_COLUMN",
        "compileLaneSweepPattern",
        "compileTwoPointBouncePattern",
        "compileTelegraphColumnPattern",
        "updatePattern()",
        "beginPatternStep",
        "completePattern",
        "abortPattern",
        "clearPatternState",
        "PATTERN_STEP_FIRE_MOVE",
        "addSweep",
        "ensurePatternSweepFire",
        "selectionSeed",
        "selected_point_index",
        "allowDuringFire",
        "preemptActivePattern",
        "validatePatternPlanEnvelope",
        "validatePatternPointEnvelope",
    ]:
        assert token in control or token in header or token in pattern_module

    assert "PatternPlan patternPlan_" in header
    assert "Pattern planning is intentionally pure" in pattern_h
    assert "compilePatternPlan" in control
    assert 'strcmp(patternId, "lane_sweep") == 0' in pattern_cpp
    assert 'strcmp(patternId, "two_point_bounce") == 0' in pattern_cpp
    assert 'strcmp(patternId, "telegraph_column") == 0' in pattern_cpp
    assert "pattern rejected: unsupported pattern_id" in control
    assert "outside safe command envelope margin" in control
    assert "pattern preempted by " in control
    assert 'preemptActivePattern("pattern", source, true);' in control
    pattern_command_body = control.split("bool TurretControl::handlePatternCommand", 1)[1].split(
        "bool TurretControl::handleCommandJson", 1
    )[0]
    assert "The motion safety gate must run after the first pattern point" in pattern_command_body
    assert "if (!ensureMotionSafetyForTracking(source)) return;" not in pattern_command_body
    assert "applyPatternPoint()" in pattern_command_body
    assert 'preemptActivePattern("target", source, true);' in control
    assert "target rejected during active pattern; send interrupt=true to override" not in control
    assert 'patternState_ = "DWELL"' in control
    assert 'patternState_ = "FIRING"' in control
    assert 'patternState_ = "FIRE_MOVE"' in control
    assert 'patternState_ = "WAIT_FIRE_SAFE"' in control
    assert 'startFireSequence(patternPlan_.fireMs, "PATTERN(fire)")' in control
    assert "plan.loopCount = normalizePatternLoopCount(params, 1);" in pattern_cpp
    assert "if (!plan.addSweep(1, true)) return false;" in pattern_cpp
    assert "if (!plan.addStep(PATTERN_STEP_DWELL, 1, plan.dwellMs)) return false;" in pattern_cpp
    assert "Keep the final return edge in PATTERN long enough" in pattern_cpp
    assert control.count("ensurePatternSweepFire(patternPlan_.fireMs);") == 1
    assert "bool TurretControl::patternSweepYawReached() const" in control
    assert "if (patternSweepYawReached())" in control
    assert 'stopPatternSweepFireAtEndpoint("endpoint")' in control
    assert '"PATTERN(sweep_fire)"' in control
    assert "keepMotionTracking" in control
    assert 'postFireMode_ = "PATTERN";' in control
    assert 'doc["pattern_step_index"]' in control
    assert 'doc["pattern_step_type"]' in control
    assert 'doc["pattern_step_count"]' in control
    assert 'doc["pattern_loop_index"]' in control
    assert 'doc["pattern_loop_count"]' in control
    assert 'doc["pattern_last_error"]' in control
    assert 'clearPatternState("hold")' in control
    assert 'clearPatternState("dead")' in control
    assert 'doc["interrupt"] | false' not in control


def test_fleet_mqtt_helper_builds_pattern_payload_for_turret_2() -> None:
    result = subprocess.run(
        [
            sys.executable,
            str(ROOT / "scripts/turret_fleet/mqtt_command.py"),
            "--dry-run",
            "turret_2",
            "pattern",
            "two_point_bounce",
            "--frame-id",
            "boss_stage_v1",
            "--point",
            "0",
            "0.35",
            "-0.6",
            "--point",
            "0",
            "-0.35",
            "-0.6",
        "--loop",
        "2",
        "--dwell-ms",
        "700",
        "--fire-ms",
            "300",
        ],
        cwd=ROOT,
        check=True,
        text=True,
        capture_output=True,
    )

    lines = result.stdout.splitlines()
    assert "topic=battlebang/turrets/turret_2/command" in lines
    payload_line = next(line for line in lines if line.startswith("payload="))
    payload = json.loads(payload_line.removeprefix("payload="))

    assert payload["command"] == "pattern"
    assert payload["pattern_id"] == "two_point_bounce"
    assert payload["frame_id"] == "boss_stage_v1"
    assert payload["ttl_ms"] == 3000
    assert payload["params"]["loop"] == 2
    assert payload["params"]["dwell_ms"] == 700
    assert payload["params"]["fire_ms"] == 300
    assert payload["params"]["points"] == [
        {"x": 0.0, "y": 0.35, "z": -0.6},
        {"x": 0.0, "y": -0.35, "z": -0.6},
    ]

    preset_result = subprocess.run(
        [
            sys.executable,
            str(ROOT / "scripts/turret_fleet/mqtt_command.py"),
            "--dry-run",
            "turret_2",
            "pattern",
            "lane_sweep",
            "--frame-id",
            "boss_stage_v1",
        ],
        cwd=ROOT,
        check=True,
        text=True,
        capture_output=True,
    )
    preset_payload_line = next(line for line in preset_result.stdout.splitlines() if line.startswith("payload="))
    preset_payload = json.loads(preset_payload_line.removeprefix("payload="))
    assert preset_payload["pattern_id"] == "lane_sweep"
    assert "points" not in preset_payload["params"]

    telegraph_result = subprocess.run(
        [
            sys.executable,
            str(ROOT / "scripts/turret_fleet/mqtt_command.py"),
            "--dry-run",
            "turret_2",
            "pattern",
            "telegraph_column",
            "--frame-id",
            "boss_stage_v1",
            "--point-index",
            "1",
            "--no-random",
        ],
        cwd=ROOT,
        check=True,
        text=True,
        capture_output=True,
    )
    telegraph_payload_line = next(line for line in telegraph_result.stdout.splitlines() if line.startswith("payload="))
    telegraph_payload = json.loads(telegraph_payload_line.removeprefix("payload="))
    assert telegraph_payload["pattern_id"] == "telegraph_column"
    assert telegraph_payload["params"]["point_index"] == 1
    assert telegraph_payload["params"]["random"] is False


def test_fleet_e2e_mqtt_harness_covers_modes_and_readable_patterns() -> None:
    import importlib.util

    script_path = ROOT / "scripts/turret_fleet/e2e_mqtt_test.py"
    script = script_path.read_text(encoding="utf-8")
    bin_helper = read("bin/turret")

    for token in [
        "run_target",
        "run_fire",
        "run_idle",
        "run_dead",
        "run_hold",
        "run_home",
        "lane_sweep",
        "two_point_bounce",
        "telegraph_column",
        "FIRE_MOVE",
        "fire_after_aim_reached",
        "fire_yaw_motion_deg",
        "lane_round_trip_seen",
        "final-hold",
        "--allow-live-fire",
        "--between-sleep-s",
        "--lane-fire-yaw-motion-deg",
        "--json-report",
    ]:
        assert token in script

    assert "fleet-e2e" in bin_helper
    assert "e2e_mqtt_test.py" in bin_helper

    help_result = subprocess.run(
        [sys.executable, str(script_path), "--help"],
        cwd=ROOT,
        check=True,
        text=True,
        capture_output=True,
    )
    assert "--allow-live-fire" in help_result.stdout
    assert "--profile-file" in help_result.stdout
    assert "--patterns-file" in help_result.stdout
    assert "--json-report" in help_result.stdout

    spec = importlib.util.spec_from_file_location("e2e_mqtt_test", script_path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)

    args = module.build_parser().parse_args(["turret_2", "--host", "COMMAND_CENTER_IP_OR_DNS"])
    assert args.target_point == [0.0, -0.5, -0.6]
    assert args.allow_live_fire is False
    assert module.E2E_PATTERN_IDS == ("lane_sweep", "two_point_bounce", "telegraph_column")
    assert module.fire_active({"fire_state": "FIRING"}) is True
    assert module.terminal_safe({"fire_state": "SAFE_OFF", "mode": "WAIT_COMMAND", "pattern_state": "IDLE"}) is True
    assert module.axis_range(
        [{"motion_state": {"yaw_current_deg": -1.0}}, {"motion_state": {"yaw_current_deg": 2.5}}],
        "yaw_current_deg",
    ) == 3.5
    assert module.sign_changes([2.0, -2.0, 2.0]) == 2


def test_repeat_lane_sweep_defaults_to_random_one_at_a_time_turrets_1_2_3_4() -> None:
    result = subprocess.run(
        [
            sys.executable,
            str(ROOT / "scripts/turret_fleet/repeat_lane_sweep_live.py"),
            "--dry-run",
            "--random-seed",
            "7",
            "--root",
            "battlebang",
        ],
        cwd=ROOT,
        check=True,
        text=True,
        capture_output=True,
    )

    output = result.stdout
    assert "turrets=turret_1,turret_2,turret_3,turret_4" in output
    assert "order=random" in output
    assert "parallel_loop=disabled" in output
    assert "sequential round=1 order=turret_4,turret_2,turret_1,turret_3" in output
    assert "parallel turret=" not in output
    for turret_id in ["turret_1", "turret_2", "turret_3", "turret_4"]:
        assert f"topic=battlebang/turrets/{turret_id}/command" in output
    first_payload_line = next(line for line in output.splitlines() if "turret=turret_4 " in line)
    payload = json.loads(first_payload_line.split("payload=", 1)[1])
    assert payload["ttl_ms"] == 3000
    assert payload["pattern_instance_id"] == f"lane_sweep-{payload['command_id']}"
    assert payload["params"] == {"return_to": "wait_command"}


def test_repeat_lane_sweep_can_stage_boss_target_start_before_immediate_turret_patterns() -> None:
    result = subprocess.run(
        [
            sys.executable,
            str(ROOT / "scripts/turret_fleet/repeat_lane_sweep_live.py"),
            "--dry-run",
            "--root",
            "battlebang",
            "--boss-id",
            "boss_target_6809477249D0",
            "--count",
            "1",
        ],
        cwd=ROOT,
        check=True,
        text=True,
        capture_output=True,
    )

    output = result.stdout
    assert "boss=boss_target_6809477249D0" in output
    assert "dry-run boss opening:" in output
    assert 'topic=battlebang/boss_targets/boss_target_6809477249D0/command payload={"command":"reset"} wait_ready<=10s' in output
    assert 'topic=battlebang/boss_targets/boss_target_6809477249D0/command payload={"command":"start"} start_intro=0s' in output
    assert 'payload={"command":"home"' not in output
    assert "pattern starts immediately" in output
    assert "dry-run boss defeat handling:" in output
    assert 'if boss hp<=0 topic=battlebang/turrets/turret_1/command payload={"command":"dead","command_id":"boss-dead-turret_1-<ms>"}' in output
    assert output.index('payload={"command":"reset"}') < output.index('payload={"command":"start"}')
    assert output.index('payload={"command":"start"}') < output.index("pattern starts immediately")
    assert output.index("dry-run boss opening:") < output.index("dry-run normal cycle:")


def test_repeat_lane_sweep_publishes_turret_dead_when_boss_hp_zero() -> None:
    import importlib.util

    script_path = ROOT / "scripts/turret_fleet/repeat_lane_sweep_live.py"
    spec = importlib.util.spec_from_file_location("repeat_lane_sweep_dead_test", script_path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)

    class FakeClient:
        def __init__(self) -> None:
            self.published: list[tuple[str, dict[str, object]]] = []

        def publish_json(self, topic: str, payload: dict[str, object]) -> None:
            self.published.append((topic, payload))

    client = FakeClient()
    monitor = module.StatusMonitor(
        root="battlebang",
        turrets=["turret_1", "turret_2", "turret_3", "turret_4"],
        boss_id="boss_target_6809477249D0",
    )
    monitor.latest_boss = {
        "mode": "DEFEATED",
        "life_state": "dead",
        "destroyed": True,
        "hp_remaining": 0,
        "hp_max": 10,
    }

    assert module.publish_turret_dead_commands_if_boss_destroyed(client, monitor, root="battlebang") is True
    assert [topic for topic, _payload in client.published] == [
        "battlebang/turrets/turret_1/command",
        "battlebang/turrets/turret_2/command",
        "battlebang/turrets/turret_3/command",
        "battlebang/turrets/turret_4/command",
    ]
    for turret_id, (_topic, payload) in zip(["turret_1", "turret_2", "turret_3", "turret_4"], client.published):
        assert payload["command"] == "dead"
        assert str(payload["command_id"]).startswith(f"boss-dead-{turret_id}-")

    assert module.publish_turret_dead_commands_if_boss_destroyed(client, monitor, root="battlebang") is True
    assert len(client.published) == 4


def test_turret_fleet_mqtt_subscribe_tolerates_live_status_before_suback() -> None:
    import importlib.util

    script_path = ROOT / "scripts/turret_fleet/e2e_mqtt_test.py"
    spec = importlib.util.spec_from_file_location("e2e_mqtt_subscribe_test", script_path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)

    class FakeSocket:
        def __init__(self) -> None:
            self.sent: list[bytes] = []

        def sendall(self, data: bytes) -> None:
            self.sent.append(data)

    publish_body = b"\x00#battlebang/turrets/turret_1/status{}"
    packets = [(0x30, publish_body), (0x90, b"\x00\x01\x00")]
    session = module.MqttSession(host="unused", port=1883, timeout_s=1.0)
    session.sock = FakeSocket()
    session.read_packet = lambda *, deadline: packets.pop(0) if packets else (None, b"")

    session.subscribe("battlebang/boss_targets/boss_target_6809477249D0/status")

    assert session.sock.sent[0].startswith(b"\x82")
    assert packets == []


def test_fleet_e2e_scenarios_pass_against_fake_mqtt_status_stream() -> None:
    import importlib.util

    script_path = ROOT / "scripts/turret_fleet/e2e_mqtt_test.py"
    spec = importlib.util.spec_from_file_location("e2e_mqtt_test_fake", script_path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    module.drain_statuses = lambda *_args, **_kwargs: None

    status_topic = "battlebang/turrets/turret_2/status"
    command_topic = "battlebang/turrets/turret_2/command"

    def status(
        cid: str,
        *,
        mode: str,
        yaw: float = 0.0,
        pitch: float = 0.0,
        fire_state: str = "SAFE_OFF",
        pattern_state: str = "IDLE",
        step_type: str = "",
        tracking: bool = False,
        aim_reached: bool = True,
        target_y: float | None = None,
    ) -> dict[str, object]:
        doc: dict[str, object] = {
            "last_command_id": cid,
            "mode": mode,
            "fire_state": fire_state,
            "pattern_state": pattern_state,
            "pattern_step_type": step_type,
            "last_error": "",
            "motion_state": {
                "yaw_current_deg": yaw,
                "pitch_current_deg": pitch,
                "yaw_goal_deg": yaw,
                "pitch_goal_deg": pitch,
                "tracking_active": tracking,
                "aim_reached": aim_reached,
            },
            "fire_output_state": {
                "esc_command_us": 1000 if fire_state == "SAFE_OFF" else 1200,
                "esc_stop_us_config": 1000,
                "relay_ch1_on": fire_state != "SAFE_OFF",
                "relay_ch2_on": False,
                "relay_ch3_on": False,
            },
        }
        if target_y is not None:
            doc["aim_state"] = {"last_target_input": {"x": 0.0, "y": target_y, "z": -0.6}}
        return doc

    class FakeClient:
        def __init__(self) -> None:
            self.queue: list[dict[str, object]] = [status("", mode="WAIT_COMMAND", yaw=0.0, pitch=0.0)]
            self.published: list[tuple[str, dict[str, object]]] = []

        def publish_json(self, topic: str, payload: dict[str, object]) -> None:
            self.published.append((topic, payload.copy()))
            cid = str(payload["command_id"])
            command = payload["command"]
            if command == "hold":
                self.queue.extend([status(cid, mode="WAIT_COMMAND", tracking=False)])
            elif command == "target":
                self.queue.extend([status(cid, mode="TARGET", yaw=2.0, pitch=-3.0, tracking=True)])
            elif command == "idle":
                self.queue.extend([status(cid, mode="IDLE", yaw=1.0, pitch=-1.0)])
            elif command == "dead":
                self.queue.extend([status(cid, mode="DEAD", pitch=45.0, fire_state="SAFE_OFF")])
            elif command == "fire":
                self.queue.extend(
                    [
                        status(cid, mode="FIRE", fire_state="FIRING"),
                        status(cid, mode="WAIT_COMMAND", fire_state="SAFE_OFF"),
                    ]
                )
            elif command == "pattern":
                pattern_id = payload["pattern_id"]
                if pattern_id == "lane_sweep":
                    self.queue.extend(
                        [
                            status(
                                cid,
                                mode="PATTERN",
                                yaw=-12.0,
                                fire_state="FIRING",
                                pattern_state="FIRE_MOVE",
                                step_type="FIRE_MOVE",
                                tracking=True,
                                aim_reached=False,
                                target_y=-0.75,
                            ),
                            status(
                                cid,
                                mode="PATTERN",
                                yaw=-12.0,
                                fire_state="SAFE_OFF",
                                pattern_state="DWELL",
                                step_type="DWELL",
                                tracking=False,
                                aim_reached=True,
                                target_y=-0.75,
                            ),
                            status(
                                cid,
                                mode="PATTERN",
                                yaw=8.0,
                                fire_state="FIRING",
                                pattern_state="FIRE_MOVE",
                                step_type="FIRE_MOVE",
                                tracking=True,
                                aim_reached=False,
                                target_y=1.0,
                            ),
                            status(cid, mode="WAIT_COMMAND", yaw=8.0, fire_state="SAFE_OFF", target_y=1.0),
                        ]
                    )
                elif pattern_id == "two_point_bounce":
                    self.queue.extend(
                        [
                            status(cid, mode="PATTERN", pattern_state="DWELL", target_y=0.75),
                            status(
                                cid,
                                mode="PATTERN",
                                fire_state="FIRING",
                                pattern_state="FIRING",
                                step_type="FIRE",
                                aim_reached=True,
                                target_y=-0.5,
                            ),
                            status(cid, mode="WAIT_COMMAND", fire_state="SAFE_OFF", target_y=-0.5),
                        ]
                    )
                else:
                    self.queue.extend(
                        [
                            status(cid, mode="PATTERN", pattern_state="DWELL", target_y=0.0),
                            status(
                                cid,
                                mode="PATTERN",
                                fire_state="FIRING",
                                pattern_state="FIRING",
                                step_type="FIRE",
                                aim_reached=True,
                                target_y=0.0,
                            ),
                            status(cid, mode="WAIT_COMMAND", fire_state="SAFE_OFF", target_y=0.0),
                        ]
                    )

        def read_publish(self, *, deadline: float) -> tuple[str, str] | None:  # noqa: ARG002
            if not self.queue:
                return None
            return status_topic, json.dumps(self.queue.pop(0))

    args = module.build_parser().parse_args(
        [
            "turret_2",
            "--host",
            "COMMAND_CENTER_IP_OR_DNS",
            "--allow-live-fire",
            "--target-observe-s",
            "0.001",
            "--idle-observe-s",
            "0.001",
            "--dead-observe-s",
            "0.001",
            "--pattern-timeout-s",
            "0.001",
        ]
    )
    client = FakeClient()

    results = [module.run_hold(client, command_topic, status_topic)]
    client.queue.append(status("", mode="WAIT_COMMAND", yaw=0.0, pitch=0.0))
    results.extend(
        [
            module.run_target(client, command_topic, status_topic, args),
            module.run_fire(client, command_topic, status_topic, args),
            module.run_idle(client, command_topic, status_topic, args),
            module.run_dead(client, command_topic, status_topic, args),
            module.run_pattern(client, command_topic, status_topic, "lane_sweep", args),
            module.run_pattern(client, command_topic, status_topic, "two_point_bounce", args),
            module.run_pattern(client, command_topic, status_topic, "telegraph_column", args),
        ]
    )

    assert [result.status for result in results] == ["PASS"] * len(results)
    assert [payload["command"] for _topic, payload in client.published] == [
        "hold",
        "target",
        "fire",
        "idle",
        "dead",
        "pattern",
        "pattern",
        "pattern",
    ]


def test_pattern_presets_are_runtime_configurable_over_mqtt_and_nvs() -> None:
    config_h = read("firmware/turret_fleet/config/runtime_config.h")
    config_cpp = read("firmware/turret_fleet/config/runtime_config.cpp")
    pattern_cpp = read("firmware/turret_fleet/control/patterns/pattern_plan.cpp")
    helper = read("scripts/turret_fleet/mqtt_command.py")
    provision = read("scripts/turret_fleet/provision.py")

    assert "String patternPresetsJson;" in config_h
    assert 'doc.containsKey("patterns")' in config_cpp
    assert "validatePatternPresets" in config_cpp
    assert 'prefs.getString("pattern_json"' in config_cpp
    assert 'prefs.putString("pattern_json"' in config_cpp
    assert "copyConfiguredPresetParams" in pattern_cpp
    assert "config.patternPresetsJson.length()" in pattern_cpp
    assert '"--profile-file"' in helper
    assert "load_profile_file" in helper
    assert '"--patterns-file"' in helper
    assert "load_patterns_file" in helper
    assert "DEFAULT_PROFILE_DIR" in provision
    assert "TURRET_FLEET_PROFILE_FILE" in provision
    assert "TURRET_FLEET_PATTERN_PRESETS_FILE" in provision

    result = subprocess.run(
        [
            sys.executable,
            str(ROOT / "scripts/turret_fleet/mqtt_command.py"),
            "--dry-run",
            "turret_2",
            "config",
            "--patterns-file",
            str(ROOT / "firmware/turret_fleet/pattern_presets/turret_2.json"),
        ],
        cwd=ROOT,
        check=True,
        text=True,
        capture_output=True,
    )
    lines = result.stdout.splitlines()
    assert "topic=battlebang/turrets/turret_2/config" in lines
    payload_line = next(line for line in lines if line.startswith("payload="))
    payload = json.loads(payload_line.removeprefix("payload="))
    assert payload["type"] == "config"
    assert payload["patterns"]["presets"]["two_point_bounce"]["points"] == [
        {"x": 0.0, "y": 0.75, "z": -0.6},
        {"x": 0.0, "y": -0.5, "z": -0.6},
    ]

    full_result = subprocess.run(
        [
            sys.executable,
            str(ROOT / "scripts/turret_fleet/mqtt_command.py"),
            "--dry-run",
            "turret_2",
            "config",
            "--profile-file",
            str(ROOT / "firmware/turret_fleet/profiles/turret_2.json"),
            "--patterns-file",
            str(ROOT / "firmware/turret_fleet/pattern_presets/turret_2.json"),
        ],
        cwd=ROOT,
        check=True,
        text=True,
        capture_output=True,
    )
    full_payload_line = next(line for line in full_result.stdout.splitlines() if line.startswith("payload="))
    full_payload = json.loads(full_payload_line.removeprefix("payload="))
    assert full_payload["type"] == "config"
    assert full_payload["turret_id"] == "turret_2"
    assert full_payload["pose"] == {
        "x_cm": -300.0,
        "y_cm": 200.0,
        "z_cm": 134.5,
        "default_target_z_cm": 70.0,
    }
    assert full_payload["motion"]["pitch_max_delta_us"] == 140
    assert full_payload["motion"]["pitch_min_drive_us"] == 90
    assert full_payload["motion"]["command_envelope_ratio"] == 0.65
    assert full_payload["motion"]["limits"] == {
        "yaw_min_deg": -50.0,
        "yaw_max_deg": 50.0,
        "pitch_min_deg": -60.0,
        "pitch_max_deg": 70.0,
    }
    assert full_payload["motion"]["yaw_plus_max_delta_us"] == 420
    assert full_payload["motion"]["yaw_minus_max_delta_us"] == 420
    assert full_payload["motion"]["yaw_plus_min_drive_us"] == 400
    assert full_payload["motion"]["yaw_minus_min_drive_us"] == 400
    assert full_payload["fire"]["relay_active_low"] is True
    assert full_payload["fire"]["relay_ch1_active_low"] is True
    assert full_payload["fire"]["relay_ch2_active_low"] is True
    assert full_payload["fire"]["relay_ch3_active_low"] is False
    assert full_payload["fire"]["relay_profile"] == "single_channel_ch3_active_high"
    assert 500 <= full_payload["patterns"]["presets"]["lane_sweep"]["dwell_ms"] <= 2000
    assert full_payload["patterns"]["presets"]["lane_sweep"]["fire_ms"] == 2000
    lane_points = full_payload["patterns"]["presets"]["lane_sweep"]["points"]
    assert len(lane_points) == 2
    assert lane_points[0]["y"] > 0
    assert lane_points[1]["y"] < 0
    for point in lane_points:
        assert isinstance(point["z"], int | float)
    assert full_payload["patterns"]["presets"]["telegraph_column"]["random"] is True
    assert full_payload["patterns"]["presets"]["telegraph_column"]["points"] == [
        {"x": 0.0, "y": 0.0, "z": -0.6},
        {"x": 0.0, "y": 0.5, "z": -0.6},
        {"x": 0.0, "y": -0.5, "z": -0.6},
    ]


def test_fleet_provision_auto_loads_per_turret_config_and_patterns(tmp_path: Path) -> None:
    env_file = tmp_path / "fleet.env"
    env_file.write_text(
        "\n".join(
            [
                "TURRET_FLEET_WIFI_SSID=TEST_WIFI",
                "TURRET_FLEET_WIFI_PASSWORD=TEST_PASSWORD",
                "TURRET_FLEET_MQTT_HOST=COMMAND_CENTER_IP_OR_DNS",
                "TURRET_FLEET_MQTT_PORT=1883",
                "TURRET_FLEET_MQTT_ROOT=battlebang",
            ]
        )
        + "\n",
        encoding="utf-8",
    )

    result = subprocess.run(
        [
            sys.executable,
            str(ROOT / "scripts/turret_fleet/provision.py"),
            "build-config",
            "2",
            "--env-file",
            str(env_file),
        ],
        cwd=ROOT,
        check=True,
        text=True,
        capture_output=True,
    )
    payload = json.loads(result.stdout)

    assert payload["type"] == "provision"
    assert payload["turret_id"] == "turret_2"
    assert payload["stage_id"] == "boss_stage_v1"
    assert payload["pose"] == {
        "x_cm": -300.0,
        "y_cm": 200.0,
        "z_cm": 134.5,
        "default_target_z_cm": 70.0,
    }
    assert payload["motion"]["limits"] == {
        "yaw_min_deg": -50.0,
        "yaw_max_deg": 50.0,
        "pitch_min_deg": -60.0,
        "pitch_max_deg": 70.0,
    }
    assert payload["wifi"] == {"ssid": "TEST_WIFI", "password": "***"}
    assert payload["mqtt"]["host"] == "COMMAND_CENTER_IP_OR_DNS"
    assert payload["patterns"]["presets"]["telegraph_column"]["random"] is True
    assert payload["patterns"]["presets"]["telegraph_column"]["points"] == [
        {"x": 0.0, "y": 0.0, "z": -0.6},
        {"x": 0.0, "y": 0.5, "z": -0.6},
        {"x": 0.0, "y": -0.5, "z": -0.6},
    ]


def test_fleet_provision_uses_dotenv_serial_port_before_auto_detect() -> None:
    provision = read("scripts/turret_fleet/provision.py")

    assert (
        'args.port or env_first(env, "TURRET_FLEET_SERIAL_PORT", "TURRET_SERIAL_PORT") or auto_detect_port(args.pio)'
        in provision
    )


def test_fleet_docs_mask_lab_broker_ip_and_define_mqtt_broker_host() -> None:
    paths = [
        "README.md",
        "bin/turret",
        "scripts/turret_fleet/README.md",
        "firmware/turret_fleet/.env.turret_fleet.example",
        "firmware/turret_fleet/README.md",
        "firmware/turret_fleet/docs/context.md",
        "firmware/turret_fleet/docs/github-actions.md",
        "firmware/turret_fleet/docs/mqtt-http-contract.md",
        "firmware/turret_fleet/docs/usage.md",
        "firmware/turret_fleet/mqtt/README.md",
        "firmware/turret_fleet/ota/README.md",
    ]
    combined = "\n".join(read(path) for path in paths)

    assert PRIVATE_LAB_PREFIX not in combined
    assert "$MQTT_BROKER_HOST" in combined
    assert "host is the MQTT broker" in combined
    assert "COMMAND_CENTER_IP_OR_DNS" in combined


def test_fleet_provision_can_use_dev_runtime_id_with_reused_physical_profile(tmp_path: Path) -> None:
    env_file = tmp_path / ".env.turret_fleet"
    env_file.write_text(
        "\n".join(
            [
                "TURRET_FLEET_WIFI_SSID=TEST_WIFI",
                "TURRET_FLEET_WIFI_PASSWORD=TEST_PASSWORD",
                "TURRET_FLEET_MQTT_HOST=COMMAND_CENTER_IP_OR_DNS",
                "TURRET_FLEET_MQTT_PORT=1883",
                "TURRET_FLEET_MQTT_ROOT=battlebang",
                "TURRET_FLEET_GROUP=dev",
                "TURRET_FLEET_STAGE_ID=dev_stage_01",
                "TURRET_FLEET_FRAME_ID=dev_stage_01",
                "TURRET_FLEET_CONFIG_VERSION=2001",
            ]
        )
        + "\n",
        encoding="utf-8",
    )

    result = subprocess.run(
        [
            sys.executable,
            str(ROOT / "scripts/turret_fleet/provision.py"),
            "build-config",
            "turret_dev_01",
            "--profile-id",
            "turret_1",
            "--env-file",
            str(env_file),
        ],
        cwd=ROOT,
        check=True,
        text=True,
        capture_output=True,
    )
    payload = json.loads(result.stdout)

    assert payload["type"] == "provision"
    assert payload["device_id"] == "turret_dev_01"
    assert payload["turret_id"] == "turret_dev_01"
    assert payload["group"] == "dev"
    assert payload["stage_id"] == "dev_stage_01"
    assert payload["coordinate_frame"]["frame_id"] == "dev_stage_01"
    assert payload["pose"] == {
        "x_cm": -300.0,
        "y_cm": 0.0,
        "z_cm": 134.5,
        "default_target_z_cm": 70.0,
    }
    assert payload["motion"]["pitch_max_delta_us"] == 140
    assert payload["patterns"]["presets"]["lane_sweep"]["points"][0] == {
        "x": 0.0,
        "y": 1.2,
        "z": 0.5,
    }


def test_serial_and_mqtt_json_buffers_are_heap_backed_to_avoid_loop_stack_overflow() -> None:
    main = read("firmware/turret_fleet/main.cpp")
    mqtt = read("firmware/turret_fleet/mqtt/mqtt_bus.cpp")
    config = read("firmware/turret_fleet/config/runtime_config.cpp")

    assert "DynamicJsonDocument doc(4096);" in main
    assert "DynamicJsonDocument doc(1024);" in main
    assert "const size_t kPayloadLimit = 8192;" in mqtt
    assert "client_.setBufferSize(kPayloadLimit);" in mqtt
    assert "const size_t kStatusDocCapacity = 8192;" in mqtt
    assert "DynamicJsonDocument doc(kStatusDocCapacity);" in mqtt
    assert "status publish failed len=" in mqtt
    assert "DynamicJsonDocument doc(1024);" in mqtt
    assert "const size_t kRuntimeConfigJsonCapacity = 8192;" in config
    assert "DynamicJsonDocument doc(kRuntimeConfigJsonCapacity);" in config
    assert "constexpr size_t kSerialCommandBufferSize = 9216;" in main
    assert "StaticJsonDocument<4096>" not in main
    assert "StaticJsonDocument<4096>" not in mqtt
    assert "StaticJsonDocument<4096>" not in config


def test_fleet_applies_power_saving_without_disabling_brownout_detector() -> None:
    main = read("firmware/turret_fleet/main.cpp")

    assert "btStop();" in main
    assert "setCpuFrequencyMhz(80);" in main
    assert "RTC_CNTL_BROWN_OUT" not in main
    assert "WRITE_PERI_REG" not in main


def test_brownout_boot_locks_motion_and_fire_until_explicit_recovery() -> None:
    main = read("firmware/turret_fleet/main.cpp")
    control = read("firmware/turret_fleet/control/turret_control.cpp")
    header = read("firmware/turret_fleet/control/turret_control.h")
    mqtt = read("firmware/turret_fleet/mqtt/mqtt_bus.cpp")
    helper = read("scripts/turret_fleet/mqtt_command.py")

    assert "fireRecoveryRequiredAtBoot = loadFireRecoveryMarker();" in main
    assert "recoveryLockoutRequiredAtBoot = loadRecoveryLockoutMarker();" in main
    assert "otaRebootInhibitRequiredAtBoot = consumeOtaRebootMarker();" in main
    assert '#include "ota/reboot_marker.h"' in main
    assert '#include "../ota/reboot_marker.h"' in mqtt
    assert "bootSafetyLockoutRequired = bootResetReason == ESP_RST_BROWNOUT" in main
    assert "!otaRebootInhibitRequiredAtBoot" in main
    assert "fireHardwareEnabled" not in read("firmware/turret_fleet/config/runtime_config.h")
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
    assert "fireHardOffDelayMs" in control
    assert "fireHardOffAtMs_" in header
    assert "hard off after wall-clock cap" in control
    assert 'doc["fire_hard_off_remaining_ms"]' in control
    assert 'kRecoveryLockoutMarkerKey = "recover_req"' in control
    assert "writeRecoveryLockoutMarker(true)" in control or "writeRecoveryLockoutMarker(active)" in control
    assert "writeRecoveryLockoutMarker(false)" in control
    assert "commandBlockedByBrownoutLockout" in control
    assert "command rejected after brownout lockout" in control
    assert "fire rejected after brownout lockout" in control
    assert "recover rejected: feedback outside stable soft window" in control
    assert "brownout recover soft-window recovery requested" in control
    assert "recoverMotionSoftWindow(source)" in control
    assert 'motion["brownout_lockout"] = brownoutLockoutActive_;' in control
    assert '"recover"' in helper


def test_yaw_drive_can_be_tuned_asymmetrically_per_pwm_direction() -> None:
    config_h = read("firmware/turret_fleet/config/runtime_config.h")
    config_cpp = read("firmware/turret_fleet/config/runtime_config.cpp")
    control = read("firmware/turret_fleet/control/turret_control.cpp")
    helper = read("scripts/turret_fleet/mqtt_command.py")

    for field in [
        "yawPlusMaxDeltaUs",
        "yawMinusMaxDeltaUs",
        "yawPlusMinDriveUs",
        "yawMinusMinDriveUs",
    ]:
        assert field in config_h
    for key in [
        'motion["yaw_plus_max_delta_us"]',
        'motion["yaw_minus_max_delta_us"]',
        'motion["yaw_plus_min_drive_us"]',
        'motion["yaw_minus_min_drive_us"]',
    ]:
        assert key in config_cpp
    for key in [
        'prefs.getUShort("yaw_p_max"',
        'prefs.getUShort("yaw_m_max"',
        'prefs.getUShort("yaw_p_min"',
        'prefs.getUShort("yaw_m_min"',
        'prefs.putUShort("yaw_p_max"',
        'prefs.putUShort("yaw_m_max"',
        'prefs.putUShort("yaw_p_min"',
        'prefs.putUShort("yaw_m_min"',
    ]:
        assert key in config_cpp
    assert "kMotionYawDeltaMaxUs = 450" in config_cpp
    assert "effectiveYawPlusMaxDeltaUs" in config_cpp
    assert "invalidOptionalYawMinDrive(next.yawPlusMinDriveUs, effectiveYawPlusMaxDeltaUs)" in config_cpp
    assert "plusPwmDirection = output < 0.0f" in control
    assert "stopUs + delta" in control
    assert "config_.yawPlusMaxDeltaUs > 0" in control
    assert "config_.yawMinusMaxDeltaUs > 0" in control
    assert '"--yaw-plus-max-delta-us"' in helper
    assert '"--yaw-minus-min-drive-us"' in helper


def test_ota_polling_is_command_center_approved_and_safe_state_gated() -> None:
    main = read("firmware/turret_fleet/main.cpp")
    mqtt = read("firmware/turret_fleet/mqtt/mqtt_bus.cpp")
    helper = read("scripts/turret_fleet/mqtt_command.py")
    publisher = read("scripts/turret_fleet/publish_mqtt_manifest.py")
    bin_helper = read("bin/turret")

    assert "pollConfiguredOta" in main
    assert "commandCenterApprovesPolledOta" in main
    assert "manifest.build != config.otaDesiredBuild" in main
    assert "config.otaCommandCenterControlled" in main
    assert "config.otaApplyOnlyInSafeState && !control.isSafeForOta()" in main
    assert 'if (mode_ == "HOME" || mode_ == "TARGET")' in read("firmware/turret_fleet/control/turret_control.cpp")
    assert "aimReached() && !targetSlewActive_" in read("firmware/turret_fleet/control/turret_control.cpp")
    assert "selectedMotionAxis_ == 'N'" in read("firmware/turret_fleet/control/turret_control.cpp")
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
    config_h = read("firmware/turret_fleet/config/runtime_config.h")
    config_cpp = read("firmware/turret_fleet/config/runtime_config.cpp")
    control = read("firmware/turret_fleet/control/turret_control.cpp")
    docs = read("firmware/turret_fleet/README.md") + read("firmware/turret_fleet/docs/mqtt-http-contract.md")

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
    config_cpp = read("firmware/turret_fleet/config/runtime_config.cpp")

    assert "next.yawMaxDeg - next.yawMinDeg > 150.0f" in config_cpp
    assert "next.pitchMaxDeg - next.pitchMinDeg > 150.0f" in config_cpp
    assert "next.deadPitchDeg < next.pitchMinDeg || next.deadPitchDeg > next.pitchMaxDeg" in config_cpp


def test_fleet_motion_commands_use_runtime_configurable_inner_envelope() -> None:
    control = read("firmware/turret_fleet/control/turret_control.cpp")
    header = read("firmware/turret_fleet/control/turret_control.h")
    config_h = read("firmware/turret_fleet/config/runtime_config.h")
    config_cpp = read("firmware/turret_fleet/config/runtime_config.cpp")

    assert "float commandEnvelopeRatio = 0.65f;" in config_h
    assert 'motion["command_envelope_ratio"]' in config_cpp
    assert 'prefs.getFloat("cmd_env"' in config_cpp
    assert 'prefs.putFloat("cmd_env"' in config_cpp
    assert "next.commandEnvelopeRatio < 0.50f" in config_cpp
    assert "next.commandEnvelopeRatio > 0.85f" in config_cpp
    assert "const float kDefaultCommandEnvelopeRatio = 0.65f;" in control
    assert "commandEnvelopeEdge(config_.yawMinDeg, config_.homeYawDeg, config_.commandEnvelopeRatio)" in control
    assert "commandEnvelopeEdge(config_.yawMaxDeg, config_.homeYawDeg, config_.commandEnvelopeRatio)" in control
    assert "commandEnvelopeEdge(config_.pitchMinDeg, config_.homePitchDeg, config_.commandEnvelopeRatio)" in control
    assert "commandEnvelopeEdge(config_.pitchMaxDeg, config_.homePitchDeg, config_.commandEnvelopeRatio)" in control
    assert "return clampf(value, yawCommandMinDeg(), yawCommandMaxDeg());" in control
    assert "return clampf(value, pitchCommandMinDeg(), pitchCommandMaxDeg());" in control
    for symbol in [
        "float yawCommandMinDeg() const;",
        "float yawCommandMaxDeg() const;",
        "float pitchCommandMinDeg() const;",
        "float pitchCommandMaxDeg() const;",
    ]:
        assert symbol in header


def test_fleet_direct_fire_requires_current_pose_inside_safe_window() -> None:
    control = read("firmware/turret_fleet/control/turret_control.cpp")
    fire_body = control.split("bool TurretControl::startFireFromCommand", 1)[1].split(
        "bool TurretControl::handlePatternCommand", 1
    )[0]

    assert "updateCurrentAngles();" in fire_body
    assert "if (!motionInsideSoftWindow())" in fire_body
    assert "fire rejected: pose outside calibrated safe envelope" in fire_body
    assert "forceFireOutputsSafeOff();" in fire_body
    assert "motionSafetyInhibited_ = true;" in fire_body


def test_fleet_allows_only_inward_yaw_recovery_from_soft_limit() -> None:
    control = read("firmware/turret_fleet/control/turret_control.cpp")
    header = read("firmware/turret_fleet/control/turret_control.h")

    assert "bool yawInwardRecoveryAllowed() const;" in header
    assert "bool TurretControl::yawInwardRecoveryAllowed() const" in control
    assert "yawLowOutside && clampedYawDeg_ > yawCurrentDeg_" in control
    assert "yawHighOutside && clampedYawDeg_ < yawCurrentDeg_" in control
    assert "yaw inward recovery tracking allowed" in control
    assert "bootHomeInwardRecoveryAllowed" in control
    assert "boot home yaw inward recovery enabled" in control
    assert "motionSafetyInhibited_ = !motionInsideSoftWindow();" in control


def test_fleet_patterns_abort_on_axis_divergence_and_hold_reports_unsafe_pose() -> None:
    control = read("firmware/turret_fleet/control/turret_control.cpp")

    assert 'abortPattern(lastError_.length() > 0 ? lastError_.c_str() : "pattern aborted: yaw divergence guard")' in control
    assert 'abortPattern(lastError_.length() > 0 ? lastError_.c_str() : "pattern aborted: pitch divergence guard")' in control
    assert "hold: pose outside calibrated safe envelope" in control
    assert "motionSafetyInhibited_ = true;" in control


def test_fleet_mqtt_helper_builds_direct_commands_and_config_patches() -> None:
    import importlib.util

    path = ROOT / "scripts/turret_fleet/mqtt_command.py"
    spec = importlib.util.spec_from_file_location("mqtt_command", path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    assert module.topic_for("battlebang/", "turret_2", "command") == "battlebang/turrets/turret_2/command"
    args = module.build_parser().parse_args(["--host", "192.0.2.52", "2", "target", "0", "0", "2"])
    suffix, payload = module.build_command_payload(args)
    assert suffix == "command"
    assert payload["command"] == "target"
    assert payload["target"] == {"x": 0.0, "y": 0.0, "z": 2.0}

    args = module.build_parser().parse_args(["--host", "192.0.2.52", "2", "initiate"])
    suffix, payload = module.build_command_payload(args)
    assert suffix == "command"
    assert payload["command"] == "home"

    args = module.build_parser().parse_args(["--host", "192.0.2.52", "2", "recover"])
    suffix, payload = module.build_command_payload(args)
    assert suffix == "command"
    assert payload["command"] == "recover"

    args = module.build_parser().parse_args([
        "--host",
        "192.0.2.52",
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
        "192.0.2.52",
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
        "--yaw-plus-max-delta-us",
        "90",
        "--yaw-minus-max-delta-us",
        "50",
        "--yaw-plus-min-drive-us",
        "70",
        "--yaw-minus-min-drive-us",
        "35",
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
        "https://github.com/KongPedia/battlebang-esp/releases/download/turret-fleet-latest/manifest.json",
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
    assert payload["motion"]["yaw_plus_max_delta_us"] == 90
    assert payload["motion"]["yaw_minus_max_delta_us"] == 50
    assert payload["motion"]["yaw_plus_min_drive_us"] == 70
    assert payload["motion"]["yaw_minus_min_drive_us"] == 35
    assert payload["motion"]["pitch_max_delta_us"] == 40
    assert payload["motion"]["limits"] == {"yaw_min_deg": -75.0, "yaw_max_deg": 75.0}
    assert payload["motion"]["home"] == {"yaw_deg": 0.0, "pitch_deg": 0.0}
    assert payload["ota"]["auto_check_enabled"] is True
    assert payload["ota"]["desired_build"] == 2
    assert payload["ota"]["public_manifest_url"].endswith("/manifest.json")
    assert "yaw_stop_us" not in payload["motion"].get("idle", {})

    args = module.build_parser().parse_args([
        "--host",
        "192.0.2.52",
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
