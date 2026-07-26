#!/usr/bin/env python3
"""Validate the boot contract of a staged ReBSD MIPS root filesystem."""

import argparse
import os
import stat
import sys
from pathlib import Path


REQUIRED_EXECUTABLES = (
    "sbin/init",
    "bin/sh",
    "libexec/getty",
    "sbin/mkfs",
    "sbin/mount",
)

REQUIRED_FILES = (
    "etc/rc",
    "etc/ttys",
    "etc/passwd",
    "etc/group",
    "etc/fstab",
)

REQUIRED_DEVICES = (
    ("bdev", "/dev/romdisk"),
    ("bdev", "/dev/swap"),
    ("bdev", "/dev/ram0"),
    ("cdev", "/dev/console"),
)

EXECUTABLE_DIRECTORIES = (
    "/bin/",
    "/sbin/",
    "/usr/bin/",
    "/usr/sbin/",
    "/libexec/",
    "/usr/libexec/",
)

COMMAND_MAN_DIRECTORIES = (
    "/usr/share/man/cat1/",
    "/usr/share/man/cat8/",
)

MANUAL_DIRECTORY = "/usr/share/man/"

# Command manuals are matched to an executable with the same basename.
# Manuals for formats, configuration files, ABIs, and other non-command
# objects need an explicit rootfs-object rule so that new manual sections
# cannot silently become unconditional payload.
MANUAL_OBJECTS = {
    "/usr/share/man/cat5/ar.0": (
        "/usr/bin/ar",
        "/usr/include/ar.h",
    ),
    "/usr/share/man/cat5/magic.0": (
        "/usr/share/misc/magic.mgc",
        "/usr/include/magic.h",
    ),
    "/usr/share/man/cat5/ranlib.0": (
        "/usr/bin/ranlib",
        "/usr/include/ranlib.h",
    ),
}


def fail(messages):
    for message in messages:
        print(f"rootfs contract: {message}", file=sys.stderr)
    return 1


def check_stage(stage):
    errors = []
    if not stage.is_dir():
        return [f"stage is not a directory: {stage}"]

    for relative in REQUIRED_EXECUTABLES:
        path = stage / relative
        try:
            mode = path.stat().st_mode
        except FileNotFoundError:
            errors.append(f"missing executable /{relative}")
            continue
        if not stat.S_ISREG(mode):
            errors.append(f"/{relative} is not a regular file")
        elif mode & 0o111 == 0:
            errors.append(f"/{relative} is not executable")
        elif path.stat().st_size == 0:
            errors.append(f"/{relative} is empty")

    for relative in REQUIRED_FILES:
        path = stage / relative
        try:
            mode = path.stat().st_mode
        except FileNotFoundError:
            errors.append(f"missing file /{relative}")
            continue
        if not stat.S_ISREG(mode):
            errors.append(f"/{relative} is not a regular file")
        elif path.stat().st_size == 0:
            errors.append(f"/{relative} is empty")

    tmp = stage / "tmp"
    if not tmp.is_symlink():
        errors.append("/tmp is not a symlink")
    elif os.readlink(tmp) != "var/tmp":
        errors.append(f"/tmp points to {os.readlink(tmp)!r}, expected 'var/tmp'")

    return errors


def check_manifest(stage, manifest):
    if not manifest.is_file():
        return [f"manifest is not a file: {manifest}"]
    text = manifest.read_text(encoding="utf-8", errors="replace")
    payload = {
        line.split(maxsplit=1)[1]
        for line in text.splitlines()
        if line.startswith(("file /", "symlink /"))
    }
    errors = []
    for kind, path in REQUIRED_DEVICES:
        if f"{kind} {path}" not in text:
            errors.append(f"manifest is missing {kind} {path}")

    commands = {}
    for path in sorted(payload):
        if not path.startswith(EXECUTABLE_DIRECTORIES):
            continue
        staged = stage / path.lstrip("/")
        try:
            mode = staged.stat().st_mode
        except FileNotFoundError:
            continue
        if stat.S_ISREG(mode) and mode & 0o111:
            commands.setdefault(staged.name, []).append(path)

    staged_pages = {
        f"/{path.relative_to(stage)}"
        for path in (stage / MANUAL_DIRECTORY.lstrip("/")).glob("cat[1-9]/*.0")
        if path.is_file()
    }
    packaged_pages = {
        path
        for path in payload
        if path.startswith(MANUAL_DIRECTORY) and path.endswith(".0")
    }

    for page in sorted(staged_pages):
        directory = f"{Path(page).parent}/"
        if directory in COMMAND_MAN_DIRECTORIES:
            command = Path(page).stem
            paths = commands.get(command, ())
            if paths and page not in payload:
                errors.append(
                    f"manifest is missing man page {page} for {', '.join(paths)}"
                )
            elif not paths and page in payload:
                errors.append(
                    f"manifest includes man page {page} without an installed command"
                )
            continue

        if page not in MANUAL_OBJECTS:
            errors.append(
                f"manual object rule is missing for non-command page {page}"
            )

    for page, objects in sorted(MANUAL_OBJECTS.items()):
        installed = [path for path in objects if path in payload]
        if installed and page not in payload:
            errors.append(
                f"manifest is missing man page {page} for {', '.join(installed)}"
            )
        elif not installed and page in payload:
            errors.append(
                f"manifest includes man page {page} without any described object: "
                f"{', '.join(objects)}"
            )

    for page in sorted(packaged_pages - staged_pages):
        errors.append(f"manifest includes unavailable man page {page}")
    return errors


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--label", default="rootfs")
    args = parser.parse_args()

    errors = check_stage(args.stage)
    errors.extend(check_manifest(args.stage, args.manifest))
    if errors:
        return fail(errors)

    files = sum(1 for path in args.stage.rglob("*") if path.is_file())
    directories = sum(1 for path in args.stage.rglob("*") if path.is_dir())
    symlinks = sum(1 for path in args.stage.rglob("*") if path.is_symlink())
    print(
        f"rootfs contract: {args.label}: ok "
        f"({directories} dirs, {files} files, {symlinks} symlinks)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
