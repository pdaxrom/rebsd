#!/usr/bin/env python3
"""Build the deterministic read-only i386 early init filesystem."""

from __future__ import annotations

import argparse
import os
import pathlib
import struct
import tempfile


MAGIC = b"RIFS"
VERSION = 1
ALIGNMENT = 16
MAX_ENTRIES = 64
MAX_PATH = 255
HEADER = struct.Struct("<4sIII")
ENTRY = struct.Struct("<IIII")


def align(value: int) -> int:
    return (value + ALIGNMENT - 1) & ~(ALIGNMENT - 1)


def parse_spec(spec: str) -> tuple[str, pathlib.Path]:
    if "=" not in spec:
        raise ValueError(f"{spec!r}: expected /path=source")
    archive_path, source_text = spec.split("=", 1)
    source = pathlib.Path(source_text)
    components = archive_path.split("/")
    if (
        not archive_path.startswith("/")
        or archive_path == "/"
        or archive_path.endswith("/")
        or any(component in ("", ".", "..") for component in components[1:])
    ):
        raise ValueError(f"{archive_path!r}: path is not canonical")
    encoded = archive_path.encode("utf-8")
    if len(encoded) > MAX_PATH or b"\0" in encoded:
        raise ValueError(f"{archive_path!r}: path is too long or contains NUL")
    if not source.is_file():
        raise ValueError(f"{source}: source file not found")
    return archive_path, source


def build_image(specs: list[str]) -> tuple[bytes, list[tuple[str, bytes]]]:
    if not specs:
        raise ValueError("at least one --file is required")
    if len(specs) > MAX_ENTRIES:
        raise ValueError(f"too many entries (maximum {MAX_ENTRIES})")

    sources: dict[str, bytes] = {}
    for spec in specs:
        archive_path, source = parse_spec(spec)
        if archive_path in sources:
            raise ValueError(f"{archive_path!r}: duplicate path")
        sources[archive_path] = source.read_bytes()
    ordered = sorted(sources.items())

    cursor = HEADER.size + len(ordered) * ENTRY.size
    paths: list[tuple[int, bytes]] = []
    for archive_path, _ in ordered:
        encoded = archive_path.encode("utf-8") + b"\0"
        paths.append((cursor, encoded))
        cursor += len(encoded)

    cursor = align(cursor)
    data: list[tuple[int, bytes]] = []
    for _, contents in ordered:
        data.append((cursor, contents))
        cursor = align(cursor + len(contents))

    image = bytearray(cursor)
    HEADER.pack_into(image, 0, MAGIC, VERSION, len(ordered), len(image))
    for index, ((path_offset, encoded), (data_offset, contents)) in enumerate(
        zip(paths, data)
    ):
        ENTRY.pack_into(
            image,
            HEADER.size + index * ENTRY.size,
            path_offset,
            len(encoded),
            data_offset,
            len(contents),
        )
        image[path_offset : path_offset + len(encoded)] = encoded
        image[data_offset : data_offset + len(contents)] = contents
    return bytes(image), ordered


def verify_image(image: bytes, expected: list[tuple[str, bytes]]) -> None:
    magic, version, count, image_size = HEADER.unpack_from(image)
    if (magic, version, count, image_size) != (
        MAGIC,
        VERSION,
        len(expected),
        len(image),
    ):
        raise ValueError("internal header verification failed")
    for index, (expected_path, expected_data) in enumerate(expected):
        path_offset, path_length, data_offset, data_length = ENTRY.unpack_from(
            image, HEADER.size + index * ENTRY.size
        )
        path = image[path_offset : path_offset + path_length]
        contents = image[data_offset : data_offset + data_length]
        if (
            path != expected_path.encode("utf-8") + b"\0"
            or contents != expected_data
            or data_offset % ALIGNMENT != 0
        ):
            raise ValueError(f"internal verification failed for {expected_path}")


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
    parser.add_argument("--file", action="append", default=[])
    args = parser.parse_args()
    try:
        image, expected = build_image(args.file)
        verify_image(image, expected)
        write_atomic(args.output, image)
    except (OSError, UnicodeError, ValueError, struct.error) as error:
        raise SystemExit(f"mkinitfs: {error}") from error
    paths = ", ".join(path for path, _ in expected)
    print(f"mkinitfs: {args.output} size={len(image)} files={paths}")


if __name__ == "__main__":
    main()
