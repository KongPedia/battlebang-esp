"""Common firmware release workflow contracts; never publishes a release."""

from __future__ import annotations

import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github" / "workflows" / "firmware-ota.yml"


def test_versioned_release_cannot_clobber_existing_assets(tmp_path):
    workflow = WORKFLOW.read_text()
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


def test_release_builds_are_serialized_per_firmware_across_refs():
    build = WORKFLOW.read_text().split("\n  build:\n", 1)[1]
    assert 'group: firmware-release-${{ matrix.id }}' in build
    assert "cancel-in-progress: false" in build
