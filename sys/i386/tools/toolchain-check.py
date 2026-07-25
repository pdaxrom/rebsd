#!/usr/bin/env python3
"""Check that the selected i686-elf GCC/binutils can build ReBSD objects."""

from __future__ import annotations

import argparse
import pathlib
import shutil
import subprocess


TOOLS = (
    "gcc",
    "ld",
    "ar",
    "ranlib",
    "size",
    "nm",
    "objcopy",
    "objdump",
    "readelf",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--prefix", required=True)
    parser.add_argument("--work-dir", required=True, type=pathlib.Path)
    return parser.parse_args()


def run(command: list[str], **kwargs: object) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        **kwargs,
    )


def main() -> None:
    args = parse_args()
    paths = {name: pathlib.Path(args.prefix + name) for name in TOOLS}
    missing = [str(path) for path in paths.values() if not path.is_file()]
    if missing:
        raise SystemExit("toolchain-check: missing tools: " + ", ".join(missing))

    target = run([str(paths["gcc"]), "-dumpmachine"]).stdout.strip()
    if target != "i686-elf":
        raise SystemExit(f"toolchain-check: target is {target}, expected i686-elf")

    if args.work_dir.exists():
        shutil.rmtree(args.work_dir)
    args.work_dir.mkdir(parents=True)
    source = args.work_dir / "smoke.c"
    obj = args.work_dir / "smoke.o"
    source.write_text(
        "void i686_toolchain_smoke(void) { __asm__ volatile (\"nop\"); }\n",
        encoding="ascii",
    )
    run(
        [
            str(paths["gcc"]),
            "-m32",
            "-march=i686",
            "-ffreestanding",
            "-fno-builtin",
            "-fno-stack-protector",
            "-fno-pic",
            "-fno-pie",
            "-mno-sse",
            "-mno-sse2",
            "-c",
            str(source),
            "-o",
            str(obj),
        ]
    )
    header = run([str(paths["readelf"]), "-h", str(obj)]).stdout
    required = ("Class:                             ELF32", "little endian",
                "Machine:                           Intel 80386")
    missing_header = [text for text in required if text not in header]
    if missing_header:
        raise SystemExit(
            "toolchain-check: unexpected ELF header: "
            + ", ".join(missing_header)
        )

    version = run([str(paths["gcc"]), "-dumpfullversion"]).stdout.strip()
    print(f"toolchain-check: ok target={target} gcc={version}")


if __name__ == "__main__":
    main()
