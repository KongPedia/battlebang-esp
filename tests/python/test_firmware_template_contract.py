from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

KEPT_FIRMWARE = {
    "go2": {
        "src": "firmware/go2",
        "target": "firmware/go2/",
        "env_hint": "esp32dev_go2",
    },
    "go2_nixo": {
        "src": "firmware/go2_nixo",
        "target": "firmware/go2_nixo/",
        "env_hint": "esp32dev_go2_nixo",
    },
    "boss_target": {
        "src": "firmware/boss_target",
        "target": "firmware/boss_target/",
        "env_hint": "esp32dev_boss_target",
    },
    "heavy_blaster": {
        "src": "firmware/heavy_blaster",
        "target": "firmware/heavy_blaster/",
        "env_hint": "esp32dev_heavy_blaster",
    },
    "station": {
        "src": "firmware/station",
        "target": "firmware/station/",
        "env_hint": "esp32dev_station",
    },
    "turret_fleet": {
        "src": "firmware/turret_fleet",
        "target": "firmware/turret_fleet/",
        "env_hint": "esp32dev_turret_fleet",
    },
}

RETIRED_FIRMWARE = {
    "hit_target": "src/hit_target",
    "feeder": "src/feeder",
    "nIxo": "src/nIxo",
    "turret": "src/turret",
}

COMMON_LIBS = {
    "bb_esp_core",
    "bb_esp_hw",
    "bb_esp_net",
    "bb_esp_nvs",
    "bb_esp_ota",
}


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_template_scope_pins_kept_and_retired_firmware_lists() -> None:
    migration = read("firmware/MIGRATION_PLAN.md")
    firmware_readme = read("firmware/README.md")
    src_readme = read("src/README.md")
    platformio = read("platformio.ini")

    for forbidden in (
        "esp32dev_go2_go2_",
        "esp32dev_go2_nixo_go2_",
        "esp32dev_go2_nixo_1ch_go2_",
        "esp32dev_go2_nixo_2ch_go2_",
        "custom_robot_id",
    ):
        assert forbidden not in platformio

    assert 'KEPT_FIRMWARE = ["go2", "go2_nixo", "boss_target", "heavy_blaster", "station", "turret_fleet"]' in migration
    assert 'RETIRED_FIRMWARE = ["hit_target", "feeder", "nIxo", "turret"]' in migration

    for name, data in KEPT_FIRMWARE.items():
        assert (ROOT / data["src"]).is_dir(), name
        assert data["target"] in firmware_readme, name
        assert data["target"] in src_readme, name
        assert data["env_hint"] in platformio, name
        assert "Active" in src_readme.split(data["src"].split("/", 1)[1].split("/", 1)[0], 1)[1], name

    for name, src_path in RETIRED_FIRMWARE.items():
        assert (ROOT / src_path).exists(), name
        assert src_path in firmware_readme or f"src/{name}/" in firmware_readme, name
        assert name in migration, name
        assert name in src_readme, name

    assert "hit-target-latest" not in firmware_readme
    assert "hit-target-latest" not in src_readme
    assert "hit_target` is unused" in firmware_readme
    assert "hit_target` is unused" in src_readme


def test_firmware_root_has_migrated_families_so_far() -> None:
    firmware_dirs = {
        path.name
        for path in (ROOT / "firmware").iterdir()
        if path.is_dir() and path.name != "_template"
    }
    assert firmware_dirs == {
        "go2",
        "go2_nixo",
        "go2_nixo_framed_packet_uart",
        "boss_target",
        "heavy_blaster",
        "station",
        "turret_fleet",
    }

    docs = read("firmware/README.md") + read("firmware/MIGRATION_PLAN.md")
    assert "Go2 physical migration is complete" in docs
    assert "Go2-Nixo physical migration is complete" in docs
    assert "Boss Target physical migration is complete" in docs
    assert "Heavy Blaster physical migration is complete" in docs
    assert "Turret Fleet physical migration is complete" in docs
    assert "one firmware at a time" in docs
    assert "go2_nixo" in docs


def test_common_modules_are_capability_split_and_template_forbids_copy_paste_impls() -> None:
    template = read("firmware/_template/README.md")
    firmware_readme = read("firmware/README.md")

    for lib in COMMON_LIBS:
        library_json = ROOT / "lib" / lib / "library.json"
        include_root = ROOT / "lib" / lib / "src" / lib
        assert library_json.exists(), lib
        assert include_root.is_dir(), lib
        assert f"{lib}/" in template, lib
        assert f"{lib}/" in firmware_readme, lib

    assert not (ROOT / "lib/battlebang_esp_common").exists()
    assert not (ROOT / "lib/common").exists()
    assert "Do **not** duplicate" in template
    for forbidden in (
        "wifi_manager.*",
        "ota_manifest.*",
        "http_ota_core.*",
        "reboot_marker.*",
        "topic_utils.*",
        "common_runtime_config_store.*",
        "relay_pin_utils.*",
    ):
        assert forbidden in template

    # MQTT is intentionally narrow until a repeated bus abstraction exists.
    assert "Add `bb_esp_mqtt` only when a repeated PubSubClient JSON/device bus pattern is ready" in read(
        "firmware/MIGRATION_PLAN.md"
    )
    assert not (ROOT / "lib/bb_esp_mqtt").exists()


def test_agent_guidance_files_cover_core_onboarding_boundaries() -> None:
    root_agents = read("AGENTS.md")
    firmware_agents = read("firmware/AGENTS.md")
    lib_agents = read("lib/AGENTS.md")
    workflow_agents = read(".github/workflows/AGENTS.md")
    tests_agents = read("tests/AGENTS.md")
    src_agents = read("src/AGENTS.md")
    firmware_readme = read("firmware/README.md")

    guidance_files = (
        "firmware/AGENTS.md",
        "lib/AGENTS.md",
        ".github/workflows/AGENTS.md",
        "tests/AGENTS.md",
    )
    for guidance_file in guidance_files:
        assert (ROOT / guidance_file).exists(), guidance_file

    # Keep this as a smoke test for non-negotiable onboarding boundaries, not a
    # phrase-by-phrase documentation snapshot. README/AGENTS prose should be
    # editable without rewriting this test unless the boundary changes.
    assert "README" in root_agents and "AGENTS.md" in root_agents
    assert all(guidance_file in firmware_readme for guidance_file in guidance_files)

    for required in (
        "firmware/<firmware_name>/",
        "Do not add new active firmware under `src/`",
        "firmware-ota.yml",
        "stage_id",
        "esp32dev_<firmware>_01",
    ):
        assert required in firmware_agents

    assert "capability-scoped" in lib_agents
    assert "monolithic `common`" in lib_agents
    assert "path filters" in lib_agents

    assert "matrix rows" in workflow_agents
    assert "Shared library changes" in workflow_agents
    assert "versioned release assets" in workflow_agents

    assert "Contract tests" in tests_agents
    assert "Host-only" in tests_agents
    assert "New active firmware must not be added under `src/`" in src_agents


def test_go2_standardization_checkpoint_is_locked_before_path_moves() -> None:
    migration = read("firmware/MIGRATION_PLAN.md")
    go2_readme = read("firmware/go2/README.md")
    go2_nixo_readme = read("firmware/go2_nixo/README.md")

    for path in (
        "firmware/go2/config/runtime_config.h",
        "firmware/go2/config/runtime_config.cpp",
        "firmware/go2_nixo/config/runtime_config.h",
        "firmware/go2_nixo/config/runtime_config.cpp",
        "scripts/go2/provision.py",
        "scripts/go2_nixo/provision.py",
        "scripts/go2_runtime/provisioning.py",
        "firmware/go2/.env.go2.example",
        "firmware/go2_nixo/.env.go2_nixo.example",
    ):
        assert (ROOT / path).exists(), path

    for required in (
        "NVS-backed common runtime config bridge",
        "host-side provisioning/status scripts",
        "real bb_esp_ota OTA",
        "relay pins/polarity/channel count out of runtime config",
        "Nixo fire timing/envelope NVS tuning",
    ):
        assert required in migration

    assert "ota_supported=true" in go2_readme
    assert "ota_supported=true" in go2_nixo_readme
    assert "MQTT `{mqtt_root}/devices/go2/{device_id}/ota`" in go2_readme
    assert "`local_secrets.h`는 표준 경로에서 필요하지 않습니다" in go2_readme
    assert "local_secrets.h` is not read by default" in go2_nixo_readme
    assert "OTA is deferred while the Nixo relay is firing" in go2_nixo_readme
    assert "Nixo fire timing/envelope" in go2_nixo_readme
    assert "relay pins, relay polarity, relay channel count" in go2_nixo_readme


def test_active_ota_release_workflows_publish_firmware_specific_stable_channels() -> None:
    workflow = read(".github/workflows/firmware-ota.yml")
    firmware_readme = read("firmware/README.md")

    assert (ROOT / ".github/workflows/firmware-ota.yml").exists()
    assert not (ROOT / ".github/workflows/boss-target-firmware.yml").exists()
    assert not (ROOT / ".github/workflows/active-firmware-ota.yml").exists()
    assert not (ROOT / ".github/workflows/turret-fleet-firmware.yml").exists()

    for expected in (
        "Firmware OTA Releases",
        "esp32dev_boss_target",
        "esp32dev_go2",
        "esp32dev_go2_nixo_1ch",
        "esp32dev_go2_nixo_2ch",
        "esp32dev_go2_nixo_framed_packet_uart_1ch",
        "esp32dev_go2_nixo_framed_packet_uart_2ch",
        "esp32dev_heavy_blaster",
        "esp32dev_station",
        "esp32dev_turret_fleet",
        "boss-target-manifest.json",
        "go2-manifest.json",
        "go2-nixo-1ch-manifest.json",
        "go2-nixo-2ch-manifest.json",
        "go2-nixo-framed-packet-uart-1ch-manifest.json",
        "go2-nixo-framed-packet-uart-2ch-manifest.json",
        "heavy-blaster-manifest.json",
        "station-manifest.json",
        "manifest.json",
        "boss-target-latest",
        "go2-latest",
        "go2-nixo-1ch-latest",
        "go2-nixo-2ch-latest",
        "go2-nixo-framed-packet-uart-1ch-latest",
        "go2-nixo-framed-packet-uart-2ch-latest",
        "heavy-blaster-latest",
        "station-latest",
        "turret-fleet-latest",
        "BB_BOSS_TARGET_VERSION",
        "BB_GO2_VERSION",
        "BB_GO2_NIXO_VERSION",
        "BB_HEAVY_BLASTER_VERSION",
        "BB_STATION_VERSION",
        "BB_TURRET_FLEET_VERSION",
        "scripts/firmware/make_release_manifest.py",
        "scripts/go2_config.py",
        "scripts/go2_nixo_config.py",
        "lib/bb_esp_ota/**",
        "needs: changes",
        "matrix: ${{ fromJSON(needs.changes.outputs.matrix) }}",
        '"selected=" + ",".join(job["id"] for job in selected)',
    ):
        assert expected in workflow

    for host_only_script_path in (
        '"scripts/go2/**"',
        '"scripts/go2_runtime/**"',
        '"scripts/boss_target/**"',
        '"scripts/go2_nixo/**"',
        '"scripts/heavy_blaster/**"',
        '"scripts/station/**"',
        '"scripts/turret_fleet/**"',
    ):
        assert host_only_script_path not in workflow

    assert 'r"^firmware/boss_target/"' in workflow
    assert 'r"^firmware/go2/"' in workflow
    assert 'r"^firmware/go2_nixo/"' in workflow
    assert 'r"^firmware/go2_nixo_framed_packet_uart/"' in workflow
    assert 'r"^firmware/heavy_blaster/"' in workflow
    assert 'r"^firmware/station/"' in workflow
    assert 'r"^firmware/turret_fleet/"' in workflow
    assert 'r"^lib/bb_esp_hw/"' in workflow
    assert 'r"^lib/bb_esp_net/"' in workflow
    assert '"paths": [r"^firmware/go2/", r"^scripts/go2_config\\.py$", r"^lib/bb_esp_core/", r"^lib/bb_esp_nvs/", r"^lib/bb_esp_ota/"]' in workflow
    assert '"paths": [r"^firmware/go2_nixo/", r"^scripts/go2_nixo_config\\.py$", r"^lib/bb_esp_core/", r"^lib/bb_esp_hw/", r"^lib/bb_esp_nvs/", r"^lib/bb_esp_ota/"]' in workflow
    assert '"paths": [r"^firmware/heavy_blaster/", r"^lib/bb_esp_core/", r"^lib/bb_esp_net/", r"^lib/bb_esp_nvs/", r"^lib/bb_esp_ota/"]' in workflow
    assert '"paths": [r"^firmware/station/", r"^lib/bb_esp_core/", r"^lib/bb_esp_net/", r"^lib/bb_esp_nvs/", r"^lib/bb_esp_ota/"]' in workflow

    assert "lib/bb_esp_core/**" in workflow
    assert "lib/bb_esp_nvs/**" in workflow
    assert "lib/bb_esp_ota/**" in workflow
    assert "scripts/firmware/make_release_manifest.py" in workflow
    assert 'VERSION="0.2.${GITHUB_RUN_NUMBER}-main"' in workflow
    assert 'BUILD="$((1000 + GITHUB_RUN_NUMBER))"' in workflow
    assert "update_stable_latest" in workflow
    assert 'UPDATE_STABLE_LATEST="true"' in workflow
    assert 'UPDATE_STABLE_LATEST="${{ inputs.update_stable_latest }}"' in workflow
    assert 'if [[ "${UPDATE_STABLE_LATEST}" == "true" ]]; then' in workflow
    assert 'update_stable_latest=false keeps branch smoke builds' in workflow
    assert 'tag_sha()' in workflow
    assert 'if existing_sha="$(gh api "repos/${PUBLIC_REPO}/git/ref/tags/${tag}"' in workflow
    assert 'printf '"'"'%s\\n'"'"' "${existing_sha}"' in workflow
    assert 'ensure_version_tag_available()' in workflow
    assert 'move_tag_to_target()' in workflow
    assert 'gh api -X PATCH "repos/${PUBLIC_REPO}/git/refs/tags/${tag}"' in workflow
    assert 'ensure_version_tag_available "${tag}" "${RELEASE_TARGET_SHA}"' in workflow
    assert 'move_tag_to_target "${tag}" "${RELEASE_TARGET_SHA}"' in workflow
    assert '--target "${RELEASE_TARGET_SHA}"' in workflow
    assert 'upload_release "${VERSION_TAG}"' in workflow
    assert 'upload_release "${STABLE_TAG}"' in workflow
    version_upload = workflow.split('upload_release "${VERSION_TAG}"', 1)[1].split(
        'upload_release "${STABLE_TAG}"', 1
    )[0]
    stable_upload = workflow.split('upload_release "${STABLE_TAG}"', 1)[1]
    assert '"false"' in version_upload
    assert '"true"' in stable_upload

    assert "The unified `Firmware OTA Releases` workflow covers all active firmware families" in firmware_readme
    assert "builds only the affected firmware matrix entries" in firmware_readme
    assert "repo-wide `latest`" in firmware_readme


def test_active_docs_no_longer_point_to_old_src_go2_path() -> None:
    active_docs = [
        "README.md",
        "src/README.md",
        "firmware/README.md",
        "firmware/go2_nixo/README.md",
        "firmware/go2_nixo/docs/README.md",
        "firmware/go2_nixo/docs/build-upload-workflow.md",
        "firmware/go2_nixo/docs/nixo-fire.md",
    ]

    for path in active_docs:
        text = read(path)
        assert "src/go2/" not in text, path
        assert "`src/go2`" not in text, path
        assert "src/go2_nixo/" not in text, path
        assert "`src/go2_nixo`" not in text, path

    assert "firmware/go2/" in read("README.md")
    assert "firmware/go2/" in read("firmware/go2_nixo/README.md")
    assert "firmware/go2_nixo/" in read("README.md")
    assert "firmware/go2_nixo/" in read("src/README.md")


def test_moved_firmware_entrypoints_do_not_point_to_old_src_paths() -> None:
    checked_paths = [
        "AGENTS.md",
        "bin/turret",
        "README.md",
        "src/README.md",
        "firmware/README.md",
        "firmware/MIGRATION_PLAN.md",
        "platformio.ini",
        ".github/workflows/firmware-ota.yml",
        "scripts/heavy_blaster/mqtt_command.py",
        "scripts/heavy_blaster/provision.py",
        "scripts/turret_fleet/provision.py",
        "scripts/turret_fleet/mqtt_command.py",
    ]
    forbidden = (
        "src/go2/",
        "src/go2_nixo/",
        "src/boss_target",
        "src/heavy-blaster",
        "scripts/heavy-blaster",
        "src/turret_fleet",
        "+<go2/",
        "+<go2_nixo/",
        "+<boss_target/",
        "+<heavy-blaster/",
        "+<turret_fleet",
        "-<go2/",
        "-<go2_nixo/",
        "-<boss_target/",
        "-<heavy-blaster/",
        "-<turret_fleet",
    )

    for path in checked_paths:
        text = read(path)
        for token in forbidden:
            assert token not in text, f"{path} still references {token}"

    assert "firmware/turret_fleet/.env.turret_fleet" in read("AGENTS.md")
    assert "firmware/turret_fleet/profiles/<turret>.json" in read("bin/turret")
    assert "firmware/heavy_blaster/.env.heavy-blaster" in read("scripts/heavy_blaster/mqtt_command.py")
