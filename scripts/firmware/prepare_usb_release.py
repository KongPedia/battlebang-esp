#!/usr/bin/env python3
"""CI-only: publish the approved Go2 Full USB partition sector for comparison.

The sector is a read-only upload guard, never an instruction to flash partitions.
Jetson runs the shell uploader, not this build helper.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path


def approved_partition_sector() -> bytes:
    # Arduino ESP32 huge_app.csv, including the ESP-IDF table MD5 entry.
    partitions = [
        (1, 2, 0x9000, 0x5000, b"nvs"),
        (1, 0, 0xE000, 0x2000, b"otadata"),
        (0, 16, 0x10000, 0x300000, b"app0"),
        (1, 130, 0x310000, 0xE0000, b"spiffs"),
        (1, 3, 0x3F0000, 0x10000, b"coredump"),
    ]
    entries = b"".join(
        struct.pack("<2sBBLL16sL", b"\xaa\x50", *partition, 0)
        for partition in partitions
    )
    table = entries + b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(entries).digest()
    return table.ljust(4096, b"\xff")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware", type=Path, required=True)
    parser.add_argument("--partitions", type=Path, required=True)
    args = parser.parse_args()
    size = args.firmware.stat().st_size
    with args.firmware.open("rb") as image:
        magic = image.read(1)
    if not 32 <= size <= 0x300000 or magic != b"\xe9":
        parser.error("invalid ESP32 app: expected image magic and size 32..3145728")
    table = args.partitions.read_bytes()
    if (
        len(table) not in (3072, 4096)
        or table.ljust(4096, b"\xff") != approved_partition_sector()
    ):
        parser.error(
            "unapproved partition layout: only unchanged huge_app is supported"
        )
    output = args.firmware.with_suffix(".partitions.bin")
    output.write_bytes(approved_partition_sector())
    print(output)


if __name__ == "__main__":
    main()
