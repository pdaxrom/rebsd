#!/usr/bin/env python3
"""Build deterministic MBR/FAT16 and MBR/FAT32 ATA smoke-test images."""

from __future__ import annotations

import argparse
import os
import pathlib
import struct
import tempfile


SECTOR_SIZE = 512
DEFAULT_SECTORS = {"fat16": 8192, "fat32": 131072}
MARKER = b"REBSDIDE"
END_MARKER = b"REBSDEND"
MBR_SIGNATURE = b"\x55\xaa"
MBR_PARTITION_OFFSET = 446
FAT_COUNT = 2
FAT16_PARTITION_TYPE = 0x06
FAT16_PARTITION_START = 64
FAT16_RESERVED_SECTORS = 1
FAT16_ROOT_ENTRIES = 64
FAT16_ROOT_SECTORS = FAT16_ROOT_ENTRIES * 32 // SECTOR_SIZE
FAT16_MIN_CLUSTERS = 4085
FAT16_MAX_CLUSTERS = 65524
FAT32_PARTITION_TYPE = 0x0C
FAT32_PARTITION_START = 2048
FAT32_RESERVED_SECTORS = 32
FAT32_MIN_CLUSTERS = 65525
FAT32_MAX_CLUSTERS = 0x0FFFFFF5
ROOT_FILE_SIZE = 700


def fat16_geometry(partition_sectors: int) -> tuple[int, int, int]:
    for sectors_per_cluster in (1, 2, 4, 8, 16, 32, 64, 128):
        fat_sectors = 1
        for _ in range(32):
            overhead = (
                FAT16_RESERVED_SECTORS
                + FAT_COUNT * fat_sectors
                + FAT16_ROOT_SECTORS
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


def fat32_geometry(partition_sectors: int) -> tuple[int, int, int]:
    for sectors_per_cluster in (1, 2, 4, 8, 16, 32, 64, 128):
        fat_sectors = 1
        for _ in range(32):
            overhead = FAT32_RESERVED_SECTORS + FAT_COUNT * fat_sectors
            if overhead >= partition_sectors:
                break
            clusters = (
                partition_sectors - overhead
            ) // sectors_per_cluster
            required = (clusters + 2) * 4
            required = (required + SECTOR_SIZE - 1) // SECTOR_SIZE
            if required == fat_sectors:
                if FAT32_MIN_CLUSTERS <= clusters <= FAT32_MAX_CLUSTERS:
                    return sectors_per_cluster, fat_sectors, clusters
                break
            fat_sectors = required
    raise ValueError("partition is outside the supported FAT32 geometry")


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
    image: bytearray,
    partition_start: int,
    partition_sectors: int,
) -> None:
    sectors_per_cluster, fat_sectors, _ = fat16_geometry(
        partition_sectors
    )
    partition_offset = partition_start * SECTOR_SIZE
    boot = memoryview(image)[partition_offset : partition_offset + SECTOR_SIZE]
    boot[0:3] = b"\xeb\x3c\x90"
    boot[3:11] = b"REBSD   "
    struct.pack_into("<H", boot, 11, SECTOR_SIZE)
    boot[13] = sectors_per_cluster
    struct.pack_into("<H", boot, 14, FAT16_RESERVED_SECTORS)
    boot[16] = FAT_COUNT
    struct.pack_into("<H", boot, 17, FAT16_ROOT_ENTRIES)
    if partition_sectors <= 0xFFFF:
        struct.pack_into("<H", boot, 19, partition_sectors)
    else:
        struct.pack_into("<I", boot, 32, partition_sectors)
    boot[21] = 0xF8
    struct.pack_into("<H", boot, 22, fat_sectors)
    struct.pack_into("<H", boot, 24, 63)
    struct.pack_into("<H", boot, 26, 16)
    struct.pack_into("<I", boot, 28, partition_start)
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
            FAT16_RESERVED_SECTORS + fat_index * fat_sectors
        ) * SECTOR_SIZE
        image[start : start + fat_bytes] = fat

    root_sector = FAT16_RESERVED_SECTORS + FAT_COUNT * fat_sectors
    root_offset = partition_offset + root_sector * SECTOR_SIZE
    put_dirent(image, root_offset, b"BOOT       ", 0x10, 2, 0)

    data_sector = root_sector + FAT16_ROOT_SECTORS
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


def build_fat32(
    image: bytearray,
    partition_start: int,
    partition_sectors: int,
) -> None:
    sectors_per_cluster, fat_sectors, clusters = fat32_geometry(
        partition_sectors
    )
    partition_offset = partition_start * SECTOR_SIZE
    boot_bytes = bytearray(SECTOR_SIZE)
    boot_bytes[0:3] = b"\xeb\x58\x90"
    boot_bytes[3:11] = b"REBSD   "
    struct.pack_into("<H", boot_bytes, 11, SECTOR_SIZE)
    boot_bytes[13] = sectors_per_cluster
    struct.pack_into("<H", boot_bytes, 14, FAT32_RESERVED_SECTORS)
    boot_bytes[16] = FAT_COUNT
    boot_bytes[21] = 0xF8
    struct.pack_into("<H", boot_bytes, 24, 63)
    struct.pack_into("<H", boot_bytes, 26, 16)
    struct.pack_into("<I", boot_bytes, 28, partition_start)
    struct.pack_into("<I", boot_bytes, 32, partition_sectors)
    struct.pack_into("<I", boot_bytes, 36, fat_sectors)
    struct.pack_into("<I", boot_bytes, 44, 2)
    struct.pack_into("<H", boot_bytes, 48, 1)
    struct.pack_into("<H", boot_bytes, 50, 6)
    boot_bytes[64] = 0x80
    boot_bytes[66] = 0x29
    struct.pack_into("<I", boot_bytes, 67, 0x52454253)
    boot_bytes[71:82] = b"REBSD ROOT "
    boot_bytes[82:90] = b"FAT32   "
    boot_bytes[510:512] = MBR_SIGNATURE
    image[partition_offset : partition_offset + SECTOR_SIZE] = boot_bytes
    backup_offset = partition_offset + 6 * SECTOR_SIZE
    image[backup_offset : backup_offset + SECTOR_SIZE] = boot_bytes

    fsinfo = bytearray(SECTOR_SIZE)
    struct.pack_into("<I", fsinfo, 0, 0x41615252)
    struct.pack_into("<I", fsinfo, 484, 0x61417272)
    struct.pack_into("<I", fsinfo, 488, clusters - 4)
    struct.pack_into("<I", fsinfo, 492, 6)
    struct.pack_into("<I", fsinfo, 508, 0xAA550000)
    fsinfo_offset = partition_offset + SECTOR_SIZE
    image[fsinfo_offset : fsinfo_offset + SECTOR_SIZE] = fsinfo
    backup_fsinfo_offset = partition_offset + 7 * SECTOR_SIZE
    image[
        backup_fsinfo_offset : backup_fsinfo_offset + SECTOR_SIZE
    ] = fsinfo

    fat_bytes = fat_sectors * SECTOR_SIZE
    fat = bytearray(fat_bytes)
    struct.pack_into("<I", fat, 0, 0x0FFFFFF8)
    struct.pack_into("<I", fat, 4, 0xFFFFFFFF)
    struct.pack_into("<I", fat, 8, 0x0FFFFFFF)
    struct.pack_into("<I", fat, 12, 0x0FFFFFFF)
    struct.pack_into("<I", fat, 16, 5)
    struct.pack_into("<I", fat, 20, 0x0FFFFFFF)
    for fat_index in range(FAT_COUNT):
        start = partition_offset + (
            FAT32_RESERVED_SECTORS + fat_index * fat_sectors
        ) * SECTOR_SIZE
        image[start : start + fat_bytes] = fat

    data_sector = FAT32_RESERVED_SECTORS + FAT_COUNT * fat_sectors
    root_offset = partition_offset + data_sector * SECTOR_SIZE
    put_dirent(image, root_offset, b"BOOT       ", 0x10, 3, 0)

    boot_dir_sector = data_sector + sectors_per_cluster
    boot_dir_offset = partition_offset + boot_dir_sector * SECTOR_SIZE
    put_dirent(image, boot_dir_offset, b".          ", 0x10, 3, 0)
    put_dirent(image, boot_dir_offset + 32, b"..         ", 0x10, 0, 0)
    put_dirent(
        image, boot_dir_offset + 64, b"ROOT    TXT", 0x21, 4, ROOT_FILE_SIZE
    )

    contents = bytearray(b"." * ROOT_FILE_SIZE)
    contents[:15] = b"REBSD FAT ROOT\n"
    contents[512:537] = b"REBSD FAT SECOND CLUSTER\n"
    file_sector = data_sector + 2 * sectors_per_cluster
    file_offset = partition_offset + file_sector * SECTOR_SIZE
    image[file_offset : file_offset + len(contents)] = contents


def build_image(sectors: int, filesystem: str = "fat16") -> bytes:
    if filesystem == "fat16":
        partition_start = FAT16_PARTITION_START
        partition_type = FAT16_PARTITION_TYPE
    elif filesystem == "fat32":
        partition_start = FAT32_PARTITION_START
        partition_type = FAT32_PARTITION_TYPE
    else:
        raise ValueError(f"unsupported filesystem {filesystem}")
    if sectors <= partition_start + 1 or sectors > 0x0FFFFFFF:
        raise ValueError("sector count must be in the ATA LBA28 range")
    image = bytearray(sectors * SECTOR_SIZE)
    image[: len(MARKER)] = MARKER
    image[8:24] = b"READ-ONLY-SMOKE\0"
    partition_sectors = sectors - partition_start
    entry = MBR_PARTITION_OFFSET
    image[entry] = 0x80
    image[entry + 1 : entry + 4] = b"\x00\x02\x00"
    image[entry + 4] = partition_type
    image[entry + 5 : entry + 8] = b"\xfe\xff\xff"
    struct.pack_into(
        "<II", image, entry + 8, partition_start, partition_sectors
    )
    image[510:512] = MBR_SIGNATURE
    if filesystem == "fat16":
        build_fat16(image, partition_start, partition_sectors)
    else:
        build_fat32(image, partition_start, partition_sectors)
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
    parser.add_argument(
        "--filesystem", choices=tuple(DEFAULT_SECTORS), default="fat16"
    )
    parser.add_argument("--sectors", type=int)
    args = parser.parse_args()
    sectors = (
        args.sectors
        if args.sectors is not None
        else DEFAULT_SECTORS[args.filesystem]
    )
    try:
        image = build_image(sectors, args.filesystem)
        write_atomic(args.output, image)
    except (OSError, ValueError) as error:
        raise SystemExit(f"mkide: {error}") from error
    print(
        f"mkide: {args.output} sectors={sectors} "
        f"bytes={len(image)} marker={MARKER.decode()} "
        f"filesystem={args.filesystem.upper()}"
    )


if __name__ == "__main__":
    main()
