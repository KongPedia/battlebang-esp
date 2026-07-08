from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_station_active_firmware_layout_identity_and_ota_contract() -> None:
    firmware = read("firmware/station/app/firmware_info.h")
    pio = read("platformio.ini")
    workflow = read(".github/workflows/firmware-ota.yml")
    config = json.loads(read("firmware/station/config.json"))

    assert (ROOT / "firmware/station").is_dir()
    assert (ROOT / "firmware/station/.env.station.example").exists()
    assert (ROOT / "scripts/station/provision.py").exists()
    assert (ROOT / "scripts/station/flash_and_provision.py").exists()
    assert not (ROOT / "src/station").exists()
    assert not (ROOT / "src/TargetStation").exists()

    assert 'BB_STATION_APP_NAME "battlebang-station"' in firmware
    assert 'BB_STATION_HARDWARE "esp32dev-target-station-v1"' in firmware
    assert "station-latest/station-manifest.json" in firmware
    assert config["defaults"]["firmware_app"] == "battlebang-station"
    assert config["defaults"]["firmware_hardware"] == "esp32dev-target-station-v1"
    assert config["defaults"]["station_id"] == "station_01"
    assert config["defaults"]["ota_public_manifest_url"].endswith("/station-latest/station-manifest.json")

    assert "[env:esp32dev_station]" in pio
    assert "+<../firmware/station/**>" in pio
    assert "esp32dev_station_01" not in pio
    assert "esp32dev_station_06" not in pio

    for expected in (
        "esp32dev_station",
        "battlebang-station",
        "esp32dev-target-station-v1",
        "station-manifest.json",
        "station-latest",
        "station-v",
        "BB_STATION_VERSION",
        "firmware/station/app/version_autogen.h",
        'r"^firmware/station/"',
    ):
        assert expected in workflow
    assert '"scripts/station/**"' not in workflow


def test_station_runtime_config_is_nvs_backed_and_mutable_for_station_wifi_mqtt() -> None:
    header = read("firmware/station/config/runtime_config.h")
    source = read("firmware/station/config/runtime_config.cpp")
    docs = read("firmware/station/docs/runtime-config.md")
    env_example = read("firmware/station/.env.station.example")
    provision = read("scripts/station/provision.py")

    for token in (
        "String deviceId",
        "String stationId",
        "String displayName",
        "String stageId",
        "String wifiSsid",
        "String wifiPassword",
        "String mqttHost",
        "String mqttRoot",
        "SensorConfig sensor",
        "LedConfig led",
        "GameplayConfig gameplay",
        "bool otaCommandCenterControlled",
    ):
        assert token in header

    assert 'const char* kConfigNamespace = "station";' in source
    assert "loadCommonRuntimeConfig(prefs, common, commonNvsKeys())" in source
    assert "saveCommonRuntimeConfig(" in source
    assert 'prefs.getString("station_id"' in source
    assert 'prefs.putString("station_id"' in source
    assert 'prefs.getUShort("hit_thr"' in source
    assert 'prefs.putUShort("hit_thr"' in source
    assert 'prefs.getUShort("led_count"' in source
    assert 'prefs.putUShort("led_count"' in source
    assert 'doc.containsKey("config_version")' in source
    assert 'error = "config_version is required";' in source
    assert "incomingVersion < config.configVersion" in source
    assert "validateTopicSegment(config.stationId, \"station_id\", error)" in source
    assert "validatePinProfile(config, error)" in source
    assert "captured_" not in source
    assert "active_" not in source

    for token in (
        "STATION_01_ID=station_01",
        "STATION_06_ID=station_06",
        "STATION_WIFI_SSID=YOUR_WIFI_SSID",
        "STATION_WIFI_PASSWORD=YOUR_WIFI_PASSWORD",
        "STATION_MQTT_HOST=COMMAND_CENTER_IP_OR_DNS",
        "STATION_STAGE_ID=lit4f_260623",
    ):
        assert token in env_example

    assert 'DEFAULT_ENV_FILE = PROJECT_ROOT / "firmware" / "station" / ".env.station"' in provision
    assert '"station_id": station_id' in provision
    assert '"ssid": env_first(env, "STATION_WIFI_SSID", default="")' in provision
    assert '"host": env_first(env, "STATION_MQTT_HOST", default="")' in provision
    assert 'station_id = args.station_id or env_first(env, "STATION_STATION_ID", "STATION_01_ID", default="station_01")' in provision

    assert "`station_id`" in docs
    assert "`wifi.ssid`" in docs
    assert "`mqtt.host`" in docs
    assert "match progress is not stored in NVS" in docs


def test_station_controller_locks_after_first_hit_and_reset_unlocks() -> None:
    controller_h = read("firmware/station/station/station_controller.h")
    controller = read("firmware/station/station/station_controller.cpp")

    assert "enum class Mode : uint8_t { UNCONFIGURED, WAITING, CAPTURED, OTA_PREPARED }" in controller_h
    assert "bool captured_ = false;" in controller_h
    assert "uint32_t captureSequence_ = 0;" in controller_h
    assert "uint32_t lockedIgnoredHits_ = 0;" in controller_h

    assert "if (captured_ && config_.gameplay.lockAfterHit)" in controller
    assert "++lockedIgnoredHits_;" in controller
    assert "if (captured_ && config_.gameplay.lockAfterHit) return false;" in controller
    assert "captured_ = true;" in controller
    assert "++captureSequence_;" in controller
    assert 'emit("captured", source, peak);' in controller
    assert "captured_ = false;" in controller
    assert "lockedIgnoredHits_ = 0;" in controller
    assert 'emit("reset", source, 0);' in controller
    assert 'obj["active"] = captured_;' in controller
    assert 'station["active"] = captured_;' in controller


def test_station_services_capture_and_led_before_network_io() -> None:
    main = read("firmware/station/main.cpp")
    controller_h = read("firmware/station/station/station_controller.h")
    controller = read("firmware/station/station/station_controller.cpp")
    bus_h = read("firmware/station/mqtt/mqtt_bus.h")
    bus = read("firmware/station/mqtt/mqtt_bus.cpp")

    assert "bool deferStateChangeStatusWhileArmed() const;" in controller_h
    assert "return config_.configured && mode_ == Mode::WAITING && !captured_;" in controller
    assert "void loop(bool deferStateChangeStatus = false);" in bus_h
    assert "bool publishStatus(const char* reason);" in bus_h
    assert "wifiClient_.setTimeout(kMqttSocketTimeoutSeconds);" in bus
    assert "if (connectIfNeeded()) client_.loop();" in bus
    assert "if (deferStateChangeStatus) return;" not in bus
    assert "if (!deferStateChangeStatus && station_ != nullptr" in bus
    assert bus.index("if (connectIfNeeded()) client_.loop();") < bus.index("if (!deferStateChangeStatus && station_ != nullptr")
    assert bus.index("if (!deferStateChangeStatus && station_ != nullptr") < bus.index('publishStatus("heartbeat")')
    assert "bool MqttBus::publishStatus(const char* reason)" in bus
    assert 'client_.publish(topics.stationStatus.c_str(), payload.c_str(), true)' in bus
    assert "return false;" in bus
    assert "lastStatusSignature_ = station_->statusSignature();" in bus
    assert bus.index("if (!ok) {") < bus.index("lastStatusSignature_ = station_->statusSignature();")

    loop_body = main.split("void loop()", 1)[1]
    sensor_index = loop_body.index("stationController.loop(now);")
    network_index = loop_body.index("if (networkStarted) {")
    assert sensor_index < network_index
    assert "const bool deferStateChangeStatus = stationController.deferStateChangeStatusWhileArmed();" in main
    assert "wifi.loop(config);" in main
    assert "mqtt.loop(deferStateChangeStatus);" in main
    assert "bool flushPendingMqttStatusIfConnected()" in main
    assert "if (!mqtt.publishStatus(reason.c_str())) return false;" in main
    assert "pendingMqttStatus = false;" in main
    assert "flushPendingMqttStatusIfConnected();" in main
    assert loop_body.index("flushPendingMqttStatusIfConnected();") < loop_body.index("mqtt.loop(deferStateChangeStatus);")
    assert "pollConfiguredOta();" in main
    assert "if (!deferStateChangeStatus) pollConfiguredOta();" not in main
    assert "void publishMqttStatusNowIfConnected(const char* reason)" in main
    assert "if (!mqtt.publishStatus(reason == nullptr ? \"state_changed\" : reason)) publishMqttStatusIfConnected(reason);" in main
    assert 'publishMqttStatusNowIfConnected("ota_downloading");' in main
    assert 'publishMqttStatusNowIfConnected(result.ok ? "ota_rebooting" : "ota_failed");' in main


def test_station_mqtt_matches_fleet_dashboard_demo_station_contract() -> None:
    topics = read("firmware/station/mqtt/topics.cpp")
    bus = read("firmware/station/mqtt/mqtt_bus.cpp")
    readme = read("firmware/station/README.md")

    assert 'joinTopic(root, "devices", "station", config.stationId, "status")' in topics
    assert 'joinTopic(root, "devices", "station", config.stationId, "command")' in topics
    assert 'joinTopic(root, "devices", "station", "all", "ota")' in topics
    assert 'validateTopicSegment(config.stationId, "station_id", error)' in topics

    for token in (
        'doc["schema_version"] = 1;',
        'doc["type"] = "status";',
        'doc["firmware_ts_ms"] = now;',
        'doc["source_uptime_ms"] = now;',
        'doc["device_type"] = "station";',
        'doc["firmware_app"] = BB_STATION_APP_NAME;',
        'doc["firmware_hardware"] = BB_STATION_HARDWARE;',
        'station_->appendStatus(doc.as<JsonObject>());',
        "topics.stationStatus",
    ):
        assert token in bus

    assert "Heavy Blaster" in readme
    assert "does not reuse Heavy Blaster topics" in readme
    assert "battlebang/devices/station/{station_id}/status" in readme
    assert '"active": true' in readme
    assert '"station": {' in readme


def test_station_six_device_usb_flash_and_provisioning_helper_contract() -> None:
    helper = read("scripts/station/flash_and_provision.py")
    provision = read("scripts/station/provision.py")
    stations = json.loads(read("firmware/station/examples/stations.example.json"))
    readme = read("firmware/station/README.md")

    assert [item["station_id"] for item in stations["stations"]] == [
        "station_01",
        "station_02",
        "station_03",
        "station_04",
        "station_05",
        "station_06",
    ]

    for token in (
        "pio run -e esp32dev_station -t upload",
        "--station station_01=/dev/cu.usbserial-110",
        "--station station_06=/dev/cu.usbserial-160",
        "subprocess.run(upload_cmd, check=True",
        "provision.py",
        "--station-id",
        "--serial-port",
    ):
        assert token in helper

    assert "SERIAL_WRITE_CHUNK_BYTES = 96" in provision
    assert "SERIAL_BOOT_SETTLE_S = 4.0" in provision
    assert "for start in range(0, len(encoded), SERIAL_WRITE_CHUNK_BYTES)" in provision

    assert "six Station boards" in readme
    assert "station_01=/dev/cu.usbserial-110" in readme
    assert "station_06=/dev/cu.usbserial-160" in readme
    assert "same firmware image" in readme
