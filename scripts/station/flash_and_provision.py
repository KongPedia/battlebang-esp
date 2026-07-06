#!/usr/bin/env python3
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[2]
PROVISION_SCRIPT = PROJECT_ROOT / "scripts" / "station" / "provision.py"
UPLOAD_COMMAND_HELP = "pio run -e esp32dev_station -t upload"


class StationFlashError(RuntimeError):
    pass


def parse_station(value: str) -> tuple[str, str]:
    if "=" not in value:
        raise argparse.ArgumentTypeError("station mapping must be station_id=/dev/ttyUSBx")
    station_id, serial_port = value.split("=", 1)
    station_id = station_id.strip()
    serial_port = serial_port.strip()
    if not station_id or not serial_port:
        raise argparse.ArgumentTypeError("station mapping requires both station_id and serial_port")
    return station_id, serial_port


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Upload the same Station firmware image, then provision each board over USB serial.",
        epilog=(
            "Example: scripts/station/flash_and_provision.py "
            "--station station_01=/dev/cu.usbserial-110 "
            "--station station_06=/dev/cu.usbserial-160"
        ),
    )
    parser.add_argument("--station", action="append", type=parse_station, required=True, help="station_id=serial_port; repeat for six boards")
    parser.add_argument("--pio", default=str(PROJECT_ROOT / ".venv-pio" / "bin" / "pio"), help="PlatformIO executable")
    parser.add_argument("--python", default=sys.executable, help="Python executable for provision.py")
    parser.add_argument("--env-file", type=Path, default=PROJECT_ROOT / "firmware" / "station" / ".env.station")
    parser.add_argument("--no-upload", action="store_true", help="skip PlatformIO upload and only provision over serial")
    parser.add_argument("--no-provision", action="store_true", help="only upload firmware")
    parser.add_argument("--dry-run", action="store_true", help="print commands without running them")
    return parser


def run_or_print(cmd: list[str], *, dry_run: bool) -> None:
    print("+", " ".join(str(part) for part in cmd))
    if not dry_run:
        subprocess.run(cmd, check=True, cwd=PROJECT_ROOT)


def main(argv: list[str] | None = None) -> int:
    args = make_parser().parse_args(argv)
    for station_id, serial_port in args.station:
        if not args.no_upload:
            upload_cmd = [args.pio, "run", "-e", "esp32dev_station", "-t", "upload", "--upload-port", serial_port]
            # Equivalent shell form: pio run -e esp32dev_station -t upload --upload-port <serial_port>
            subprocess.run(upload_cmd, check=True, cwd=PROJECT_ROOT) if not args.dry_run else print("+", " ".join(upload_cmd))
        if not args.no_provision:
            provision_cmd = [
                args.python,
                str(PROVISION_SCRIPT),
                "--env-file",
                str(args.env_file),
                "--station-id",
                station_id,
                "--serial-port",
                serial_port,
                "--print-json",
            ]
            run_or_print(provision_cmd, dry_run=args.dry_run)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as exc:
        print(f"error: command failed with exit {exc.returncode}: {exc.cmd}", file=sys.stderr)
        raise SystemExit(exc.returncode)
    except StationFlashError as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(2)
