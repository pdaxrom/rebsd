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


def clusters_for_size(size: int, cluster_bytes: int) -> int:
    return (size + cluster_bytes - 1) // cluster_bytes


def data_cluster_offset(
    partition_offset: int,
    data_sector: int,
    sectors_per_cluster: int,
    cluster: int,
) -> int:
    return partition_offset + (
        data_sector + (cluster - 2) * sectors_per_cluster
    ) * SECTOR_SIZE


def fat16_chain(
    fat: bytearray, first_cluster: int, cluster_count: int
) -> None:
    for index in range(cluster_count):
        cluster = first_cluster + index
        next_cluster = (
            0xFFFF if index + 1 == cluster_count else cluster + 1
        )
        struct.pack_into("<H", fat, cluster * 2, next_cluster)


def fat32_chain(
    fat: bytearray, first_cluster: int, cluster_count: int
) -> None:
    for index in range(cluster_count):
        cluster = first_cluster + index
        next_cluster = (
            0x0FFFFFFF if index + 1 == cluster_count else cluster + 1
        )
        struct.pack_into("<I", fat, cluster * 4, next_cluster)


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
    init_contents: bytes | None,
) -> None:
    sectors_per_cluster, fat_sectors, clusters = fat16_geometry(
        partition_sectors
    )
    partition_offset = partition_start * SECTOR_SIZE
    cluster_bytes = sectors_per_cluster * SECTOR_SIZE
    boot_cluster = 2
    root_file_cluster = 3
    root_file_clusters = clusters_for_size(ROOT_FILE_SIZE, cluster_bytes)
    sbin_cluster = root_file_cluster + root_file_clusters
    init_cluster = sbin_cluster + 1
    init_clusters = (
        clusters_for_size(len(init_contents), cluster_bytes)
        if init_contents is not None
        else 0
    )
    last_cluster = (
        init_cluster + init_clusters - 1
        if init_clusters != 0
        else root_file_cluster + root_file_clusters - 1
    )
    if last_cluster > clusters + 1:
        raise ValueError("FAT16 payload does not fit in the partition")
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
    fat16_chain(fat, boot_cluster, 1)
    fat16_chain(fat, root_file_cluster, root_file_clusters)
    if init_contents is not None:
        fat16_chain(fat, sbin_cluster, 1)
        if init_clusters != 0:
            fat16_chain(fat, init_cluster, init_clusters)
    for fat_index in range(FAT_COUNT):
        start = partition_offset + (
            FAT16_RESERVED_SECTORS + fat_index * fat_sectors
        ) * SECTOR_SIZE
        image[start : start + fat_bytes] = fat

    root_sector = FAT16_RESERVED_SECTORS + FAT_COUNT * fat_sectors
    root_offset = partition_offset + root_sector * SECTOR_SIZE
    put_dirent(
        image, root_offset, b"BOOT       ", 0x10, boot_cluster, 0
    )
    if init_contents is not None:
        put_dirent(
            image, root_offset + 32, b"SBIN       ", 0x10, sbin_cluster, 0
        )

    data_sector = root_sector + FAT16_ROOT_SECTORS
    boot_dir_offset = data_cluster_offset(
        partition_offset, data_sector, sectors_per_cluster, boot_cluster
    )
    put_dirent(
        image, boot_dir_offset, b".          ", 0x10, boot_cluster, 0
    )
    put_dirent(image, boot_dir_offset + 32, b"..         ", 0x10, 0, 0)
    put_dirent(
        image,
        boot_dir_offset + 64,
        b"ROOT    TXT",
        0x21,
        root_file_cluster,
        ROOT_FILE_SIZE,
    )

    contents = bytearray(b"." * ROOT_FILE_SIZE)
    contents[:15] = b"REBSD FAT ROOT\n"
    contents[512:537] = b"REBSD FAT SECOND CLUSTER\n"
    file_offset = data_cluster_offset(
        partition_offset,
        data_sector,
        sectors_per_cluster,
        root_file_cluster,
    )
    image[file_offset : file_offset + len(contents)] = contents
    if init_contents is not None:
        sbin_dir_offset = data_cluster_offset(
            partition_offset,
            data_sector,
            sectors_per_cluster,
            sbin_cluster,
        )
        put_dirent(
            image, sbin_dir_offset, b".          ", 0x10, sbin_cluster, 0
        )
        put_dirent(
            image, sbin_dir_offset + 32, b"..         ", 0x10, 0, 0
        )
        put_dirent(
            image,
            sbin_dir_offset + 64,
            b"INIT       ",
            0x21,
            init_cluster if init_clusters != 0 else 0,
            len(init_contents),
        )
        if init_clusters != 0:
            init_offset = data_cluster_offset(
                partition_offset,
                data_sector,
                sectors_per_cluster,
                init_cluster,
            )
            image[init_offset : init_offset + len(init_contents)] = (
                init_contents
            )


def build_fat32(
    image: bytearray,
    partition_start: int,
    partition_sectors: int,
    init_contents: bytes | None,
) -> None:
    sectors_per_cluster, fat_sectors, clusters = fat32_geometry(
        partition_sectors
    )
    partition_offset = partition_start * SECTOR_SIZE
    cluster_bytes = sectors_per_cluster * SECTOR_SIZE
    root_cluster = 2
    boot_cluster = 3
    root_file_cluster = 4
    root_file_clusters = clusters_for_size(ROOT_FILE_SIZE, cluster_bytes)
    sbin_cluster = root_file_cluster + root_file_clusters
    init_cluster = sbin_cluster + 1
    init_clusters = (
        clusters_for_size(len(init_contents), cluster_bytes)
        if init_contents is not None
        else 0
    )
    last_cluster = (
        init_cluster + init_clusters - 1
        if init_clusters != 0
        else root_file_cluster + root_file_clusters - 1
    )
    if last_cluster > clusters + 1:
        raise ValueError("FAT32 payload does not fit in the partition")
    used_clusters = 2 + root_file_clusters
    if init_contents is not None:
        used_clusters += 1 + init_clusters
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
    struct.pack_into("<I", boot_bytes, 44, root_cluster)
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
    struct.pack_into("<I", fsinfo, 488, clusters - used_clusters)
    struct.pack_into("<I", fsinfo, 492, last_cluster + 1)
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
    fat32_chain(fat, root_cluster, 1)
    fat32_chain(fat, boot_cluster, 1)
    fat32_chain(fat, root_file_cluster, root_file_clusters)
    if init_contents is not None:
        fat32_chain(fat, sbin_cluster, 1)
        if init_clusters != 0:
            fat32_chain(fat, init_cluster, init_clusters)
    for fat_index in range(FAT_COUNT):
        start = partition_offset + (
            FAT32_RESERVED_SECTORS + fat_index * fat_sectors
        ) * SECTOR_SIZE
        image[start : start + fat_bytes] = fat

    data_sector = FAT32_RESERVED_SECTORS + FAT_COUNT * fat_sectors
    root_offset = data_cluster_offset(
        partition_offset, data_sector, sectors_per_cluster, root_cluster
    )
    put_dirent(
        image, root_offset, b"BOOT       ", 0x10, boot_cluster, 0
    )
    if init_contents is not None:
        put_dirent(
            image, root_offset + 32, b"SBIN       ", 0x10, sbin_cluster, 0
        )

    boot_dir_offset = data_cluster_offset(
        partition_offset, data_sector, sectors_per_cluster, boot_cluster
    )
    put_dirent(
        image, boot_dir_offset, b".          ", 0x10, boot_cluster, 0
    )
    put_dirent(image, boot_dir_offset + 32, b"..         ", 0x10, 0, 0)
    put_dirent(
        image,
        boot_dir_offset + 64,
        b"ROOT    TXT",
        0x21,
        root_file_cluster,
        ROOT_FILE_SIZE,
    )

    contents = bytearray(b"." * ROOT_FILE_SIZE)
    contents[:15] = b"REBSD FAT ROOT\n"
    contents[512:537] = b"REBSD FAT SECOND CLUSTER\n"
    file_offset = data_cluster_offset(
        partition_offset,
        data_sector,
        sectors_per_cluster,
        root_file_cluster,
    )
    image[file_offset : file_offset + len(contents)] = contents
    if init_contents is not None:
        sbin_dir_offset = data_cluster_offset(
            partition_offset,
            data_sector,
            sectors_per_cluster,
            sbin_cluster,
        )
        put_dirent(
            image, sbin_dir_offset, b".          ", 0x10, sbin_cluster, 0
        )
        put_dirent(
            image,
            sbin_dir_offset + 32,
            b"..         ",
            0x10,
            0,
            0,
        )
        put_dirent(
            image,
            sbin_dir_offset + 64,
            b"INIT       ",
            0x21,
            init_cluster if init_clusters != 0 else 0,
            len(init_contents),
        )
        if init_clusters != 0:
            init_offset = data_cluster_offset(
                partition_offset,
                data_sector,
                sectors_per_cluster,
                init_cluster,
            )
            image[init_offset : init_offset + len(init_contents)] = (
                init_contents
            )


def build_image(
    sectors: int,
    filesystem: str = "fat16",
    init_contents: bytes | None = None,
) -> bytes:
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
        build_fat16(
            image, partition_start, partition_sectors, init_contents
        )
    else:
        build_fat32(
            image, partition_start, partition_sectors, init_contents
        )
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
    parser.add_argument(
        "--file",
        action="append",
        default=[],
        metavar="/sbin/init=HOST_PATH",
    )
    args = parser.parse_args()
    sectors = (
        args.sectors
        if args.sectors is not None
        else DEFAULT_SECTORS[args.filesystem]
    )
    try:
        init_contents = None
        for specification in args.file:
            image_path, separator, host_path = specification.partition("=")
            if separator == "" or image_path != "/sbin/init":
                raise ValueError(
                    "--file currently supports /sbin/init=HOST_PATH"
                )
            if init_contents is not None:
                raise ValueError("duplicate /sbin/init")
            init_contents = pathlib.Path(host_path).read_bytes()
        image = build_image(sectors, args.filesystem, init_contents)
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
