"""CI-only USB release contract; never connects to a device."""

from __future__ import annotations

import hashlib
import json
import os
import re
import struct
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts/firmware/prepare_usb_release.py"


def approved_table() -> bytes:
    rows = [
        (1, 2, 0x9000, 0x5000, b"nvs"),
        (1, 0, 0xE000, 0x2000, b"otadata"),
        (0, 16, 0x10000, 0x300000, b"app0"),
        (1, 130, 0x310000, 0xE0000, b"spiffs"),
        (1, 3, 0x3F0000, 0x10000, b"coredump"),
    ]
    entries = b"".join(struct.pack("<2sBBLL16sL", b"\xaa\x50", *row, 0) for row in rows)
    return (entries + b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(entries).digest()).ljust(
        3072, b"\xff"
    )


def prepare(tmp_path: Path, table: bytes, firmware: bytes = b"\xe9" + b"\0" * 31):
    partitions = tmp_path / "partitions.bin"
    partitions.write_bytes(table)
    app = tmp_path / "battlebang-go2-full-usb-2ch-0.3.99-test.bin"
    app.write_bytes(firmware)
    result = subprocess.run(
        [
            sys.executable,
            str(SCRIPT),
            "--firmware",
            str(app),
            "--partitions",
            str(partitions),
        ],
        text=True,
        capture_output=True,
    )
    return result, app.with_suffix(".partitions.bin")


def test_approved_table_becomes_one_ff_padded_sector(tmp_path):
    result, output = prepare(tmp_path, approved_table())
    assert result.returncode == 0, result.stderr
    assert output.read_bytes() == approved_table().ljust(4096, b"\xff")


@pytest.mark.parametrize("length", [0, 1, 0x300001])
def test_invalid_app_size_writes_no_layout_asset(tmp_path, length):
    result, output = prepare(tmp_path, approved_table(), b"\xe9" * length)
    assert result.returncode != 0
    assert not output.exists()


@pytest.mark.parametrize("offset", [0, 4, 32, 64, 80, 160, 1024])
def test_any_layout_or_checksum_change_is_rejected(tmp_path, offset):
    bad = bytearray(approved_table())
    bad[offset] ^= 1
    result, output = prepare(tmp_path, bad)
    assert result.returncode != 0
    assert not output.exists()


def test_padded_input_accepted_but_oversized_table_rejected(tmp_path):
    result, output = prepare(tmp_path, approved_table().ljust(4096, b"\xff"))
    assert result.returncode == 0, result.stderr
    output.unlink()
    result, output = prepare(tmp_path, approved_table().ljust(4097, b"\xff"))
    assert result.returncode != 0
    assert not output.exists()


def test_wrong_image_magic_rejected(tmp_path):
    result, output = prepare(tmp_path, approved_table(), b"not an ESP32 image" * 2)
    assert result.returncode != 0
    assert not output.exists()


def test_release_workflow_exports_layout_and_hash_without_changing_other_families():
    workflow = (ROOT / ".github/workflows/firmware-ota.yml").read_text()
    assert 'if [[ "${{ matrix.id }}" == "go2-full-usb-2ch" ]]' in workflow
    assert "python scripts/firmware/prepare_usb_release.py" in workflow
    assert (
        'shasum -a 256 "dist/${FIRMWARE_NAME%.bin}.partitions.bin" >> dist/sha256.txt'
        in workflow
    )
    assert 'r"^scripts/firmware/prepare_usb_release\\.py$"' in workflow


@pytest.mark.parametrize(
    "product,ref,expected",
    [
        ("go2-full-usb-2ch", "refs/heads/main", True),
        ("go2-full-usb-2ch", "refs/heads/smoke", False),
        ("go2-full-uart-2ch", "refs/heads/main", False),
    ],
)
def test_usb_upload_pointer_is_main_only_and_never_publishes_ota_manifest(
    tmp_path, product, ref, expected
):
    workflow = (ROOT / ".github/workflows/firmware-ota.yml").read_text()
    block = workflow.split("          # Upload discovery only:", 1)[1].split(
        '          if [[ "${UPDATE_STABLE_LATEST}"', 1
    )[0]
    block = "\n".join(block.splitlines()[1:])
    block = block.replace("${{ matrix.id }}", product).replace(
        "${{ steps.version.outputs.version }}", "0.3.99-main"
    )
    script = (
        """set -eu
mkdir dist
PUBLIC_REPO=test/repo
RELEASE_TARGET_SHA=test
move_tag_to_target() { printf 'move %s\\n' "$*"; }
gh() { printf 'gh %s\\n' "$*"; }
"""
        + block
    )
    result = subprocess.run(
        ["/bin/bash", "-c", script],
        cwd=tmp_path,
        env=os.environ | {"GITHUB_REF": ref},
        text=True,
        capture_output=True,
    )
    assert result.returncode == 0, result.stderr
    assert ("release upload" in result.stdout) is expected
    if expected:
        assert (tmp_path / "dist/usb-version.txt").read_text() == "0.3.99-main\n"
        assert "manifest" not in result.stdout
        assert "go2-full-usb-2ch-upload-latest" in result.stdout


@pytest.mark.parametrize(
    "changed",
    [
        "bin/upload_esp.sh",
        "tests/python/test_usb_release_artifacts.py",
        "tests/python/test_usb_release_upload.py",
        "scripts/firmware/prepare_usb_release.py",
    ],
)
def test_host_changes_run_unit_tests_without_publishing_firmware(tmp_path, changed):
    workflow = (ROOT / ".github/workflows/firmware-ota.yml").read_text()
    push_paths = workflow.split("    paths:", 1)[1].split("  workflow_dispatch:", 1)[0]
    assert f'"{changed}"' in push_paths
    assert "  usb-upload-tests:" in workflow
    assert "needs: [changes, usb-upload-tests]" in workflow
    assert "if: needs.changes.outputs.has_changes == 'true'" in workflow
    # Execute the actual selector with only git's changed-file result replaced.
    code = workflow.split("          python - <<'PY' >> \"$GITHUB_OUTPUT\"", 1)[
        1
    ].split("\n          PY", 1)[0]
    code = "\n".join(line[10:] for line in code.splitlines())
    code = code.replace("files = changed_files()", f"files = [{changed!r}]")
    env = os.environ | {"GITHUB_EVENT_NAME": "push", "GITHUB_SHA": "0" * 40}
    result = subprocess.run(
        [sys.executable, "-c", code], env=env, text=True, capture_output=True
    )
    assert result.returncode == 0, result.stderr
    matrix = json.loads(
        re.search(r"^matrix=(.*)$", result.stdout, re.MULTILINE).group(1)
    )
    selected = [job["id"] for job in matrix["include"]]
    if changed == "scripts/firmware/prepare_usb_release.py":
        assert selected == ["go2-full-usb-2ch"]
        assert "has_changes=true" in result.stdout
    else:
        assert selected == []
        assert "has_changes=false" in result.stdout


def test_versioned_release_cannot_clobber_existing_assets(tmp_path):
    """Exercise the actual inline workflow function with a harmless fake gh."""
    workflow = (ROOT / ".github/workflows/firmware-ota.yml").read_text()
    function = workflow.split("          upload_release() {", 1)[1].split(
        '\n          upload_release "${VERSION_TAG}"', 1
    )[0]
    function = "upload_release() {" + "\n".join(
        line[10:] for line in function.splitlines()
    )
    calls = tmp_path / "calls"
    script = f"""
set -eu
PUBLIC_REPO=test/repo
RELEASE_TARGET_SHA=test
ensure_version_tag_available() {{ :; }}
move_tag_to_target() {{ :; }}
gh() {{ printf '%s\\n' "$*" >> '{calls}'; return 0; }}
{function}
upload_release test-v1 title notes false
"""
    result = subprocess.run(["bash", "-c", script], text=True, capture_output=True)
    assert result.returncode != 0
    assert "immutable" in result.stderr
    assert "release upload" not in calls.read_text()
