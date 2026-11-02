"""TDD contract for the PR #59 Go2 Full consolidation.

The baseline tests preserve source/configuration evidence before files move.  The
target tests intentionally fail until ``firmware/go2_full`` replaces the two
integrated Go2/Nixo source trees.  No hardware is accessed by this module.
"""

from __future__ import annotations

import ast
import hashlib
import importlib.util
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BASELINE = json.loads(
    (ROOT / "tests/fixtures/go2_full/baseline_contract.json").read_text(
        encoding="utf-8"
    )
)


def _sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _rename_blaster_identifiers_outside_cpp_literals(source: str) -> str:
    """Rename Nixo C++ identifiers/comments while preserving wire string literals."""

    parts: list[str] = []
    index = 0
    plain_start = 0
    while index < len(source):
        if source[index] not in {'"', "'"}:
            index += 1
            continue
        plain = source[plain_start:index]
        parts.append(source[plain_start:index].replace("Nixo", "Blaster").replace("nixo", "blaster").replace("NIXO", "BLASTER"))
        quote = source[index]
        literal_start = index
        index += 1
        while index < len(source):
            if source[index] == "\\":
                index += 2
                continue
            if source[index] == quote:
                index += 1
                break
            index += 1
        parts.append(source[literal_start:index])
        plain_start = index
    parts.append(source[plain_start:].replace("Nixo", "Blaster").replace("nixo", "blaster").replace("NIXO", "BLASTER"))
    return "".join(parts)


def _migrate_pr59_source_to_go2_full(source: str) -> str:
    """Apply the only source transformations allowed by this refactor."""

    def migrate_include(match: re.Match[str]) -> str:
        include = match.group(1)
        include = include.replace("go2_nixo_framed_packet_uart/", "go2_full/")
        include = include.replace("go2_nixo/", "go2_full/")
        include = include.replace("nixo/nixo_fire_client", "blaster/blaster_fire_client")
        include = include.replace("go2_full/display/", "go2_full/led/")
        return f'#include "{include}"'

    source = re.sub(r'#include "([^"]+)"', migrate_include, source)
    source = source.replace("BB_GO2_NIXO_", "BB_GO2_FULL_")
    source = source.replace("namespace go2_nixo", "namespace go2_full")
    source = source.replace("}  // namespace go2_nixo", "}  // namespace go2_full")
    source = source.replace(
        'FIRMWARE_NAME = "go2_nixo"', 'FIRMWARE_NAME = "go2_full"'
    )
    source = _rename_blaster_identifiers_outside_cpp_literals(source)
    # The later two-channel-only decision changes only the former 1ch fallback
    # defaults. The verified relay_2ch profile itself remains byte-for-byte locked.
    source = source.replace(
        "#define BATTLEBANG_NIXO_RELAY1_PIN 23",
        "#define BATTLEBANG_NIXO_RELAY1_PIN 22",
    )
    source = source.replace(
        "#define BATTLEBANG_NIXO_RELAY2_PIN -1",
        "#define BATTLEBANG_NIXO_RELAY2_PIN 23",
    )
    source = source.replace(
        "#define BATTLEBANG_NIXO_RELAY_DELAY1_MS 800",
        "#define BATTLEBANG_NIXO_RELAY_DELAY1_MS 150",
    )
    if source.lstrip().startswith("{"):
        source = source.replace('"nixo_relay1_pin": 23', '"nixo_relay1_pin": 22')
        source = source.replace('"nixo_relay2_pin": -1', '"nixo_relay2_pin": 23')
        source = source.replace('"nixo_relay_delay1_ms": 800', '"nixo_relay_delay1_ms": 150')
    return source.replace(
        "Generic integrated Go2+Nixo hardware profile. Device identity (go2_01/go2_02/...) plus hit/Nixo timing tuning are provisioned into NVS at runtime; relay pins/polarity/channel count are built in.",
        "Go2 Full fixed two-channel hardware profile. Device identity (go2_01/go2_02/...) plus hit/Nixo timing tuning are provisioned into NVS at runtime; the fixed two-channel relay pins/polarity are built in.",
    )


def _tree_sha256(relative_root: str) -> str:
    root = ROOT / relative_root
    digest = hashlib.sha256()
    for path in sorted(root.rglob("*")):
        if path.is_file():
            digest.update(path.relative_to(root).as_posix().encode("utf-8"))
            digest.update(b"\0")
            digest.update(path.read_bytes())
            digest.update(b"\0")
    return digest.hexdigest()


def _platformio_env_blocks() -> dict[str, str]:
    source = (ROOT / "platformio.ini").read_text(encoding="utf-8")
    matches = list(re.finditer(r"^\[env:([^]]+)\]\n", source, re.MULTILINE))
    blocks: dict[str, str] = {}
    for index, match in enumerate(matches):
        end = matches[index + 1].start() if index + 1 < len(matches) else len(source)
        blocks[match.group(1)] = source[match.start() : end].rstrip()
    return blocks


def _effective_platformio_options(name: str) -> dict[str, str]:
    blocks = _platformio_env_blocks()
    options: dict[str, str] = {}
    block = blocks[name]
    parent = re.search(r"^extends\s*=\s*env:([^\s]+)$", block, re.MULTILINE)
    if parent:
        options.update(_effective_platformio_options(parent.group(1)))
    for key, value in re.findall(r"^([A-Za-z0-9_]+)\s*=\s*([^\n]+)$", block, re.MULTILINE):
        if key != "extends":
            options[key] = value.strip()
    return options


def _workflow_jobs() -> dict[str, dict]:
    workflow = (ROOT / ".github/workflows/firmware-ota.yml").read_text(
        encoding="utf-8"
    )
    literal = workflow.split("firmware_jobs = [", 1)[1].split(
        "\n\n          always_all_paths", 1
    )[0]
    jobs = ast.literal_eval("[" + literal)
    return {job["id"]: job for job in jobs}


def _stable_json_sha256(value: object) -> str:
    payload = json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
    return _sha256_bytes(payload)


def test_baseline_behavior_sources_match_pr59_snapshot() -> None:
    for entry in BASELINE["behavior_file_sha256"]:
        old_path = ROOT / entry["old_path"]
        target_path = ROOT / entry["target_path"]
        path = target_path if target_path.is_file() else old_path
        assert path.is_file(), (
            "behavior source is absent at old and target paths: "
            f"{entry['old_path']} -> {entry['target_path']}"
        )
        source = path.read_text(encoding="utf-8")
        if path == old_path:
            assert _sha256_bytes(source.encode()) == entry["baseline_sha256"], path
            source = _migrate_pr59_source_to_go2_full(source)
        assert _sha256_bytes(source.encode()) == entry["expected_target_sha256"], path


def test_intentional_refactors_keep_baseline_provenance_and_executable_evidence() -> None:
    for entry in BASELINE["intentional_refactors"]:
        target_path = ROOT / entry["target_path"]
        assert target_path.is_file(), target_path
        assert entry["reason"].strip(), entry["old_path"]
        for evidence in entry["evidence_tests"]:
            assert (ROOT / evidence).is_file(), evidence
        assert re.fullmatch(r"[0-9a-f]{64}", entry["baseline_sha256"]), entry["old_path"]
    assert re.fullmatch(r"[0-9a-f]{40}", BASELINE["baseline_commit"])


def test_baseline_protected_firmware_and_libraries_match_pr59_snapshot() -> None:
    for path, expected in BASELINE["protected_tree_sha256"].items():
        assert _tree_sha256(path) == expected, path


def test_baseline_non_integrated_platformio_environments_are_unchanged() -> None:
    blocks = _platformio_env_blocks()
    for name, expected in BASELINE["protected_platformio_env_sha256"].items():
        assert name in blocks, name
        assert _sha256_bytes(blocks[name].encode()) == expected, name


def test_baseline_non_integrated_ota_jobs_are_unchanged() -> None:
    jobs = _workflow_jobs()
    for name, expected in BASELINE["protected_workflow_job_sha256"].items():
        assert name in jobs, name
        assert _stable_json_sha256(jobs[name]) == expected, name


def test_target_has_one_go2_full_tree_and_no_old_integrated_trees() -> None:
    assert (ROOT / "firmware/go2_full/main.cpp").is_file()
    assert not (ROOT / "firmware/go2_nixo").exists()
    assert not (ROOT / "firmware/go2_nixo_framed_packet_uart").exists()
    assert not (ROOT / "firmware/legacy").exists()
    assert (ROOT / "firmware/go2/main.cpp").is_file()
    assert not (ROOT / "firmware/go2_full/variants/relay_1ch").exists()
    assert (ROOT / "firmware/go2_full/variants/relay_2ch/config.json").is_file()


def test_target_main_keeps_packet_runtime_and_removes_jetson_text_protocol() -> None:
    main = (ROOT / "firmware/go2_full/main.cpp").read_text(encoding="utf-8")
    assert "pollJetsonUart" in main
    assert "IncrementalParser jetsonPacketParser" in main
    assert "pollCommandStream(JetsonSerial" not in main
    assert "jetsonCommandLine" not in main
    assert 'doc["jetson_uart_protocol"] = "framed"' in main


def test_target_main_preserves_pr59_physical_piezo_gate() -> None:
    main = (ROOT / "firmware/go2_full/main.cpp").read_text(encoding="utf-8")
    assert "constexpr int PIEZO_ENABLE_BUTTON_PIN = 27;" in main
    button = (
        ROOT / "firmware/go2_full/pull_down_button/pull_down_button.cpp"
    ).read_text(encoding="utf-8")
    assert "pinMode(pin_, INPUT_PULLDOWN)" in button
    assert "PIEZO_DO_PIN != PIEZO_ENABLE_BUTTON_PIN" in main
    assert "piezoEnableButton.begin(PIEZO_ENABLE_BUTTON_PIN, millis())" in main
    assert 'doc["piezo_adc_enabled"]' in main
    poll = main.split("static void pollAnalogPiezo(uint32_t now) {", 1)[1].split(
        "\n}", 1
    )[0]
    assert poll.index("piezoSensor.poll") < poll.index("publishAdcHitEvent")
    assert "if (!piezoEnableButton.enabled()) return;" in main


def test_target_preserves_existing_nvs_and_mqtt_wire_names() -> None:
    runtime = (ROOT / "firmware/go2_full/config/runtime_config.cpp").read_text(
        encoding="utf-8"
    )
    mqtt = (ROOT / "firmware/go2_full/mqtt/hit_mqtt_client.cpp").read_text(
        encoding="utf-8"
    )
    assert 'kConfigNamespace = "go2_full"' in runtime
    assert 'kHardwareNamespace = "go2_full_hw"' in runtime
    assert 'kPreviousConfigNamespace = "go2_nixo"' in runtime
    assert 'kPreviousHardwareNamespace = "go2_nixo_hw"' in runtime
    assert 'makeDeviceTopics(config.common.mqttRoot, "go2_nixo"' in mqtt


def test_target_firmware_identity_uses_go2_full_names() -> None:
    info = (ROOT / "firmware/go2_full/app/firmware_info.h").read_text(
        encoding="utf-8"
    )
    config = (ROOT / "firmware/go2_full/build_config.h").read_text(encoding="utf-8")
    assert "BB_GO2_FULL_APP_NAME" in info
    assert "BB_GO2_NIXO_APP_NAME" not in info
    assert 'FIRMWARE_NAME = "go2_full"' in config
    assert 'battlebang-go2-full-' in info


def test_target_platformio_has_only_two_channel_packet_environments() -> None:
    blocks = _platformio_env_blocks()
    for name in (
        "esp32dev_go2_full_uart_1ch",
        "esp32dev_go2_full_usb_1ch",
    ):
        assert name not in blocks
    for name in ("esp32dev_go2_full", "esp32dev_go2_full_uart_2ch", "esp32dev_go2_full_usb_2ch"):
        assert name in blocks
        assert "firmware/go2_full" in blocks[name] or "extends = env:esp32dev_go2_full" in blocks[name]
    assert "esp32dev_go2_nixo" not in blocks
    assert "single_char_newline" not in (ROOT / "platformio.ini").read_text(encoding="utf-8")


def test_target_existing_packet_envs_are_aliases_for_go2_full_only() -> None:
    blocks = _platformio_env_blocks()
    aliases = {
        "esp32dev_go2_nixo_framed_packet_uart_2ch": "esp32dev_go2_full_uart_2ch",
        "esp32dev_go2_nixo_framed_packet_usb_2ch": "esp32dev_go2_full_usb_2ch",
    }
    for alias, canonical in aliases.items():
        assert blocks[alias].strip() == f"[env:{alias}]\nextends = env:{canonical}"
    assert "esp32dev_go2_nixo_framed_packet_uart_1ch" not in blocks
    assert "esp32dev_go2_nixo_framed_packet_usb_1ch" not in blocks


def test_target_ota_matrix_has_only_go2_full_integrated_jobs() -> None:
    jobs = _workflow_jobs()
    expected = {
        "go2-full-uart-2ch",
        "go2-full-usb-2ch",
    }
    assert expected <= jobs.keys()
    assert not any(name.startswith("go2-nixo") for name in jobs)
    for name in expected:
        job = jobs[name]
        assert job["app"].startswith("battlebang-go2-full-")
        assert job["hardware"].startswith("esp32dev-go2-full-button-relay-")
        assert job["channel"] == name
        assert job["version_header"] == "firmware/go2_full/app/version_autogen.h"


def test_target_has_canonical_go2_full_provisioning_entrypoint() -> None:
    assert (ROOT / "scripts/go2_full_config.py").is_file()
    provision = (ROOT / "scripts/go2_full/provision.py").read_text(encoding="utf-8")
    assert "single_char_newline" not in provision
    assert 'SUPPORTED_JETSON_TRANSPORTS = ("uart2", "usb_serial")' in provision
    assert 'SUPPORTED_RELAY_VARIANTS = ("relay_2ch",)' in provision
    assert 'default="relay_2ch"' in provision
    assert 'default="usb_serial"' in provision


def test_target_build_metadata_matches_each_two_channel_release_row() -> None:
    jobs = _workflow_jobs()
    expected = {
        "esp32dev_go2_full_uart_2ch": ("uart2", "go2-full-uart-2ch"),
        "esp32dev_go2_full_usb_2ch": ("usb_serial", "go2-full-usb-2ch"),
    }
    for env_name, (transport, channel) in expected.items():
        options = _effective_platformio_options(env_name)
        assert options["custom_go2_full_relay_variant"] == "relay_2ch"
        assert options["custom_go2_full_jetson_transport"] == transport
        job = jobs[channel]
        assert job["env"] == env_name
        assert job["channel"] == channel
        assert job["hardware"] == "esp32dev-go2-full-button-relay-2ch-v1"
        assert job["app"] == f"battlebang-go2-full-{'usb' if transport == 'usb_serial' else 'uart'}"

    base = _effective_platformio_options("esp32dev_go2_full")
    assert base["custom_go2_full_relay_variant"] == "relay_2ch"
    assert base["custom_go2_full_jetson_transport"] == "usb_serial"


def test_target_build_script_rejects_non_two_channel_profiles_and_pin_overrides() -> None:
    script = (ROOT / "scripts/go2_full_config.py").read_text(encoding="utf-8")
    assert 'return option_value or "relay_2ch"' in script
    assert script.count('name != "relay_2ch"') >= 2
    assert "relay1_pin < 0 or relay2_pin < 0" in script
    assert "relay1_pin == relay2_pin" in script
    assert '"GO2_FULL_RELAY_VARIANT", "GO2_NIXO_RELAY_VARIANT"' in script


def test_target_provision_defaults_and_legacy_env_fallback_preserve_two_channel_packet_mode() -> None:
    path = ROOT / "scripts/go2_full/provision.py"
    spec = importlib.util.spec_from_file_location("go2_full_provision_contract", path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    assert module.detect_firmware({}, None) == "go2_full"
    assert module.detect_jetson_transport({}, None) == "usb_serial"
    assert module.detect_relay_variant({}, None) == "relay_2ch"
    assert module.detect_jetson_transport(
        {"GO2_FULL_JETSON_TRANSPORT": "usb_serial", "GO2_NIXO_JETSON_TRANSPORT": "uart2"},
        None,
    ) == "usb_serial"
    assert module.detect_jetson_transport({"GO2_NIXO_JETSON_TRANSPORT": "uart2"}, None) == "uart2"

    try:
        module.detect_relay_variant({"GO2_FULL_RELAY_VARIANT": "relay_1ch"}, None)
    except module.ProvisioningError:
        pass
    else:
        raise AssertionError("relay_1ch must be rejected rather than remapped")



def test_flywheel_gpio23_precedes_chain_gpio22() -> None:
    for profile in ('hardware_profile.json', 'variants/relay_2ch/config.json'):
        data = json.loads((ROOT / 'firmware/go2_full' / profile).read_text())
        pins = data.get('defaults', data)
        assert (pins['blaster_relay1_pin'], pins['blaster_relay2_pin']) == (23, 22)
    header = (ROOT / 'firmware/go2_full/build_config.h').read_text()
    assert '#define BATTLEBANG_BLASTER_RELAY1_PIN 23' in header
    assert '#define BATTLEBANG_BLASTER_RELAY2_PIN 22' in header
    source = (ROOT / 'firmware/go2_full/blaster/blaster_fire_client.cpp').read_text()
    start = source.index('void BlasterFireClient::updateFireSequence')
    sequence = source[start:]
    assert sequence.index('digitalWrite(BLASTER_RELAY1_PIN_VALUE, BLASTER_RELAY_ON_LEVEL_VALUE)') < sequence.index('digitalWrite(BLASTER_RELAY2_PIN_VALUE, BLASTER_RELAY_ON_LEVEL_VALUE)')
    assert 'now - fireTimerMs_ >= relayDelay1Ms_' in sequence
