#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

from mqtt_command import publish_mqtt


def main() -> int:
    parser = argparse.ArgumentParser(description="Publish OTA manifest to MQTT without external MQTT CLI/tools.")
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--topic", default="battlebang/turrets/all/ota")
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--username", default="")
    parser.add_argument("--password", default="")
    parser.add_argument("--timeout-s", type=float, default=5.0)
    args = parser.parse_args()

    payload = args.manifest.read_text(encoding="utf-8")
    publish_mqtt(
        host=args.host,
        port=args.port,
        topic=args.topic,
        payload=payload,
        username=args.username or None,
        password=args.password or None,
        timeout_s=args.timeout_s,
    )
    print(f"published {args.manifest} -> mqtt://{args.host}:{args.port}/{args.topic}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
