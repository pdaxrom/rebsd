#!/usr/bin/env python3
"""Stage ad-hoc files into a MIPS rootfs and extend its manifest."""

import argparse
import os
import shutil
import stat
from pathlib import Path


def clean_rel(path):
    rel = path.strip()
    if not rel:
        raise ValueError("empty patch file path")
    rel = rel.lstrip("/")
    parts = Path(rel).parts
    if any(part in ("", ".", "..") for part in parts):
        raise ValueError(f"unsafe patch file path: {path}")
    return Path(*parts)


def read_manifest_entries(text):
    dirs = set()
    files = set()
    for raw in text.splitlines():
        line = raw.strip()
        if line.startswith("dir "):
            dirs.add(line.split(None, 1)[1])
        elif line.startswith("file "):
            files.add(line.split(None, 1)[1])
    return dirs, files


def parent_dirs(path):
    current = Path("/")
    for part in path.parts[:-1]:
        current = current / part
        yield current.as_posix()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--rootfs", required=True)
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--manifest-out", required=True)
    parser.add_argument("--patch-dir", required=True)
    parser.add_argument("files", nargs="+")
    args = parser.parse_args()

    rootfs = Path(args.rootfs)
    patch_dir = Path(args.patch_dir)
    manifest = Path(args.manifest)
    manifest_out = Path(args.manifest_out)

    if not rootfs.is_dir():
        raise SystemExit(f"{rootfs}: rootfs stage directory not found")
    if not patch_dir.is_dir():
        raise SystemExit(f"{patch_dir}: patch directory not found")
    if not manifest.is_file():
        raise SystemExit(f"{manifest}: manifest not found")

    text = manifest.read_text()
    known_dirs, known_files = read_manifest_entries(text)
    appended = []

    for name in args.files:
        rel = clean_rel(name)
        src = patch_dir / rel
        dst = rootfs / rel
        if not src.is_file():
            raise SystemExit(f"{src}: patch file not found")

        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)

        manifest_path = "/" + rel.as_posix()
        for directory in parent_dirs(rel):
            if directory not in known_dirs:
                appended.append(f"dir {directory}\n")
                known_dirs.add(directory)

        if manifest_path not in known_files:
            mode = stat.S_IMODE(src.stat().st_mode)
            appended.append(f"file {manifest_path}\n")
            appended.append(f"mode {mode:04o}\n")
            known_files.add(manifest_path)

    with manifest_out.open("w") as out:
        out.write(text)
        if appended:
            out.write("\n# Ad-hoc rootfs patch files.\n")
            out.writelines(appended)

    print(manifest_out)


if __name__ == "__main__":
    main()
