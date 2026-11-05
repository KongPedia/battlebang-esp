import hashlib
import os
import re
import shutil
import subprocess
from pathlib import Path

import pytest


REPO = Path(__file__).resolve().parents[2]
SCRIPT = REPO / "bin" / "upload_esp.sh"
VERSION = "0.3.38-main"
FIRMWARE_NAME = f"battlebang-go2-full-usb-2ch-{VERSION}.bin"
PARTITIONS_NAME = f"battlebang-go2-full-usb-2ch-{VERSION}.partitions.bin"


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


@pytest.fixture
def release(tmp_path: Path) -> Path:
    release_dir = tmp_path / "release"
    release_dir.mkdir()
    firmware = b"\xe9" + (b"firmware-image" * 128)
    partitions = bytes(range(256)) * 16
    (release_dir / FIRMWARE_NAME).write_bytes(firmware)
    (release_dir / PARTITIONS_NAME).write_bytes(partitions)
    (release_dir / "sha256.txt").write_text(
        f"{_sha256(firmware)}  dist/{FIRMWARE_NAME}\n"
        f"{_sha256(partitions)}  dist/{PARTITIONS_NAME}\n"
    )
    (release_dir / "usb-version.txt").write_text(VERSION + "\n")
    return release_dir


@pytest.fixture
def runtime(tmp_path: Path, release: Path) -> tuple[dict[str, str], Path]:
    tools = tmp_path / "tools"
    tools.mkdir()
    for command in (
        "bash",
        "date",
        "mktemp",
        "rm",
        "wc",
        "od",
        "uname",
        "readlink",
        "tar",
        "chmod",
        "mkdir",
        "mv",
    ):
        source = shutil.which(command)
        assert source
        (tools / command).symlink_to(source)

    (tools / "curl").write_text(
        """#!/bin/bash
set -euo pipefail
output=
url=
while (($#)); do
  case "$1" in
    --output) output=$2; shift 2 ;;
    http*) url=$1; shift ;;
    *) shift ;;
  esac
done
[[ ${FAKE_CURL_FAIL:-} != 1 ]] || exit 22
name=${url##*/}
/bin/cp "$FAKE_RELEASE_DIR/$name" "$output"
"""
    )
    (tools / "sha256sum").write_text(
        """#!/bin/bash
set -euo pipefail
/usr/bin/shasum -a 256 "$1"
"""
    )
    (tools / "timeout").write_text(
        """#!/bin/bash
set -euo pipefail
printf '%s\n' "$*" >> "$FAKE_TIMEOUT_CALLS"
[[ $1 == --kill-after=5s ]]
shift
duration=$1
shift
[[ ${FAKE_TIMEOUT:-} != 1 ]] || exit 124
exec "$@"
"""
    )
    (tools / "udevadm").write_text(
        """#!/bin/bash
if [[ $* == *--export-db* ]]; then
  printf 'P: /fake/one\nE: DEVNAME=/dev/null\nE: ID_VENDOR_ID=10c4\nE: ID_MODEL_ID=ea60\nE: ID_USB_DRIVER=cp210x\n\n'
  if [[ ${FAKE_AMBIGUOUS:-} == 1 ]]; then
    printf 'P: /fake/two\nE: DEVNAME=/dev/zero\nE: ID_VENDOR_ID=1a86\nE: ID_MODEL_ID=7523\nE: ID_USB_DRIVER=ch341\n\n'
  fi
else
  printf 'ID_VENDOR_ID=%s\nID_MODEL_ID=ea60\nID_USB_DRIVER=cp210x\n' "${FAKE_VID:-10c4}"
fi
"""
    )
    for path in tools.iterdir():
        if not path.is_symlink():
            path.chmod(0o755)

    calls = tmp_path / "esptool.calls"
    timeout_calls = tmp_path / "timeout.calls"
    esptool = tmp_path / "fake-esptool"
    esptool.write_text(
        """#!/bin/bash
set -euo pipefail
if [[ ${1:-} == -h ]]; then
  printf 'esptool v5.6.0 - serial utility\n'
  exit "${FAKE_HELP_FAIL:-0}"
fi
printf '%s\n' "$*" >> "$FAKE_ESPTOOL_CALLS"
args=" $* "
if [[ $args == *" read-flash "* ]]; then
  [[ ${FAKE_READ_FAIL:-} != 1 ]] || exit 8
  out=${!#}
  /bin/cp "$FAKE_DEVICE_PARTITIONS" "$out"
  exit 0
fi
[[ ${FAKE_WRITE_FAIL:-} != 1 ]] || exit 9
exit 0
"""
    )
    esptool.chmod(0o755)

    device_partitions = tmp_path / "device-partitions.bin"
    shutil.copy2(release / PARTITIONS_NAME, device_partitions)
    env = {
        "PATH": str(tools),
        "FAKE_RELEASE_DIR": str(release),
        "FAKE_ESPTOOL_CALLS": str(calls),
        "FAKE_TIMEOUT_CALLS": str(timeout_calls),
        "FAKE_DEVICE_PARTITIONS": str(device_partitions),
    }
    return env, esptool


def run_upload(
    runtime: tuple[dict[str, str], Path],
    *extra: str,
    env_update: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    env, esptool = runtime
    merged_env = os.environ.copy()
    merged_env.update(env)
    if env_update:
        merged_env.update(env_update)
    return subprocess.run(
        [
            "/bin/bash",
            str(SCRIPT),
            "--version",
            VERSION,
            "--port",
            "/dev/null",
            "--esptool",
            str(esptool),
            *extra,
        ],
        text=True,
        capture_output=True,
        env=merged_env,
        timeout=10,
    )


def calls(runtime: tuple[dict[str, str], Path]) -> list[str]:
    path = Path(runtime[0]["FAKE_ESPTOOL_CALLS"])
    return path.read_text().splitlines() if path.exists() else []


def timeout_calls(runtime: tuple[dict[str, str], Path]) -> list[str]:
    path = Path(runtime[0]["FAKE_TIMEOUT_CALLS"])
    return path.read_text().splitlines() if path.exists() else []


def test_happy_path_reads_layout_then_writes_only_application(runtime):
    result = run_upload(runtime, env_update={"ESPTOOL_BAUD": "921600"})

    assert result.returncode == 0, result.stderr
    assert "UPLOAD_OK" in result.stdout
    assert "upload only" in result.stdout.lower()
    assert len(calls(runtime)) == 2
    assert calls(runtime)[0].startswith(
        "--chip esp32 --port /dev/null --baud 115200 read-flash 0x8000 0x1000 "
    )
    assert calls(runtime)[1].startswith(
        "--chip esp32 --port /dev/null --baud 115200 write-flash --compress 0x10000 "
    )
    joined = "\n".join(calls(runtime))
    assert "--verify" not in joined
    assert "--force" not in joined
    assert "erase" not in joined
    assert "read-flash 0x0" not in joined
    device_timeouts = [
        line for line in timeout_calls(runtime) if "--chip esp32" in line
    ]
    assert device_timeouts[0].startswith("--kill-after=5s 45s ")
    assert device_timeouts[1].startswith("--kill-after=5s 600s ")


@pytest.mark.parametrize(
    "args",
    [
        [],
        ["--version", "../../bad", "--port", "/dev/null", "--esptool", "/bin/true"],
        ["--version", VERSION, "--port", "/not/a/device", "--esptool", "/bin/true"],
        ["--version", VERSION, "--port", "/dev/null", "--esptool", "esptool"],
    ],
)
def test_invalid_arguments_do_not_touch_device(runtime, args):
    env = os.environ.copy()
    env.update(runtime[0])
    result = subprocess.run(
        ["/bin/bash", str(SCRIPT), *args], text=True, capture_output=True, env=env
    )

    assert result.returncode != 0
    assert calls(runtime) == []


def test_download_failure_does_not_touch_device(runtime):
    result = run_upload(runtime, env_update={"FAKE_CURL_FAIL": "1"})

    assert result.returncode != 0
    assert calls(runtime) == []


def test_checksum_mismatch_does_not_touch_device(runtime, release):
    (release / FIRMWARE_NAME).write_bytes(b"\xe9" + b"tampered" * 32)

    result = run_upload(runtime)

    assert result.returncode != 0
    assert "firmware checksum mismatch" in result.stderr
    assert calls(runtime) == []


def test_partition_checksum_mismatch_does_not_touch_device(runtime, release):
    (release / PARTITIONS_NAME).write_bytes(b"tampered".ljust(4096, b"\xff"))

    result = run_upload(runtime)

    assert result.returncode != 0
    assert "partition metadata checksum mismatch" in result.stderr
    assert calls(runtime) == []


def test_non_esp32_image_does_not_touch_device(runtime, release):
    firmware = b"x" * 64
    (release / FIRMWARE_NAME).write_bytes(firmware)
    partitions = (release / PARTITIONS_NAME).read_bytes()
    (release / "sha256.txt").write_text(
        f"{_sha256(firmware)}  dist/{FIRMWARE_NAME}\n"
        f"{_sha256(partitions)}  dist/{PARTITIONS_NAME}\n"
    )

    result = run_upload(runtime)

    assert result.returncode != 0
    assert calls(runtime) == []


def test_duplicate_checksum_entry_is_rejected_before_device_access(runtime, release):
    checksum = release / "sha256.txt"
    checksum.write_text(
        checksum.read_text() + checksum.read_text().splitlines()[0] + "\n"
    )

    result = run_upload(runtime)

    assert result.returncode != 0
    assert calls(runtime) == []


def test_oversized_firmware_is_rejected_before_device_access(runtime, release):
    firmware = release / FIRMWARE_NAME
    firmware.write_bytes(b"x" * (0x300000 + 1))
    partitions = (release / PARTITIONS_NAME).read_bytes()
    (release / "sha256.txt").write_text(
        f"{_sha256(firmware.read_bytes())}  dist/{FIRMWARE_NAME}\n"
        f"{_sha256(partitions)}  dist/{PARTITIONS_NAME}\n"
    )

    result = run_upload(runtime)

    assert result.returncode != 0
    assert calls(runtime) == []


def test_wrong_partition_asset_size_is_rejected_before_device_access(runtime, release):
    partitions = b"short"
    (release / PARTITIONS_NAME).write_bytes(partitions)
    firmware = (release / FIRMWARE_NAME).read_bytes()
    (release / "sha256.txt").write_text(
        f"{_sha256(firmware)}  dist/{FIRMWARE_NAME}\n"
        f"{_sha256(partitions)}  dist/{PARTITIONS_NAME}\n"
    )

    result = run_upload(runtime)

    assert result.returncode != 0
    assert calls(runtime) == []


def test_device_layout_mismatch_never_writes(runtime):
    Path(runtime[0]["FAKE_DEVICE_PARTITIONS"]).write_bytes(b"z" * 4096)

    result = run_upload(runtime)

    assert result.returncode != 0
    assert len(calls(runtime)) == 1
    assert "read-flash 0x8000 0x1000" in calls(runtime)[0]


def test_partition_read_failure_never_writes_or_reports_success(runtime):
    result = run_upload(runtime, env_update={"FAKE_READ_FAIL": "1"})

    assert result.returncode != 0
    assert "UPLOAD_OK" not in result.stdout
    assert len(calls(runtime)) == 1
    assert "read-flash 0x8000 0x1000" in calls(runtime)[0]


def test_write_failure_has_no_success_marker(runtime):
    result = run_upload(runtime, env_update={"FAKE_WRITE_FAIL": "1"})

    assert result.returncode != 0
    assert "UPLOAD_OK" not in result.stdout
    assert len(calls(runtime)) == 2


def test_failure_removes_temporary_release_files(runtime, tmp_path):
    result = run_upload(
        runtime,
        env_update={"FAKE_WRITE_FAIL": "1", "TMPDIR": str(tmp_path)},
    )

    assert result.returncode != 0
    assert list(tmp_path.glob("battlebang-esp-upload.*")) == []


def test_device_timeout_has_no_success_marker(runtime):
    result = run_upload(runtime, env_update={"FAKE_TIMEOUT": "1"})

    assert result.returncode != 0
    assert "UPLOAD_OK" not in result.stdout
    assert calls(runtime) == []


def test_runtime_script_has_no_forbidden_deployment_dependencies():
    text = SCRIPT.read_text()
    for forbidden in ("python", "pip", "platformio", "pio", "docker", "git", "jq"):
        assert not re.search(rf"\b{forbidden}\b", text, re.IGNORECASE)


def run_resolve(runtime, *args, env_update=None):
    env, esptool = runtime
    return subprocess.run(
        ["/bin/bash", str(SCRIPT), "--resolve-only", "--esptool", str(esptool), *args],
        env=os.environ | env | (env_update or {}),
        capture_output=True,
        text=True,
        timeout=10,
    )


def test_resolve_automatically_selects_product_version_and_single_usb_without_io(
    runtime,
):
    result = run_resolve(runtime)
    assert result.returncode == 0, result.stderr
    assert dict(line.split("=", 1) for line in result.stdout.splitlines()) == {
        "version": VERSION,
        "port": "/dev/null",
        "esptool": str(runtime[1]),
        "vid": "10c4",
        "pid": "ea60",
        "driver": "cp210x",
    }
    assert calls(runtime) == []


def test_resolve_explicit_version_does_not_download_pointer(runtime):
    result = run_resolve(
        runtime, "--version", "0.3.99-test", env_update={"FAKE_CURL_FAIL": "1"}
    )
    assert result.returncode == 0, result.stderr
    assert "version=0.3.99-test" in result.stdout
    assert calls(runtime) == []


def test_ambiguous_usb_does_not_probe_either_device(runtime):
    result = run_resolve(runtime, env_update={"FAKE_AMBIGUOUS": "1"})
    assert result.returncode != 0
    assert "multiple" in result.stderr.lower()
    assert calls(runtime) == []


def test_unapproved_usb_is_rejected_even_with_explicit_port(runtime):
    result = run_resolve(
        runtime, "--port", "/dev/null", env_update={"FAKE_VID": "ffff"}
    )
    assert result.returncode != 0
    assert "unsupported USB" in result.stderr
    assert calls(runtime) == []


@pytest.mark.parametrize(
    "value", ["../../bad\n", "0.3.39-main\n0.3.40-main\n", "0.3.39; touch /tmp/oops\n"]
)
def test_bad_upload_only_pointer_fails_closed(runtime, release, value):
    (release / "usb-version.txt").write_text(value)
    result = run_resolve(runtime)
    assert result.returncode != 0
    assert calls(runtime) == []


def test_tool_smoke_failure_is_before_device_access(runtime):
    result = run_upload(runtime, env_update={"FAKE_HELP_FAIL": "1"})
    assert result.returncode != 0
    assert "uploader" in result.stderr
    assert calls(runtime) == []


def test_auto_installer_pins_official_archive_and_member_hashes():
    text = SCRIPT.read_text()
    assert "7723120429394b4d6d974d694e5cf378e00259381a7f8856c2c88492041b9f59" in text
    assert "d2662a740a2179cc3fa28713df13b7106e1f8108700bc62a86b13564b6d01ab7" in text
    assert "esptool-linux-aarch64/esptool" in text
    assert '"$esptool" -h' in text


@pytest.mark.parametrize("corrupt", [False, True])
def test_first_use_bootstrap_checks_archive_then_installs_one_executable(
    runtime, tmp_path, corrupt
):
    import io
    import tarfile

    env, fake_tool = runtime
    tools = Path(env["PATH"])
    (tools / "uname").unlink()
    (tools / "uname").write_text(
        "#!/bin/bash\n[[ $1 == -s ]] && echo Linux || echo aarch64\n"
    )
    (tools / "uname").chmod(0o755)
    archive = Path(env["FAKE_RELEASE_DIR"]) / "esptool-v5.6.0-linux-aarch64.tar.gz"
    with tarfile.open(archive, "w:gz") as output:
        info = tarfile.TarInfo("esptool-linux-aarch64/esptool")
        info.size = len(fake_tool.read_bytes())
        output.addfile(info, io.BytesIO(fake_tool.read_bytes()))
    # Fake only the two official binary digests, keeping real extraction and all control flow.
    sha = tools / "sha256sum"
    sha.write_text("""#!/bin/bash
case "$1" in
  *.tar.gz) printf '%s  %s\\n' "${FAKE_ARCHIVE_HASH:-7723120429394b4d6d974d694e5cf378e00259381a7f8856c2c88492041b9f59}" "$1" ;;
  */esptool) printf '%s  %s\\n' d2662a740a2179cc3fa28713df13b7106e1f8108700bc62a86b13564b6d01ab7 "$1" ;;
  *) /usr/bin/shasum -a 256 "$1" ;;
esac
""")
    cache_home = tmp_path / "home"
    custom_env = os.environ | env | {"HOME": str(cache_home)}
    if corrupt:
        custom_env["FAKE_ARCHIVE_HASH"] = "0" * 64
    result = subprocess.run(
        ["/bin/bash", str(SCRIPT), "--resolve-only"],
        env=custom_env,
        capture_output=True,
        text=True,
        timeout=10,
    )
    cached = cache_home / ".local/share/battlebang-esp/esptool-v5.6.0/esptool"
    assert calls(runtime) == []
    if corrupt:
        assert result.returncode != 0
        assert "archive checksum mismatch" in result.stderr
        assert not cached.exists()
    else:
        assert result.returncode == 0, result.stderr
        assert cached.read_bytes() == fake_tool.read_bytes()
        assert f"esptool={cached}" in result.stdout
        assert list(cached.parent.iterdir()) == [cached]
        # Reuse the verified cache without network or rebuilding any environment.
        (Path(env["FAKE_RELEASE_DIR"]) / "usb-version.txt").unlink()
        again = subprocess.run(
            ["/bin/bash", str(SCRIPT), "--resolve-only", "--version", VERSION],
            env=custom_env | {"FAKE_CURL_FAIL": "1"},
            capture_output=True,
            text=True,
        )
        assert again.returncode == 0, again.stderr
