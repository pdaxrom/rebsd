#!/usr/bin/env python3
"""Host smoke tests for the compact ReBSD fsck.fat."""

import os
import pathlib
import shutil
import struct
import subprocess
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[3]
SECTOR = 512


def put16(data, offset, value):
    struct.pack_into("<H", data, offset, value)


def put32(data, offset, value):
    struct.pack_into("<I", data, offset, value)


def write_at(path, offset, data):
    with path.open("r+b") as image:
        image.seek(offset)
        image.write(data)


def read32(path, offset):
    with path.open("rb") as image:
        image.seek(offset)
        return struct.unpack("<I", image.read(4))[0]


def make_fat16(path):
    total = 32768
    reserved = 1
    fats = 2
    fat_sectors = 32
    root_entries = 512
    sectors_per_cluster = 4
    path.touch()
    with path.open("r+b") as image:
        image.truncate(total * SECTOR)
    boot = bytearray(SECTOR)
    boot[0:3] = b"\xeb\x3c\x90"
    boot[3:11] = b"REBSD   "
    put16(boot, 11, SECTOR)
    boot[13] = sectors_per_cluster
    put16(boot, 14, reserved)
    boot[16] = fats
    put16(boot, 17, root_entries)
    put16(boot, 19, total)
    boot[21] = 0xF8
    put16(boot, 22, fat_sectors)
    boot[510:512] = b"\x55\xaa"
    write_at(path, 0, boot)
    fat = bytearray(fat_sectors * SECTOR)
    put16(fat, 0, 0xFFF8)
    put16(fat, 2, 0xFFFF)
    for index in range(fats):
        write_at(path, (reserved + index * fat_sectors) * SECTOR, fat)
    return {
        "reserved": reserved,
        "fat_sectors": fat_sectors,
        "root_sector": reserved + fats * fat_sectors,
    }


def make_fat32(path):
    total = 131072
    reserved = 32
    fats = 2
    fat_sectors = 1009
    sectors_per_cluster = 1
    clusters = total - reserved - fats * fat_sectors
    path.touch()
    with path.open("r+b") as image:
        image.truncate(total * SECTOR)
    boot = bytearray(SECTOR)
    boot[0:3] = b"\xeb\x58\x90"
    boot[3:11] = b"REBSD   "
    put16(boot, 11, SECTOR)
    boot[13] = sectors_per_cluster
    put16(boot, 14, reserved)
    boot[16] = fats
    boot[21] = 0xF8
    put32(boot, 32, total)
    put32(boot, 36, fat_sectors)
    put32(boot, 44, 2)
    put16(boot, 48, 1)
    put16(boot, 50, 6)
    boot[510:512] = b"\x55\xaa"
    write_at(path, 0, boot)
    write_at(path, 6 * SECTOR, boot)

    fsinfo = bytearray(SECTOR)
    put32(fsinfo, 0, 0x41615252)
    put32(fsinfo, 484, 0x61417272)
    put32(fsinfo, 488, clusters - 1)
    put32(fsinfo, 492, 3)
    put32(fsinfo, 508, 0xAA550000)
    write_at(path, SECTOR, fsinfo)
    write_at(path, 7 * SECTOR, fsinfo)

    fat = bytearray(fat_sectors * SECTOR)
    put32(fat, 0, 0x0FFFFFF8)
    put32(fat, 4, 0x0FFFFFFF)
    put32(fat, 8, 0x0FFFFFFF)
    for index in range(fats):
        write_at(path, (reserved + index * fat_sectors) * SECTOR, fat)
    return {
        "total": total,
        "reserved": reserved,
        "fat_sectors": fat_sectors,
        "clusters": clusters,
        "data_sector": reserved + fats * fat_sectors,
        "max_cluster": clusters + 1,
    }


def set_fat16(path, geometry, cluster, value, copies=(0, 1)):
    for index in copies:
        offset = (
            geometry["reserved"] + index * geometry["fat_sectors"]
        ) * SECTOR + cluster * 2
        write_at(path, offset, struct.pack("<H", value))


def set_fat32(path, geometry, cluster, value, copies=(0, 1)):
    for index in copies:
        offset = (
            geometry["reserved"] + index * geometry["fat_sectors"]
        ) * SECTOR + cluster * 4
        write_at(path, offset, struct.pack("<I", value))


def set_fsinfo32(path, free_clusters, next_free):
    for sector in (1, 7):
        write_at(
            path,
            sector * SECTOR + 488,
            struct.pack("<II", free_clusters, next_free),
        )


def directory_entry(name, cluster):
    entry = bytearray(32)
    entry[0:11] = name
    entry[11] = 0x10
    put16(entry, 20, cluster >> 16)
    put16(entry, 26, cluster & 0xFFFF)
    return entry


def make_multicluster_parent32(path, geometry):
    root = bytearray(SECTOR)
    parent_first = bytearray(SECTOR)
    parent_second = bytearray(SECTOR)
    child = bytearray(SECTOR)

    root[0:32] = directory_entry(b"PARENT     ", 4)
    parent_first[0:32] = directory_entry(b".          ", 4)
    parent_first[32:64] = directory_entry(b"..         ", 0)
    for offset in range(64, SECTOR, 32):
        parent_first[offset] = 0xE5
    parent_second[0:32] = directory_entry(b"CHILD      ", 6)
    child[0:32] = directory_entry(b".          ", 6)
    child[32:64] = directory_entry(b"..         ", 4)

    set_fat32(path, geometry, 4, 5)
    set_fat32(path, geometry, 5, 0x0FFFFFFF)
    set_fat32(path, geometry, 6, 0x0FFFFFFF)
    write_at(path, geometry["data_sector"] * SECTOR, root)
    write_at(path, (geometry["data_sector"] + 2) * SECTOR, parent_first)
    write_at(path, (geometry["data_sector"] + 3) * SECTOR, parent_second)
    write_at(path, (geometry["data_sector"] + 4) * SECTOR, child)
    set_fsinfo32(path, geometry["clusters"] - 4, 3)


def set_root_file16(
    path, geometry, size, cluster=5, slot=0, name=b"TEST    BIN"
):
    entry = bytearray(32)
    entry[0:11] = name
    entry[11] = 0x20
    put16(entry, 26, cluster)
    put32(entry, 28, size)
    write_at(path, geometry["root_sector"] * SECTOR + slot * 32, entry)


def set_orphan_lfn16(path, geometry):
    entry = bytearray(32)
    entry[0] = 0x41
    entry[11] = 0x0F
    entry[13] = 0x5A
    write_at(path, geometry["root_sector"] * SECTOR, entry)


def build_checker(work):
    checker = work / "fsck.fat"
    command = [
        os.environ.get("CC", "cc"),
        "-std=c89",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-pedantic",
        "-I",
        str(ROOT / "sys"),
        str(ROOT / "src/cmd/fsck.fat/main.c"),
        str(ROOT / "sys/fs/fat/fat_subr.c"),
        "-o",
        str(checker),
    ]
    subprocess.run(command, check=True)
    return checker


def run(checker, expected, *arguments):
    result = subprocess.run(
        [str(checker), *map(str, arguments)],
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
    return result.stdout


def main():
    with tempfile.TemporaryDirectory(prefix="rebsd-fsck-fat-") as temporary:
        work = pathlib.Path(temporary)
        checker = build_checker(work)

        clean16 = work / "clean16.img"
        geometry16 = make_fat16(clean16)
        run(checker, 0, "-n", clean16)

        mirror16 = work / "mirror16.img"
        shutil.copyfile(clean16, mirror16)
        set_fat16(mirror16, geometry16, 5, 0xFFFF, copies=(1,))
        run(checker, 4, "-n", mirror16)
        run(checker, 4, "-p", mirror16)
        run(checker, 1, "-y", mirror16)
        run(checker, 0, "-n", mirror16)

        lost16 = work / "lost16.img"
        shutil.copyfile(clean16, lost16)
        set_fat16(lost16, geometry16, 5, 0xFFFF)
        run(checker, 4, "-n", lost16)
        run(checker, 1, "-y", lost16)
        run(checker, 0, "-n", lost16)

        short16 = work / "short16.img"
        shutil.copyfile(clean16, short16)
        set_fat16(short16, geometry16, 5, 0xFFFF)
        set_root_file16(short16, geometry16, 4096)
        run(checker, 4, "-n", short16)
        run(checker, 1, "-y", short16)
        run(checker, 0, "-n", short16)

        invalid16 = work / "invalid16.img"
        shutil.copyfile(clean16, invalid16)
        set_fat16(invalid16, geometry16, 5, 1)
        set_root_file16(invalid16, geometry16, 2048)
        run(checker, 4, "-n", invalid16)
        run(checker, 1, "-y", invalid16)
        run(checker, 0, "-n", invalid16)

        loop16 = work / "loop16.img"
        shutil.copyfile(clean16, loop16)
        set_fat16(loop16, geometry16, 5, 6)
        set_fat16(loop16, geometry16, 6, 5)
        set_root_file16(loop16, geometry16, 4096)
        run(checker, 4, "-n", loop16)
        run(checker, 1, "-y", loop16)
        run(checker, 0, "-n", loop16)

        cross16 = work / "cross16.img"
        shutil.copyfile(clean16, cross16)
        set_fat16(cross16, geometry16, 5, 0xFFFF)
        set_root_file16(cross16, geometry16, 2048)
        set_root_file16(
            cross16,
            geometry16,
            2048,
            slot=1,
            name=b"OTHER   BIN",
        )
        run(checker, 4, "-n", cross16)
        run(checker, 1, "-y", cross16)
        run(checker, 0, "-n", cross16)

        orphan_lfn16 = work / "orphan-lfn16.img"
        shutil.copyfile(clean16, orphan_lfn16)
        set_orphan_lfn16(orphan_lfn16, geometry16)
        run(checker, 4, "-n", orphan_lfn16)
        run(checker, 1, "-y", orphan_lfn16)
        run(checker, 0, "-n", orphan_lfn16)

        clean32 = work / "clean32.img"
        geometry32 = make_fat32(clean32)
        run(checker, 0, "-n", clean32)

        parent32 = work / "multicluster-parent32.img"
        shutil.copyfile(clean32, parent32)
        make_multicluster_parent32(parent32, geometry32)
        output = run(checker, 0, "-n", parent32)
        if "entry is inconsistent" in output:
            raise RuntimeError(
                "correct '..' entry in a multicluster parent was rejected"
            )

        short_geometry32 = work / "short-geometry32.img"
        shutil.copyfile(clean32, short_geometry32)
        short_sectors = geometry32["total"] - 64
        with short_geometry32.open("r+b") as image:
            image.truncate(short_sectors * SECTOR)
        output = run(checker, 4, "-n", short_geometry32)
        if "64 tail clusters are free" not in output:
            raise RuntimeError("safe FAT32 tail was not audited")
        run(checker, 4, "-p", short_geometry32)
        if read32(short_geometry32, 32) != geometry32["total"]:
            raise RuntimeError("preen mode changed FAT32 geometry")
        run(checker, 1, "-y", short_geometry32)
        if read32(short_geometry32, 32) != short_sectors:
            raise RuntimeError("primary FAT32 geometry was not repaired")
        if read32(short_geometry32, 6 * SECTOR + 32) != short_sectors:
            raise RuntimeError("backup FAT32 geometry was not repaired")
        run(checker, 0, "-n", short_geometry32)

        undersized_fat32 = work / "undersized-fat32.img"
        shutil.copyfile(clean32, undersized_fat32)
        oversized_total = 300000
        write_at(undersized_fat32, 32, struct.pack("<I", oversized_total))
        write_at(
            undersized_fat32,
            6 * SECTOR + 32,
            struct.pack("<I", oversized_total),
        )
        output = run(checker, 4, "-n", undersized_fat32)
        if "declared tail clusters have no FAT entries" not in output:
            raise RuntimeError("undersized FAT was not audited safely")
        run(checker, 1, "-y", undersized_fat32)
        if read32(undersized_fat32, 32) != geometry32["total"]:
            raise RuntimeError("undersized FAT32 geometry was not repaired")
        run(checker, 0, "-n", undersized_fat32)

        allocated_tail32 = work / "allocated-tail32.img"
        shutil.copyfile(clean32, allocated_tail32)
        set_fat32(
            allocated_tail32,
            geometry32,
            geometry32["max_cluster"],
            0x0FFFFFFF,
        )
        with allocated_tail32.open("r+b") as image:
            image.truncate(short_sectors * SECTOR)
        output = run(checker, 4, "-y", allocated_tail32)
        if "1 allocated tail cluster" not in output:
            raise RuntimeError("allocated FAT32 tail was not rejected")
        if read32(allocated_tail32, 32) != geometry32["total"]:
            raise RuntimeError("unsafe FAT32 geometry was modified")

        boundary_link32 = work / "boundary-link32.img"
        shutil.copyfile(clean32, boundary_link32)
        set_fat32(
            boundary_link32,
            geometry32,
            5,
            geometry32["max_cluster"],
        )
        with boundary_link32.open("r+b") as image:
            image.truncate(short_sectors * SECTOR)
        output = run(checker, 4, "-y", boundary_link32)
        if "1 link crosses the device boundary" not in output:
            raise RuntimeError("cross-boundary FAT32 link was not rejected")
        if read32(boundary_link32, 32) != geometry32["total"]:
            raise RuntimeError("linked FAT32 geometry was modified")

        fsinfo32 = work / "fsinfo32.img"
        shutil.copyfile(clean32, fsinfo32)
        write_at(fsinfo32, SECTOR + 488, struct.pack("<I", 7))
        run(checker, 4, "-n", fsinfo32)
        run(checker, 1, "-y", fsinfo32)
        run(checker, 0, "-n", fsinfo32)

        next32 = work / "next32.img"
        shutil.copyfile(clean32, next32)
        write_at(next32, SECTOR + 492, struct.pack("<I", 2))
        write_at(next32, 7 * SECTOR + 492, struct.pack("<I", 2))
        run(checker, 4, "-n", next32)
        run(checker, 1, "-y", next32)
        run(checker, 0, "-n", next32)

        backup_fsinfo32 = work / "backup-fsinfo32.img"
        shutil.copyfile(clean32, backup_fsinfo32)
        write_at(backup_fsinfo32, 7 * SECTOR + 488, struct.pack("<I", 7))
        run(checker, 0, "-n", backup_fsinfo32)

        backup32 = work / "backup32.img"
        shutil.copyfile(clean32, backup32)
        write_at(backup32, 6 * SECTOR + 71, b"X")
        run(checker, 4, "-n", backup32)
        run(checker, 1, "-y", backup32)
        run(checker, 0, "-n", backup32)

        lost32 = work / "lost32.img"
        shutil.copyfile(clean32, lost32)
        set_fat32(lost32, geometry32, 5, 0x0FFFFFFF)
        run(checker, 4, "-n", lost32)
        run(checker, 1, "-y", lost32)
        run(checker, 0, "-n", lost32)

    print("fsck.fat smoke: all tests passed")


if __name__ == "__main__":
    main()
