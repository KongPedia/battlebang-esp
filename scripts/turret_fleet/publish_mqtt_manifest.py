#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description="Publish OTA manifest to MQTT. Requires paho-mqtt.")
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--topic", default="battlebang/turrets/all/ota")
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--username", default="")
    parser.add_argument("--password", default="")
    parser.add_argument("--retain", action="store_true")
    args = parser.parse_args()

    try:
        import paho.mqtt.client as mqtt  # type: ignore
    except ImportError as exc:
        raise SystemExit("Install paho-mqtt first: python3 -m pip install paho-mqtt") from exc

    payload = args.manifest.read_text(encoding="utf-8")
    try:
        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    except AttributeError:
        # paho-mqtt 1.x compatibility for laptops that have an older package.
        client = mqtt.Client()
    if args.username:
        client.username_pw_set(args.username, args.password)
    client.connect(args.host, args.port, keepalive=30)
    info = client.publish(args.topic, payload=payload, qos=1, retain=args.retain)
    info.wait_for_publish()
    client.disconnect()
    print(f"published {args.manifest} -> mqtt://{args.host}:{args.port}/{args.topic} retain={args.retain}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
