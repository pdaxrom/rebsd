#!/usr/bin/env python3
"""Host-side destructive tests on a temporary sparse GPT disk image."""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import uuid
import zlib


SECTOR = 512
ENTRY_SIZE = 128
ENTRY_COUNT = 128
ENTRY_BYTES = ENTRY_SIZE * ENTRY_COUNT
IMAGE_BYTES = 3 * 1024**4
IMAGE_SECTORS = IMAGE_BYTES // SECTOR


def run(*args: str, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        args,
        check=check,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def read_at(path: Path, offset: int, length: int) -> bytes:
    with path.open("rb") as stream:
        stream.seek(offset)
        data = stream.read(length)
    assert len(data) == length
    return data


def parse_header(path: Path, lba: int) -> tuple[dict[str, int], bytes]:
    sector = bytearray(read_at(path, lba * SECTOR, SECTOR))
    assert sector[:8] == b"EFI PART"
    revision, header_size, stored_crc = struct.unpack_from("<III", sector, 8)
    assert revision == 0x00010000
    assert header_size == 92
    struct.pack_into("<I", sector, 16, 0)
    assert zlib.crc32(sector[:header_size]) & 0xFFFFFFFF == stored_crc
    values = struct.unpack_from("<QQQQ16sQIII", sector, 24)
    header = {
        "current": values[0],
        "alternate": values[1],
        "first": values[2],
        "last": values[3],
        "entries_lba": values[5],
        "entry_count": values[6],
        "entry_size": values[7],
        "entries_crc": values[8],
    }
    entries = read_at(path, header["entries_lba"] * SECTOR, ENTRY_BYTES)
    assert header["entry_count"] == ENTRY_COUNT
    assert header["entry_size"] == ENTRY_SIZE
    assert zlib.crc32(entries) & 0xFFFFFFFF == header["entries_crc"]
    return header, entries


def validate(
    path: Path, media_sectors: int = IMAGE_SECTORS
) -> tuple[dict[str, int], bytes]:
    pmbr = read_at(path, 0, SECTOR)
    assert pmbr[510:512] == b"\x55\xaa"
    assert pmbr[446 + 4] == 0xEE
    assert struct.unpack_from("<I", pmbr, 446 + 8)[0] == 1
    assert struct.unpack_from("<I", pmbr, 446 + 12)[0] == min(
        media_sectors - 1, 0xFFFFFFFF
    )

    primary, primary_entries = parse_header(path, 1)
    backup, backup_entries = parse_header(path, media_sectors - 1)
    assert primary["current"] == 1
    assert primary["alternate"] == media_sectors - 1
    assert primary["entries_lba"] == 2
    assert backup["current"] == media_sectors - 1
    assert backup["alternate"] == 1
    assert backup["entries_lba"] == media_sectors - 33
    assert primary["first"] == backup["first"] == 34
    assert primary["last"] == backup["last"] == media_sectors - 34
    assert primary_entries == backup_entries
    return primary, primary_entries


def metadata_digest(path: Path) -> str:
    digest = hashlib.sha256()
    digest.update(read_at(path, 0, 34 * SECTOR))
    digest.update(read_at(path, (IMAGE_SECTORS - 33) * SECTOR, 33 * SECTOR))
    return digest.hexdigest()


def file_digest(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while data := stream.read(1024 * 1024):
            digest.update(data)
    return digest.hexdigest()


def make_legacy_mbr(
    path: Path, media_sectors: int, partitions: list[tuple[int, int, int, int]]
) -> bytes:
    prefix = bytes((index * 37 + 11) & 0xFF for index in range(446))
    mbr = bytearray(SECTOR)
    mbr[:446] = prefix
    for index, (status, part_type, first, sectors) in enumerate(partitions):
        assert index < 4
        struct.pack_into(
            "<B3sB3sII", mbr, 446 + index * 16, status, b"\0" * 3,
            part_type, b"\0" * 3, first, sectors
        )
    mbr[510:512] = b"\x55\xaa"
    with path.open("wb") as stream:
        stream.truncate(media_sectors * SECTOR)
        stream.seek(0)
        stream.write(mbr)
    return prefix


def compile_tool(top: Path, output: Path) -> None:
    run(
        os.environ.get("CC", "cc"),
        "-std=c99",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-pedantic",
        "-DGPT_HOST",
        str(top / "src/cmd/gpt/gpt.c"),
        "-o",
        str(output),
    )


def main() -> None:
    mode = sys.argv[1] if len(sys.argv) > 1 else "test"
    if mode not in {"compile", "test"}:
        raise SystemExit("usage: smoke.py [compile|test]")
    top = Path(__file__).resolve().parents[3]
    with tempfile.TemporaryDirectory(prefix="rebsd-gpt-") as tmp_name:
        tmp = Path(tmp_name)
        tool = tmp / "gpt"
        compile_tool(top, tool)
        if mode == "compile":
            return

        image = tmp / "disk.img"
        with image.open("wb") as stream:
            stream.truncate(IMAGE_BYTES)

        run(str(tool), "-c", str(image))
        _, entries = validate(image)
        assert entries == bytes(ENTRY_BYTES)

        run(str(tool), "-a", "-s", "100000", "-l", "DATA",
            str(image), "1")
        _, entries = validate(image)
        first, last = struct.unpack_from("<QQ", entries, 32)
        assert first == 2048
        assert last == 102047
        label = entries[56:128].decode("utf-16le").rstrip("\0")
        assert label == "DATA"
        printed = run(str(tool), "-p", str(image)).stdout
        assert "6442450944 sectors" in printed
        assert "primary GPT" in printed
        assert "100000 DATA" in printed

        # Break only the primary header CRC and verify read-only fallback.
        with image.open("r+b") as stream:
            stream.seek(SECTOR + 16)
            byte = stream.read(1)
            stream.seek(SECTOR + 16)
            stream.write(bytes([byte[0] ^ 1]))
        printed = run(str(tool), "-p", str(image)).stdout
        assert "backup GPT" in printed

        repaired = run(str(tool), "-a", "-s", "4096", "-l", "SECOND",
            str(image), "2")
        assert "using backup GPT" in repaired.stderr
        _, entries = validate(image)
        second_first, second_last = struct.unpack_from(
            "<QQ", entries, ENTRY_SIZE + 32
        )
        assert second_first > 0x10000
        assert second_last - second_first + 1 == 4096

        before = metadata_digest(image)
        failed = run(
            str(tool), "-a", "-b", str(second_first), "-s", "1",
            str(image), "3", check=False
        )
        assert failed.returncode != 0
        assert metadata_digest(image) == before

        run(str(tool), "-d", str(image), "1")
        _, entries = validate(image)
        assert entries[:ENTRY_SIZE] == bytes(ENTRY_SIZE)
        assert entries[ENTRY_SIZE] != 0

        # Convert a legacy FAT MBR without moving or changing payload sectors.
        legacy = tmp / "legacy.img"
        legacy_sectors = 16384
        legacy_first = 63
        legacy_length = 16000
        prefix = make_legacy_mbr(
            legacy, legacy_sectors,
            [(0x80, 0x0C, legacy_first, legacy_length)]
        )
        first_marker = b"REBSD-MBR-FIRST" * 32
        last_marker = b"REBSD-MBR-LAST" * 32
        with legacy.open("r+b") as stream:
            stream.seek(legacy_first * SECTOR)
            stream.write(first_marker)
            stream.seek((legacy_first + legacy_length - 1) * SECTOR)
            stream.write(last_marker)
        before = file_digest(legacy)
        dry_run = run(str(tool), "-m", "-n", str(legacy))
        assert "no data written" in dry_run.stdout
        assert file_digest(legacy) == before
        migrated = run(str(tool), "-m", str(legacy))
        assert "payload sectors were not moved" in migrated.stdout
        _, entries = validate(legacy, legacy_sectors)
        assert read_at(legacy, 0, 446) == prefix
        assert entries[:16] == uuid.UUID(
            "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7"
        ).bytes_le
        first, last = struct.unpack_from("<QQ", entries, 32)
        assert first == legacy_first
        assert last == legacy_first + legacy_length - 1
        assert read_at(legacy, first * SECTOR, len(first_marker)) == first_marker
        assert read_at(legacy, last * SECTOR, len(last_marker)) == last_marker
        printed = run(str(tool), "-p", str(legacy)).stdout
        assert f"{legacy_first:20d}" in printed
        assert f"{legacy_length:20d}" in printed

        # Every unsafe migration must fail before changing any byte.
        unsafe_cases = [
            [(0, 0x0C, 20, 1000)],
            [(0, 0x0F, 2048, 1000)],
            [(0, 0xB7, 2048, 1000)],
            [(0, 0x0C, 2048, 1000), (0, 0x83, 2500, 1000)],
            [(0, 0x0C, legacy_sectors - 100, 100)],
        ]
        for case_number, partitions in enumerate(unsafe_cases):
            unsafe = tmp / f"unsafe-{case_number}.img"
            make_legacy_mbr(unsafe, legacy_sectors, partitions)
            before = file_digest(unsafe)
            failed = run(str(tool), "-m", str(unsafe), check=False)
            assert failed.returncode != 0
            assert file_digest(unsafe) == before
        print("gpt smoke: all tests passed")


if __name__ == "__main__":
    main()
