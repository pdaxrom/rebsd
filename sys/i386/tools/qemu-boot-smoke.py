#!/usr/bin/env python3
"""Boot the early i686 image under QEMU and require serial markers."""

from __future__ import annotations

import argparse
import os
import pathlib
import select
import subprocess
import time


BOOT_MARKERS = (
    "REBSD_I686_BOOT",
    "cpu: i686",
    "boot: linux-x86-2.02",
    "memory-map: ok",
    "gdt: ok",
    "tss: ok",
    "console: com1,vga",
    "idt: ok",
    "exception-int3: ok",
    "memory-normalized: ok",
    "physical-allocator: ok",
    "paging: on",
    "cr0.wp: on",
    "kernel-text-ro: ok",
    "pmap-primitives: ok",
    "tlb-invlpg: ok",
    "vm-bootstrap-reserved: ok",
    "vm-page-selftest: ok",
    "pmap-public: ok",
    "vmspace-selftest: ok",
    "vmspace-page-fault: ok",
    "copyio-selftest: ok",
    "uarea-selftest: ok",
    "context-switch: ok",
    "fork-frame: ok",
    "ring3: ok",
    "syscall-int80: ok",
    "syscall-production: ok",
    "signal-frame: ok",
    "user-return: ok",
    "user-trap: ok",
    "process-bootstrap: ok",
    "process-table: ok",
    "process-user: ok",
    "proc0-context: ok",
    "initfs: ok",
    "elf32-user: ok",
    "user-stack: ok",
    "pic: ok",
    "pit: hz=100",
    "timer-ticks: ok",
    "HALT",
)

EXCEPTION_MARKERS = {
    "divide": (
        "REBSD_I686_BOOT",
        "idt: ok",
        "exception-int3: ok",
        "exception: vector=0x00000000 error=0x00000000",
        "PANIC: cpu exception",
    ),
    "gp": (
        "REBSD_I686_BOOT",
        "idt: ok",
        "exception-int3: ok",
        "exception: vector=0x0000000d error=",
        "PANIC: cpu exception",
    ),
    "page": (
        "REBSD_I686_BOOT",
        "memory-normalized: ok",
        "paging: on",
        "cr0.wp: on",
        "kernel-text-ro: ok",
        "pmap-primitives: ok",
        "tlb-invlpg: ok",
        "vm-bootstrap-reserved: ok",
        "vm-page-selftest: ok",
        "pmap-public: ok",
        "vmspace-selftest: ok",
        "vmspace-page-fault: ok",
        "copyio-selftest: ok",
        "uarea-selftest: ok",
        "context-switch: ok",
        "fork-frame: ok",
        "ring3: ok",
        "syscall-int80: ok",
        "syscall-production: ok",
        "signal-frame: ok",
        "user-return: ok",
        "user-trap: ok",
        "process-bootstrap: ok",
        "process-table: ok",
        "process-user: ok",
        "proc0-context: ok",
        "initfs: ok",
        "elf32-user: ok",
        "user-stack: ok",
        "exception: vector=0x0000000e error=0x00000003",
        " cr2=",
        "PANIC: cpu exception",
    ),
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", required=True)
    parser.add_argument("--machine", required=True)
    parser.add_argument("--cpu", required=True)
    parser.add_argument("--memory", default="64M")
    parser.add_argument("--kernel", required=True, type=pathlib.Path)
    parser.add_argument("--timeout", type=float, default=8.0)
    parser.add_argument("--expect-exception", choices=tuple(EXCEPTION_MARKERS))
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
    if args.expect_exception:
        command.extend(["-append", f"rebsd.trap={args.expect_exception}"])

    markers = (
        EXCEPTION_MARKERS[args.expect_exception]
        if args.expect_exception
        else BOOT_MARKERS
    )
    stop_marker = (
        b"PANIC: cpu exception\r\n"
        if args.expect_exception
        else b"HALT\r\n"
    )
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
                if stop_marker in output_bytes:
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
    missing = [marker for marker in markers if marker not in output]
    if missing:
        raise SystemExit(
            "qemu-boot-smoke: missing serial markers: " + ", ".join(missing)
        )
    print("qemu-boot-smoke: ok")


if __name__ == "__main__":
    main()
