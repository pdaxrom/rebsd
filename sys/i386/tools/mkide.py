#!/usr/bin/env python3
"""Build a deterministic MBR/FAT16 ATA smoke-test disk image."""

from __future__ import annotations

import argparse
import os
import pathlib
import struct
import tempfile


SECTOR_SIZE = 512
DEFAULT_SECTORS = 8192
MARKER = b"REBSDIDE"
END_MARKER = b"REBSDEND"
MBR_SIGNATURE = b"\x55\xaa"
MBR_PARTITION_OFFSET = 446
PARTITION_TYPE = 0x06
PARTITION_START = 64
FAT_COUNT = 2
FAT_RESERVED_SECTORS = 1
FAT_ROOT_ENTRIES = 64
FAT_ROOT_SECTORS = FAT_ROOT_ENTRIES * 32 // SECTOR_SIZE
FAT16_MIN_CLUSTERS = 4085
FAT16_MAX_CLUSTERS = 65524
ROOT_FILE_SIZE = 700


def fat16_geometry(partition_sectors: int) -> tuple[int, int, int]:
    for sectors_per_cluster in (1, 2, 4, 8, 16, 32, 64, 128):
        fat_sectors = 1
        for _ in range(32):
            overhead = (
                FAT_RESERVED_SECTORS
                + FAT_COUNT * fat_sectors
                + FAT_ROOT_SECTORS
            )
            if overhead >= partition_sectors:
                break
            clusters = (
                partition_sectors - overhead
            ) // sectors_per_cluster
            required = (clusters + 2) * 2
            required = (required + SECTOR_SIZE - 1) // SECTOR_SIZE
            if required == fat_sectors:
                if FAT16_MIN_CLUSTERS <= clusters <= FAT16_MAX_CLUSTERS:
                    return sectors_per_cluster, fat_sectors, clusters
                break
            fat_sectors = required
    raise ValueError("partition is outside the supported FAT16 geometry")


def put_dirent(
    data: bytearray,
    offset: int,
    short_name: bytes,
    attributes: int,
    cluster: int,
    size: int,
) -> None:
    if len(short_name) != 11:
        raise ValueError("FAT short name must contain exactly 11 bytes")
    data[offset : offset + 32] = bytes(32)
    data[offset : offset + 11] = short_name
    data[offset + 11] = attributes
    struct.pack_into("<H", data, offset + 20, cluster >> 16)
    struct.pack_into("<H", data, offset + 26, cluster & 0xFFFF)
    struct.pack_into("<I", data, offset + 28, size)


def build_fat16(
    image: bytearray, partition_offset: int, partition_sectors: int
) -> None:
    sectors_per_cluster, fat_sectors, _ = fat16_geometry(
        partition_sectors
    )
    boot = memoryview(image)[partition_offset : partition_offset + SECTOR_SIZE]
    boot[0:3] = b"\xeb\x3c\x90"
    boot[3:11] = b"REBSD   "
    struct.pack_into("<H", boot, 11, SECTOR_SIZE)
    boot[13] = sectors_per_cluster
    struct.pack_into("<H", boot, 14, FAT_RESERVED_SECTORS)
    boot[16] = FAT_COUNT
    struct.pack_into("<H", boot, 17, FAT_ROOT_ENTRIES)
    if partition_sectors <= 0xFFFF:
        struct.pack_into("<H", boot, 19, partition_sectors)
    else:
        struct.pack_into("<I", boot, 32, partition_sectors)
    boot[21] = 0xF8
    struct.pack_into("<H", boot, 22, fat_sectors)
    struct.pack_into("<H", boot, 24, 63)
    struct.pack_into("<H", boot, 26, 16)
    struct.pack_into("<I", boot, 28, PARTITION_START)
    boot[36] = 0x80
    boot[38] = 0x29
    struct.pack_into("<I", boot, 39, 0x52454253)
    boot[43:54] = b"REBSD ROOT "
    boot[54:62] = b"FAT16   "
    boot[510:512] = MBR_SIGNATURE

    fat_bytes = fat_sectors * SECTOR_SIZE
    fat = bytearray(fat_bytes)
    struct.pack_into("<H", fat, 0, 0xFFF8)
    struct.pack_into("<H", fat, 2, 0xFFFF)
    struct.pack_into("<H", fat, 4, 0xFFFF)
    struct.pack_into("<H", fat, 6, 4)
    struct.pack_into("<H", fat, 8, 0xFFFF)
    for fat_index in range(FAT_COUNT):
        start = partition_offset + (
            FAT_RESERVED_SECTORS + fat_index * fat_sectors
        ) * SECTOR_SIZE
        image[start : start + fat_bytes] = fat

    root_sector = FAT_RESERVED_SECTORS + FAT_COUNT * fat_sectors
    root_offset = partition_offset + root_sector * SECTOR_SIZE
    put_dirent(image, root_offset, b"BOOT       ", 0x10, 2, 0)

    data_sector = root_sector + FAT_ROOT_SECTORS
    boot_dir_offset = partition_offset + data_sector * SECTOR_SIZE
    put_dirent(image, boot_dir_offset, b".          ", 0x10, 2, 0)
    put_dirent(image, boot_dir_offset + 32, b"..         ", 0x10, 0, 0)
    put_dirent(
        image, boot_dir_offset + 64, b"ROOT    TXT", 0x21, 3, ROOT_FILE_SIZE
    )

    contents = bytearray(b"." * ROOT_FILE_SIZE)
    contents[:15] = b"REBSD FAT ROOT\n"
    contents[512:537] = b"REBSD FAT SECOND CLUSTER\n"
    file_sector = data_sector + sectors_per_cluster
    file_offset = partition_offset + file_sector * SECTOR_SIZE
    image[file_offset : file_offset + len(contents)] = contents


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
    build_fat16(image, partition_offset, partition_sectors)
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
        f"bytes={len(image)} marker={MARKER.decode()} filesystem=FAT16"
    )


if __name__ == "__main__":
    main()
