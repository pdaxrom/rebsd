#!/usr/bin/env python3
"""Host smoke tests for the compact ReBSD mkfs.fat."""

import os
import pathlib
import struct
import subprocess
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[3]
SECTOR = 512


def build_tools(work):
    mkfs = work / "mkfs.fat"
    fsck = work / "fsck.fat"
    common = ["-std=c89", "-Wall", "-Wextra", "-Werror", "-pedantic"]
    subprocess.run(
        [
            os.environ.get("CC", "cc"),
            *common,
            "-DMKFS_FAT_HOST",
            str(ROOT / "src/cmd/mkfs.fat/main.c"),
            "-o",
            str(mkfs),
        ],
        check=True,
    )
    subprocess.run(
        [
            os.environ.get("CC", "cc"),
            *common,
            "-I",
            str(ROOT / "sys"),
            str(ROOT / "src/cmd/fsck.fat/main.c"),
            str(ROOT / "sys/fs/fat/fat_subr.c"),
            "-o",
            str(fsck),
        ],
        check=True,
    )
    return mkfs, fsck


def image(path, size):
    with path.open("wb") as output:
        output.truncate(size)


def run(expected, *arguments):
    result = subprocess.run(
        list(map(str, arguments)),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    print(result.stdout, end="")
    if result.returncode != expected:
        raise RuntimeError(
            f"{' '.join(map(str, arguments))}: expected {expected}, "
            f"got {result.returncode}"
        )


def read_sector(path, sector):
    with path.open("rb") as source:
        source.seek(sector * SECTOR)
        data = source.read(SECTOR)
    if len(data) != SECTOR:
        raise RuntimeError(f"short sector {sector} in {path}")
    return data


def le16(data, offset):
    return struct.unpack_from("<H", data, offset)[0]


def le32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def verify_fat16(path, label):
    boot = read_sector(path, 0)
    assert boot[510:512] == b"\x55\xaa"
    assert le16(boot, 22) != 0
    assert boot[43:54] == label
    root_sector = le16(boot, 14) + boot[16] * le16(boot, 22)
    root = read_sector(path, root_sector)
    assert root[0:11] == label
    assert root[11] == 0x08


def verify_fat32(path, label):
    boot = read_sector(path, 0)
    assert boot[510:512] == b"\x55\xaa"
    assert le16(boot, 22) == 0
    assert le32(boot, 36) != 0
    assert boot[71:82] == label
    assert le32(boot, 44) == 2
    assert read_sector(path, 6) == boot
    fsinfo = read_sector(path, 1)
    assert le32(fsinfo, 0) == 0x41615252
    assert le32(fsinfo, 492) == 3
    assert read_sector(path, 7) == fsinfo
    root_sector = le16(boot, 14) + boot[16] * le32(boot, 36)
    root = read_sector(path, root_sector)
    assert root[0:11] == label
    assert root[11] == 0x08


def main():
    with tempfile.TemporaryDirectory(prefix="rebsd-mkfs-fat-") as temporary:
        work = pathlib.Path(temporary)
        mkfs, fsck = build_tools(work)

        fat16 = work / "fat16.img"
        image(fat16, 64 * 1024 * 1024)
        run(0, mkfs, "-i", "0x12345678", "-n", "rebsd16", fat16)
        verify_fat16(fat16, b"REBSD16    ")
        run(0, fsck, "-n", fat16)

        target_smoke = work / "target-smoke.img"
        image(target_smoke, 4 * 1024 * 1024)
        run(0, mkfs, "-F", "16", "-n", "REBSDTEST", target_smoke)
        verify_fat16(target_smoke, b"REBSDTEST  ")
        run(0, fsck, "-n", target_smoke)

        fat32 = work / "fat32.img"
        image(fat32, 64 * 1024 * 1024)
        run(0, mkfs, "-F", "32", "-n", "rebsd32", fat32)
        verify_fat32(fat32, b"REBSD32    ")
        run(0, fsck, "-n", fat32)

        explicit = work / "explicit.img"
        image(explicit, 64 * 1024 * 1024)
        run(0, mkfs, "-F", "16", "-s", "8", explicit)
        assert read_sector(explicit, 0)[13] == 8
        run(0, fsck, "-n", explicit)

        dry_run = work / "dry-run.img"
        image(dry_run, 64 * 1024 * 1024)
        run(0, mkfs, "-N", dry_run)
        assert read_sector(dry_run, 0) == bytes(SECTOR)

        small = work / "small.img"
        image(small, 1024 * 1024)
        run(1, mkfs, "-F", "16", small)
        run(1, mkfs, "-n", "BAD/NAME", fat16)

    print("mkfs.fat smoke: all tests passed")


if __name__ == "__main__":
    main()
