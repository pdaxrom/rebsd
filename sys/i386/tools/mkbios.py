#!/usr/bin/env python3
"""Wrap the i686 Linux/x86 image in a deterministic BIOS floppy image."""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import struct


FLOPPY_BYTES = 1440 * 1024
SECTOR_BYTES = 512
BOOT_FLAG_OFFSET = 0x1FE
SETUP_SECTS_OFFSET = 0x1F1
SYSSIZE_OFFSET = 0x1F4
HEADER_OFFSET = 0x202
FLOPPY_SECTORS_PER_TRACK = 18


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--kernel", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    image = args.kernel.read_bytes()

    if len(image) < 2 * SECTOR_BYTES:
        raise SystemExit("mkbios: kernel image is too small")
    if len(image) > FLOPPY_BYTES:
        raise SystemExit(
            f"mkbios: kernel image is {len(image)} bytes; "
            f"floppy limit is {FLOPPY_BYTES}"
        )
    if image[0] not in (0xE9, 0xEB):
        raise SystemExit("mkbios: first sector has no BIOS entry jump")
    if struct.unpack_from("<H", image, BOOT_FLAG_OFFSET)[0] != 0xAA55:
        raise SystemExit("mkbios: first sector has no 0xAA55 signature")
    if image[HEADER_OFFSET : HEADER_OFFSET + 4] != b"HdrS":
        raise SystemExit("mkbios: image has no Linux/x86 HdrS header")

    setup_sectors = image[SETUP_SECTS_OFFSET]
    if setup_sectors == 0 or setup_sectors >= FLOPPY_SECTORS_PER_TRACK:
        raise SystemExit(
            "mkbios: remaining setup must fit after the boot sector "
            "in the first floppy track"
        )
    setup_bytes = (setup_sectors + 1) * SECTOR_BYTES
    if setup_bytes > len(image):
        raise SystemExit("mkbios: setup sectors extend beyond kernel image")

    syssize = struct.unpack_from("<I", image, SYSSIZE_OFFSET)[0]
    if syssize == 0:
        raise SystemExit("mkbios: kernel syssize is zero")
    kernel_sectors = (syssize + 31) // 32
    if setup_bytes + kernel_sectors * SECTOR_BYTES > FLOPPY_BYTES:
        raise SystemExit("mkbios: setup and padded kernel exceed floppy image")

    output = image + bytes(FLOPPY_BYTES - len(image))
    args.output.write_bytes(output)
    digest = hashlib.sha256(output).hexdigest()
    print(
        f"mkbios: {args.output} image={len(image)} "
        f"floppy={len(output)} sectors={len(output) // SECTOR_BYTES} "
        f"sha256={digest}"
    )


if __name__ == "__main__":
    main()
