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


def is_excluded(rel, prefixes):
    return any(rel == prefix or rel.startswith(prefix + "/")
               for prefix in prefixes)


def declared_modes(manifest):
    modes = {}
    current = None

    if manifest is None:
        return modes
    for raw_line in manifest.read_text(
        encoding="utf-8", errors="strict"
    ).splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        fields = line.split(maxsplit=1)
        command = fields[0]
        argument = fields[1] if len(fields) == 2 else ""
        if command in ("dir", "file", "link", "symlink"):
            current = argument
        elif command == "mode" and current is not None:
            modes[current] = int(argument, 8)
        elif command == "default":
            current = None
    return modes


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument(
        "--attributes",
        type=Path,
        help="preserve explicit modes from the canonical base manifest",
    )
    parser.add_argument("--devices", type=Path)
    parser.add_argument("--exclude", action="append", default=[],
                        help="exclude an absolute rootfs path and its contents")
    args = parser.parse_args()

    stage = args.stage.resolve()
    if not stage.is_dir():
        parser.error(f"stage is not a directory: {stage}")
    excludes = tuple("/" + item.strip("/") for item in args.exclude)
    modes = declared_modes(args.attributes)

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
        if is_excluded(rel, excludes):
            continue
        if path.is_symlink():
            lines.extend((f"symlink {rel}", f"target {os.readlink(path)}", ""))
        elif path.is_dir():
            mode = modes.get(rel, stat.S_IMODE(path.lstat().st_mode))
            lines.extend((f"dir {rel}", f"mode {mode:04o}", ""))
        elif path.is_file():
            mode = modes.get(rel, stat.S_IMODE(path.lstat().st_mode))
            lines.extend((f"file {rel}", f"mode {mode:04o}", ""))

    if args.devices:
        lines.append(args.devices.read_text(encoding="utf-8").rstrip())
        lines.append("")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
