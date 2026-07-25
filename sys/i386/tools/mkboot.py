#!/usr/bin/env python3
"""Build a Linux/x86 boot-protocol image from setup and kernel binaries."""

from __future__ import annotations

import argparse
import pathlib
import struct


SETUP_SECTS_OFFSET = 0x1F1
SYSSIZE_OFFSET = 0x1F4
BOOT_FLAG_OFFSET = 0x1FE
HEADER_OFFSET = 0x202
VERSION_OFFSET = 0x206
LOADFLAGS_OFFSET = 0x211
CODE32_START_OFFSET = 0x214

BOOT_FLAG = 0xAA55
HEADER_MAGIC = b"HdrS"
MIN_PROTOCOL = 0x0202
LOAD_HIGH = 0x01
KERNEL_LOAD_ADDRESS = 0x00100000


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--setup", required=True, type=pathlib.Path)
    parser.add_argument("--kernel", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    return parser.parse_args()


def get_u16(data: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def get_u32(data: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def validate_setup(setup: bytearray) -> None:
    if len(setup) < 1024:
        raise SystemExit("mkboot: setup image is too small")
    setup_sects = setup[SETUP_SECTS_OFFSET]
    expected_size = (setup_sects + 1) * 512
    if len(setup) != expected_size:
        raise SystemExit(
            f"mkboot: setup size {len(setup)} != header size {expected_size}"
        )
    if get_u16(setup, BOOT_FLAG_OFFSET) != BOOT_FLAG:
        raise SystemExit("mkboot: missing 0xAA55 boot flag")
    if setup[HEADER_OFFSET : HEADER_OFFSET + 4] != HEADER_MAGIC:
        raise SystemExit("mkboot: missing HdrS boot protocol magic")
    if get_u16(setup, VERSION_OFFSET) < MIN_PROTOCOL:
        raise SystemExit("mkboot: Linux/x86 boot protocol is older than 2.02")
    if setup[LOADFLAGS_OFFSET] & LOAD_HIGH == 0:
        raise SystemExit("mkboot: setup image is not marked LOAD_HIGH")
    if get_u32(setup, CODE32_START_OFFSET) != KERNEL_LOAD_ADDRESS:
        raise SystemExit("mkboot: protected-mode entry is not 1 MiB")


def main() -> None:
    args = parse_args()
    setup = bytearray(args.setup.read_bytes())
    kernel = args.kernel.read_bytes()
    validate_setup(setup)

    if not kernel:
        raise SystemExit("mkboot: protected-mode payload is empty")
    syssize = (len(kernel) + 15) // 16
    if syssize > 0xFFFFFFFF:
        raise SystemExit("mkboot: protected-mode payload is too large")
    struct.pack_into("<I", setup, SYSSIZE_OFFSET, syssize)

    args.output.write_bytes(setup + kernel)
    print(
        f"mkboot: {args.output} setup={len(setup)} kernel={len(kernel)} "
        f"syssize={syssize}"
    )


if __name__ == "__main__":
    main()
