#!/usr/bin/env python3
"""Create an MBR disk with two valid Linux swap v1 partitions."""

from __future__ import annotations

import argparse
import pathlib
import struct


SECTOR_BYTES = 512
PAGE_BYTES = 4096
DISK_SECTORS = 65536
PARTITIONS = ((2048, 24576), (28672, 24576))
LINUX_SWAP_TYPE = 0x82
LINUX_SWAP_MAGIC = b"SWAPSPACE2"


def partition_entry(start: int, sectors: int) -> bytes:
    return struct.pack(
        "<B3sB3sII",
        0,
        b"\x00\x02\x00",
        LINUX_SWAP_TYPE,
        b"\xff\xff\xff",
        start,
        sectors,
    )


def swap_header(sectors: int) -> bytes:
    pages = sectors * SECTOR_BYTES // PAGE_BYTES
    if pages < 2:
        raise ValueError("swap partition is too small")
    header = bytearray(PAGE_BYTES)
    struct.pack_into("<III", header, 1024, 1, pages - 1, 0)
    header[-len(LINUX_SWAP_MAGIC) :] = LINUX_SWAP_MAGIC
    return bytes(header)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()

    image = bytearray(DISK_SECTORS * SECTOR_BYTES)
    for index, (start, sectors) in enumerate(PARTITIONS):
        offset = 446 + index * 16
        image[offset : offset + 16] = partition_entry(start, sectors)
        header_offset = start * SECTOR_BYTES
        image[header_offset : header_offset + PAGE_BYTES] = swap_header(
            sectors
        )
    image[510:512] = b"\x55\xaa"
    args.output.write_bytes(image)


if __name__ == "__main__":
    main()
