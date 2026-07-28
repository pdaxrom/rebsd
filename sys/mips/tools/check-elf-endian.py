#!/usr/bin/env python3
"""Reject target ELF files whose byte order does not match the build ABI."""

import argparse
from pathlib import Path


ELF_DATA = {
    "little": 1,
    "big": 2,
}


def collect_files(files, trees):
    paths = [Path(name) for name in files]
    for tree_name in trees:
        tree = Path(tree_name)
        if not tree.is_dir():
            raise SystemExit(f"ELF endian check: missing tree: {tree}")
        paths.extend(sorted(tree.rglob("*.o")))
    return paths


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--endian", choices=sorted(ELF_DATA), required=True)
    parser.add_argument("--file", action="append", default=[])
    parser.add_argument("--tree", action="append", default=[])
    args = parser.parse_args()

    paths = collect_files(args.file, args.tree)
    if not paths:
        raise SystemExit("ELF endian check: no input files")

    expected = ELF_DATA[args.endian]
    checked = 0
    failures = []
    for path in paths:
        try:
            header = path.read_bytes()[:6]
        except OSError as error:
            failures.append(f"{path}: {error}")
            continue
        if len(header) < 6 or header[:4] != b"\x7fELF":
            failures.append(f"{path}: not an ELF file")
            continue
        checked += 1
        if header[5] != expected:
            actual = "little" if header[5] == 1 else (
                "big" if header[5] == 2 else f"unknown({header[5]})"
            )
            failures.append(
                f"{path}: expected {args.endian}-endian, found {actual}-endian"
            )

    if failures:
        for failure in failures:
            print(f"ELF endian check: {failure}")
        raise SystemExit(1)
    print(f"ELF endian check: {args.endian}: ok ({checked} files)")


if __name__ == "__main__":
    main()
