#!/usr/bin/env python3
"""Boot the early i686 image under QEMU and require serial markers."""

from __future__ import annotations

import argparse
import os
import pathlib
import select
import subprocess
import time


REQUIRED_MARKERS = (
    "REBSD_I686_BOOT",
    "cpu: i686",
    "boot: linux-x86-2.02",
    "memory-map: ok",
    "gdt: ok",
    "console: com1,vga",
    "HALT",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", required=True)
    parser.add_argument("--machine", required=True)
    parser.add_argument("--cpu", required=True)
    parser.add_argument("--memory", default="64M")
    parser.add_argument("--kernel", required=True, type=pathlib.Path)
    parser.add_argument("--timeout", type=float, default=8.0)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    command = [
        args.qemu,
        "-machine",
        args.machine,
        "-cpu",
        args.cpu,
        "-accel",
        "tcg",
        "-m",
        args.memory,
        "-kernel",
        str(args.kernel),
        "-serial",
        "stdio",
        "-display",
        "none",
        "-monitor",
        "none",
        "-no-reboot",
        "-no-shutdown",
    ]
    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if process.stdout is None:
        raise SystemExit("qemu-boot-smoke: failed to capture QEMU output")

    output_bytes = bytearray()
    deadline = time.monotonic() + args.timeout
    while time.monotonic() < deadline:
        ready, _, _ = select.select([process.stdout], [], [], 0.1)
        if ready:
            chunk = os.read(process.stdout.fileno(), 4096)
            if chunk:
                output_bytes.extend(chunk)
                if b"HALT\r\n" in output_bytes:
                    break
        if process.poll() is not None:
            break

    if process.poll() is None:
        process.terminate()
        try:
            tail, _ = process.communicate(timeout=2.0)
        except subprocess.TimeoutExpired:
            process.kill()
            tail, _ = process.communicate()
    else:
        tail, _ = process.communicate()
    output_bytes.extend(tail)

    output = output_bytes.decode("utf-8", errors="replace")
    print(output, end="")
    missing = [marker for marker in REQUIRED_MARKERS if marker not in output]
    if missing:
        raise SystemExit(
            "qemu-boot-smoke: missing serial markers: " + ", ".join(missing)
        )
    print("qemu-boot-smoke: ok")


if __name__ == "__main__":
    main()
