#!/usr/bin/env python3
"""Build an MBR disk whose first partition contains a FAT filesystem."""

from __future__ import annotations

import argparse
import pathlib
import struct
import subprocess


SECTOR_SIZE = 512
PARTITION_START = 2048


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mkfs", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--fat", choices=(16, 32), type=int, default=16)
    parser.add_argument("--declared-extra-sectors", type=int, default=0)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    mkfs = args.mkfs.resolve()
    total_sectors = 32768 if args.fat == 16 else 262144
    partition_sectors = total_sectors - PARTITION_START
    partition_type = 0x06 if args.fat == 16 else 0x0c
    partition = args.output.with_suffix(args.output.suffix + ".partition")
    try:
        with partition.open("wb") as output:
            output.truncate(partition_sectors * SECTOR_SIZE)
        subprocess.run(
            [mkfs, "-F", str(args.fat), "-n", "REBSDFAT", partition],
            check=True,
        )
        if args.declared_extra_sectors:
            if args.fat != 32:
                raise ValueError("an oversized BPB fixture requires FAT32")
            declared = partition_sectors + args.declared_extra_sectors
            with partition.open("r+b") as output:
                boot = output.read(SECTOR_SIZE)
                backup = struct.unpack_from("<H", boot, 50)[0]
                output.seek(32)
                output.write(struct.pack("<I", declared))
                if backup:
                    output.seek(backup * SECTOR_SIZE + 32)
                    output.write(struct.pack("<I", declared))

        mbr = bytearray(SECTOR_SIZE)
        mbr[446] = 0x80
        mbr[450] = partition_type
        mbr[454:458] = struct.pack("<I", PARTITION_START)
        mbr[458:462] = struct.pack("<I", partition_sectors)
        mbr[510:512] = b"\x55\xaa"
        with args.output.open("wb") as output:
            output.write(mbr)
            output.truncate(total_sectors * SECTOR_SIZE)
        with partition.open("rb") as source, args.output.open("r+b") as output:
            output.seek(PARTITION_START * SECTOR_SIZE)
            while data := source.read(128 * 1024):
                output.write(data)
    finally:
        partition.unlink(missing_ok=True)

    print(
        f"mkfat-smoke: {args.output} partition 1 start={PARTITION_START} "
        f"sectors={partition_sectors} FAT{args.fat} "
        f"declared-extra={args.declared_extra_sectors}"
    )


if __name__ == "__main__":
    main()
