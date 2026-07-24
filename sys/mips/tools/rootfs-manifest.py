#!/usr/bin/env python3
"""Generate an fsutil manifest from an already staged root filesystem."""

import argparse
import os
import stat
from pathlib import Path


INTERNAL_STAMPS = {
    ".base",
    ".stamp",
    ".userland",
}


def is_internal(stage, path):
    name = path.name
    return (
        path.parent == stage
        and (
            name.startswith(".")
            or name in INTERNAL_STAMPS
            or name.startswith(".userland.")
        )
    )


def relative(stage, path):
    return "/" + path.relative_to(stage).as_posix()


def mode_string(path):
    return f"{stat.S_IMODE(path.lstat().st_mode):04o}"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--devices", type=Path)
    args = parser.parse_args()

    stage = args.stage.resolve()
    if not stage.is_dir():
        parser.error(f"stage is not a directory: {stage}")

    lines = [
        "#",
        "# Generated from the staged root filesystem; do not edit.",
        "#",
        "default",
        "owner 0",
        "group 0",
        "dirmode 0775",
        "filemode 0664",
        "",
    ]

    paths = sorted(stage.rglob("*"), key=lambda path: path.as_posix())
    for path in paths:
        if is_internal(stage, path):
            continue
        rel = relative(stage, path)
        if path.is_symlink():
            lines.extend((f"symlink {rel}", f"target {os.readlink(path)}", ""))
        elif path.is_dir():
            lines.extend((f"dir {rel}", f"mode {mode_string(path)}", ""))
        elif path.is_file():
            lines.extend((f"file {rel}", f"mode {mode_string(path)}", ""))

    if args.devices:
        lines.append(args.devices.read_text(encoding="utf-8").rstrip())
        lines.append("")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
