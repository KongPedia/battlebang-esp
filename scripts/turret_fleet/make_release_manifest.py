#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description="Create BattleBang turret fleet firmware manifest.")
    parser.add_argument("--firmware", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--build", type=int, required=True)
    parser.add_argument("--url", default="", help="HTTP URL reachable by ESP32 devices. Can be patched by command center later.")
    parser.add_argument("--job-id", default="")
    parser.add_argument("--channel", default="stable")
    parser.add_argument("--app", default="battlebang-turret-fleet")
    parser.add_argument("--hardware", default="esp32dev-turret-v2")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    firmware = args.firmware
    if not firmware.exists():
        raise SystemExit(f"firmware not found: {firmware}")

    manifest = {
        "type": "firmware",
        "job_id": args.job_id or f"fw-{args.version}-{args.build}",
        "channel": args.channel,
        "app": args.app,
        "hardware": args.hardware,
        "version": args.version,
        "build": args.build,
        "url": args.url,
        "sha256": sha256_file(firmware),
        "size": firmware.stat().st_size,
        "force": False,
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
