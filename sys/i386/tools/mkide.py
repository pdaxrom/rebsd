#!/usr/bin/env python3
"""Build a deterministic, non-filesystem ATA smoke-test disk image."""

from __future__ import annotations

import argparse
import os
import pathlib
import tempfile


SECTOR_SIZE = 512
DEFAULT_SECTORS = 4096
MARKER = b"REBSDIDE"
MBR_SIGNATURE = b"\x55\xaa"


def build_image(sectors: int) -> bytes:
    if sectors < 1 or sectors > 0x0FFFFFFF:
        raise ValueError("sector count must be in the ATA LBA28 range")
    image = bytearray(sectors * SECTOR_SIZE)
    image[: len(MARKER)] = MARKER
    image[8:24] = b"READ-ONLY-SMOKE\0"
    image[510:512] = MBR_SIGNATURE
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
