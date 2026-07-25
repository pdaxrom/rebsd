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
    "boot-loader: linux-protocol",
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
    "process-image: fat-vfs",
    "process-user: ok",
    "process-fork: ok",
    "syscall-fork: ok",
    "syscall-exit: ok",
    "syscall-wait4: ok",
    "wait4-nohang: ok",
    "wait4-zombie: ok",
    "wait4-efault: ok",
    "process-reap: ok",
    "proc0-context: ok",
    "scheduler-switch: ok",
    "initfs: ok",
    "elf32-user: ok",
    "user-stack: ok",
    "pci: mechanism=1",
    "pci-host: 0x80861237",
    "pci-isa: 0x80867000",
    "pci-ide: 0x80867010",
    "pci-platform: intel",
    "ide-primary-master: ata",
    "ide-sectors: 0x00002000",
    "ide-lba28: ok",
    "ide-backend-read: ok",
    "ide-lba0: ok",
    "ide-image: rebsd-smoke",
    "ide-mbr: present",
    "ide-mbr-table: ok",
    "ide-part0-type: 0x00000006",
    "ide-part0-start: 0x0000000000000040",
    "ide-part0-sectors: 0x0000000000001fc0",
    "ide-partition-read: rebsd-fat16",
    "ide-last-lba: rebsd-smoke",
    "ide-bounds: ok",
    "disk-attach: read-only",
    "disk-write-open: erofs",
    "disk-partition: fat16",
    "disk-strategy-read: rebsd-fat16",
    "disk-strategy-eof: ok",
    "disk-strategy-write: erofs",
    "fat-mount: fat16,read-only",
    "fat-root-lookup: ok",
    "fat-root-read: ok",
    "fat-root-chain: ok",
    "fat-root-eof: ok",
    "fat-root-missing: enoent",
    "fat-root-directory: eisdir",
    "vfs-root: fat,read-only",
    "vfs-namei-init: ok",
    "vfs-read-init: ok",
    "disk-close: ok",
    "pic: ok",
    "pit: hz=100",
    "timer-ticks: ok",
    "hardware-summary: pci",
    "hardware-pci-host: 0x80861237",
    "hardware-pci-isa: 0x80867000",
    "hardware-pci-ide: 0x80867010",
    "hardware-pci-vga: 0x12341111",
    "hardware-pci-platform: intel",
    "HALT",
)

BIOS_BOOT_MARKERS = tuple(
    "boot-loader: bios-int13"
    if marker == "boot-loader: linux-protocol"
    else marker
    for marker in BOOT_MARKERS
)

IDE_DISK_MARKERS = (
    "ide-primary-master: ata",
    "ide-sectors: 0x00002000",
    "ide-lba28: ok",
    "ide-backend-read: ok",
    "ide-lba0: ok",
    "ide-image: rebsd-smoke",
    "ide-mbr: present",
    "ide-mbr-table: ok",
    "ide-part0-type: 0x00000006",
    "ide-part0-start: 0x0000000000000040",
    "ide-part0-sectors: 0x0000000000001fc0",
    "ide-partition-read: rebsd-fat16",
    "ide-last-lba: rebsd-smoke",
    "ide-bounds: ok",
    "disk-attach: read-only",
    "disk-write-open: erofs",
    "disk-partition: fat16",
    "disk-strategy-read: rebsd-fat16",
    "disk-strategy-eof: ok",
    "disk-strategy-write: erofs",
    "fat-mount: fat16,read-only",
    "fat-root-lookup: ok",
    "fat-root-read: ok",
    "fat-root-chain: ok",
    "fat-root-eof: ok",
    "fat-root-missing: enoent",
    "fat-root-directory: eisdir",
    "vfs-root: fat,read-only",
    "vfs-namei-init: ok",
    "vfs-read-init: ok",
    "process-image: fat-vfs",
    "disk-close: ok",
)

VFS_INIT_MARKERS = (
    "vfs-namei-init: ok",
    "vfs-read-init: ok",
    "process-image: fat-vfs",
)

NO_DISK_BOOT_MARKERS = tuple(
    marker for marker in BOOT_MARKERS if marker not in IDE_DISK_MARKERS
) + ("process-image: initfs", "ide-primary-master: none")

BIOS_NO_DISK_BOOT_MARKERS = tuple(
    "boot-loader: bios-int13"
    if marker == "boot-loader: linux-protocol"
    else marker
    for marker in NO_DISK_BOOT_MARKERS
)

EXCEPTION_MARKERS = {
    "divide": (
        "REBSD_I686_BOOT",
        "boot-loader: linux-protocol",
        "idt: ok",
        "exception-int3: ok",
        "exception: vector=0x00000000 error=0x00000000",
        "PANIC: cpu exception",
    ),
    "gp": (
        "REBSD_I686_BOOT",
        "boot-loader: linux-protocol",
        "idt: ok",
        "exception-int3: ok",
        "exception: vector=0x0000000d error=",
        "PANIC: cpu exception",
    ),
    "page": (
        "REBSD_I686_BOOT",
        "boot-loader: linux-protocol",
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
        "process-image: fat-vfs",
        "process-user: ok",
        "process-fork: ok",
        "syscall-fork: ok",
        "syscall-exit: ok",
        "syscall-wait4: ok",
        "wait4-nohang: ok",
        "wait4-zombie: ok",
        "wait4-efault: ok",
        "process-reap: ok",
        "proc0-context: ok",
        "scheduler-switch: ok",
        "initfs: ok",
        "elf32-user: ok",
        "user-stack: ok",
        "pci: mechanism=1",
        "pci-host: 0x80861237",
        "pci-isa: 0x80867000",
        "pci-ide: 0x80867010",
        "pci-platform: intel",
        "ide-primary-master: ata",
        "ide-sectors: 0x00002000",
        "ide-lba28: ok",
        "ide-backend-read: ok",
        "ide-lba0: ok",
        "ide-image: rebsd-smoke",
        "ide-mbr: present",
        "ide-mbr-table: ok",
        "ide-part0-type: 0x00000006",
        "ide-part0-start: 0x0000000000000040",
        "ide-part0-sectors: 0x0000000000001fc0",
        "ide-partition-read: rebsd-fat16",
        "ide-last-lba: rebsd-smoke",
        "ide-bounds: ok",
        "disk-attach: read-only",
        "disk-write-open: erofs",
        "disk-partition: fat16",
        "disk-strategy-read: rebsd-fat16",
        "disk-strategy-eof: ok",
        "disk-strategy-write: erofs",
        "fat-mount: fat16,read-only",
        "fat-root-lookup: ok",
        "fat-root-read: ok",
        "fat-root-chain: ok",
        "fat-root-eof: ok",
        "fat-root-missing: enoent",
        "fat-root-directory: eisdir",
        "vfs-root: fat,read-only",
        "vfs-namei-init: ok",
        "vfs-read-init: ok",
        "disk-close: ok",
        "exception: vector=0x0000000e error=0x00000003",
        " cr2=",
        "PANIC: cpu exception",
    ),
}

FAT32_MARKER_REPLACEMENTS = {
    "ide-sectors: 0x00002000": "ide-sectors: 0x00020000",
    "ide-part0-type: 0x00000006": "ide-part0-type: 0x0000000c",
    "ide-part0-start: 0x0000000000000040":
        "ide-part0-start: 0x0000000000000800",
    "ide-part0-sectors: 0x0000000000001fc0":
        "ide-part0-sectors: 0x000000000001f800",
}


def filesystem_markers(
    markers: tuple[str, ...], filesystem: str
) -> tuple[str, ...]:
    if filesystem == "fat16":
        return markers
    return tuple(
        FAT32_MARKER_REPLACEMENTS.get(marker, marker).replace(
            "fat16", "fat32"
        )
        for marker in markers
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", required=True)
    parser.add_argument("--machine", required=True)
    parser.add_argument("--cpu", required=True)
    parser.add_argument("--memory", default="64M")
    image = parser.add_mutually_exclusive_group(required=True)
    image.add_argument("--kernel", type=pathlib.Path)
    image.add_argument("--bios-image", type=pathlib.Path)
    parser.add_argument("--disk", type=pathlib.Path)
    parser.add_argument(
        "--expect-filesystem", choices=("fat16", "fat32"), default="fat16"
    )
    parser.add_argument("--timeout", type=float, default=8.0)
    parser.add_argument("--expect-exception", choices=tuple(EXCEPTION_MARKERS))
    parser.add_argument("--expect-no-disk", action="store_true")
    parser.add_argument("--expect-no-init", action="store_true")
    args = parser.parse_args()
    if args.expect_no_disk:
        if (
            args.disk is not None
            or args.expect_exception is not None
            or args.expect_no_init
        ):
            parser.error("--expect-no-disk cannot be combined with disk/trap")
    elif args.disk is None:
        parser.error("--disk is required unless --expect-no-disk is used")
    if args.expect_no_init and args.expect_exception is not None:
        parser.error("--expect-no-init cannot be combined with a trap")
    if args.bios_image is not None and args.expect_exception is not None:
        parser.error("--bios-image cannot be combined with --expect-exception")
    return args


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
        "-serial",
        "stdio",
        "-display",
        "none",
        "-monitor",
        "none",
        "-no-reboot",
        "-no-shutdown",
    ]
    if args.kernel is not None:
        command.extend(["-kernel", str(args.kernel)])
    else:
        command.extend(
            [
                "-drive",
                (
                    f"file={args.bios_image},format=raw,if=floppy,index=0,"
                    "readonly=on"
                ),
                "-boot",
                "order=a",
            ]
        )
    if args.disk is not None:
        command.extend(
            [
                "-drive",
                (
                    f"file={args.disk},format=raw,if=ide,index=0,"
                    "media=disk,snapshot=on"
                ),
            ]
        )
    if args.expect_exception:
        command.extend(["-append", f"rebsd.trap={args.expect_exception}"])

    if args.expect_exception:
        markers = EXCEPTION_MARKERS[args.expect_exception]
    elif args.expect_no_disk:
        markers = (
            BIOS_NO_DISK_BOOT_MARKERS
            if args.bios_image is not None
            else NO_DISK_BOOT_MARKERS
        )
    elif args.expect_no_init:
        markers = tuple(
            marker
            for marker in (
                BIOS_BOOT_MARKERS
                if args.bios_image is not None
                else BOOT_MARKERS
            )
            if marker not in VFS_INIT_MARKERS
        ) + ("process-image: initfs",)
    elif args.bios_image is not None:
        markers = BIOS_BOOT_MARKERS
    else:
        markers = BOOT_MARKERS
    markers = filesystem_markers(markers, args.expect_filesystem)
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
    if args.expect_no_init:
        unexpected = [marker for marker in VFS_INIT_MARKERS if marker in output]
        if unexpected:
            raise SystemExit(
                "qemu-boot-smoke: unexpected serial markers: "
                + ", ".join(unexpected)
            )
    print("qemu-boot-smoke: ok")


if __name__ == "__main__":
    main()
