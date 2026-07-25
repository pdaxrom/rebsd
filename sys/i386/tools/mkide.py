#!/usr/bin/env python3
"""Build a deterministic, non-filesystem ATA smoke-test disk image."""

from __future__ import annotations

import argparse
import os
import pathlib
import struct
import tempfile


SECTOR_SIZE = 512
DEFAULT_SECTORS = 4096
MARKER = b"REBSDIDE"
PARTITION_MARKER = b"REBSDPART"
NEXT_MARKER = b"REBSDNEXT"
END_MARKER = b"REBSDEND"
MBR_SIGNATURE = b"\x55\xaa"
MBR_PARTITION_OFFSET = 446
PARTITION_TYPE = 0xB7
PARTITION_START = 64


def build_image(sectors: int) -> bytes:
    if sectors <= PARTITION_START + 1 or sectors > 0x0FFFFFFF:
        raise ValueError("sector count must be in the ATA LBA28 range")
    image = bytearray(sectors * SECTOR_SIZE)
    image[: len(MARKER)] = MARKER
    image[8:24] = b"READ-ONLY-SMOKE\0"
    partition_sectors = sectors - PARTITION_START
    entry = MBR_PARTITION_OFFSET
    image[entry] = 0x80
    image[entry + 1 : entry + 4] = b"\x00\x02\x00"
    image[entry + 4] = PARTITION_TYPE
    image[entry + 5 : entry + 8] = b"\xfe\xff\xff"
    struct.pack_into(
        "<II", image, entry + 8, PARTITION_START, partition_sectors
    )
    image[510:512] = MBR_SIGNATURE
    partition_offset = PARTITION_START * SECTOR_SIZE
    image[
        partition_offset : partition_offset + len(PARTITION_MARKER)
    ] = PARTITION_MARKER
    next_offset = partition_offset + SECTOR_SIZE
    image[next_offset : next_offset + len(NEXT_MARKER)] = NEXT_MARKER
    end_offset = (sectors - 1) * SECTOR_SIZE
    image[end_offset : end_offset + len(END_MARKER)] = END_MARKER
    return bytes(image)


def write_atomic(path: pathlib.Path, contents: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary_name = ""
    try:
        with tempfile.NamedTemporaryFile(
            prefix=path.name + ".", dir=path.parent, delete=False
        ) as temporary:
            temporary.write(contents)
            temporary_name = temporary.name
        os.replace(temporary_name, path)
    finally:
        if temporary_name:
            try:
                os.unlink(temporary_name)
            except FileNotFoundError:
                pass


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--sectors", type=int, default=DEFAULT_SECTORS)
    args = parser.parse_args()
    try:
        image = build_image(args.sectors)
        write_atomic(args.output, image)
    except (OSError, ValueError) as error:
        raise SystemExit(f"mkide: {error}") from error
    print(
        f"mkide: {args.output} sectors={args.sectors} "
        f"bytes={len(image)} marker={MARKER.decode()}"
    )


if __name__ == "__main__":
    main()
