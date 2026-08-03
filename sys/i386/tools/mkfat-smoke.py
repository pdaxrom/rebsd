#!/usr/bin/env python3
"""Build an MBR disk whose first partition contains a FAT16 filesystem."""

from __future__ import annotations

import argparse
import pathlib
import struct
import subprocess


SECTOR_SIZE = 512
TOTAL_SECTORS = 32768
PARTITION_START = 2048
PARTITION_SECTORS = TOTAL_SECTORS - PARTITION_START


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mkfs", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    mkfs = args.mkfs.resolve()
    partition = args.output.with_suffix(args.output.suffix + ".partition")
    try:
        with partition.open("wb") as output:
            output.truncate(PARTITION_SECTORS * SECTOR_SIZE)
        subprocess.run(
            [mkfs, "-F", "16", "-n", "REBSDFAT", partition],
            check=True,
        )

        mbr = bytearray(SECTOR_SIZE)
        mbr[446] = 0x80
        mbr[450] = 0x06
        mbr[454:458] = struct.pack("<I", PARTITION_START)
        mbr[458:462] = struct.pack("<I", PARTITION_SECTORS)
        mbr[510:512] = b"\x55\xaa"
        with args.output.open("wb") as output:
            output.write(mbr)
            output.truncate(TOTAL_SECTORS * SECTOR_SIZE)
        with partition.open("rb") as source, args.output.open("r+b") as output:
            output.seek(PARTITION_START * SECTOR_SIZE)
            while data := source.read(128 * 1024):
                output.write(data)
    finally:
        partition.unlink(missing_ok=True)

    print(
        f"mkfat-smoke: {args.output} partition 1 start={PARTITION_START} "
        f"sectors={PARTITION_SECTORS}"
    )


if __name__ == "__main__":
    main()
