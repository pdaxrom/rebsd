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
    "process-image: vfs",
    "syscall-open: ok",
    "syscall-read: ok",
    "syscall-lseek: ok",
    "syscall-close: ok",
    "fd-vfs: ok",
    "fd-fork-shared-offset: ok",
    "fd-exit-close: ok",
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
    "rootfs: ok",
    "elf32-user: ok",
    "user-stack: ok",
    "pci: mechanism=1",
    "pci-host: 0x80861237",
    "pci-isa: 0x80867000",
    "pci-ide: 0x80867010",
    "pci-platform: intel",
    "ide-primary-master: ata",
    "ide-sectors: 0x00000100",
    "ide-lba28: ok",
    "ide-backend-read: ok",
    "ide-lba0: ok",
    "ide-bounds: ok",
    "disk-attach: read-only",
    "disk-write-open: erofs",
    "disk-device: whole",
    "disk-strategy-read: ok",
    "disk-strategy-eof: ok",
    "disk-strategy-write: erofs",
    "vfs-root: ufs,romdisk,read-only",
    "vfs-exec-init: ok",
    "disk-close: ok",
    "pic: ok",
    "pit: hz=100",
    "timer-ticks: ok",
    "hardclock-ticks: ok",
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
    "ide-sectors: 0x00000100",
    "ide-lba28: ok",
    "ide-backend-read: ok",
    "ide-lba0: ok",
    "ide-bounds: ok",
    "disk-attach: read-only",
    "disk-write-open: erofs",
    "disk-device: whole",
    "disk-strategy-read: ok",
    "disk-strategy-eof: ok",
    "disk-strategy-write: erofs",
    "disk-close: ok",
)

NO_DISK_BOOT_MARKERS = tuple(
    marker for marker in BOOT_MARKERS if marker not in IDE_DISK_MARKERS
) + ("ide-primary-master: none",)

BIOS_NO_DISK_BOOT_MARKERS = tuple(
    "boot-loader: bios-int13"
    if marker == "boot-loader: linux-protocol"
    else marker
    for marker in NO_DISK_BOOT_MARKERS
)

USB_MASS_STORAGE_MARKERS = (
    "ehci0: pci-id=0x808624cd",
    "dma: i686 coherent pool",
    "usb0: initializing core",
    "usb0: core ready",
    "umass0: SCSI/Bulk-Only driver ready",
    "uhub0: external hub driver ready",
    "ehci0: EHCI version=100",
    "umass0: QEMU QEMU HARDDISK",
    "ehci0: port1 device attached speed=high",
    "ehci0: irq 11 enabled",
)

OHCI_KEYBOARD_MARKERS = (
    "ohci0: pci-id=0x106b003f",
    "ukbd0: HID boot-keyboard driver ready",
    "ohci0: OHCI revision=10",
    "ukbd0: boot keyboard, interrupt in 0x81, 8 bytes",
    "ohci0: port1 device attached speed=full",
    "ohci0: irq enabled line=",
)

UHCI_MARKERS = (
    "uhci0: pci-id=0x80867020",
    "uhci0: UHCI revision=10 ports=2 control/bulk/interrupt",
    "uhci0: irq ",
)

UHCI_KEYBOARD_MARKERS = UHCI_MARKERS + (
    "ukbd0: HID boot-keyboard driver ready",
    "ukbd0: boot keyboard, interrupt in 0x81, 8 bytes",
    "uhci0: port1 device attached speed=full",
)

UHCI_MASS_STORAGE_MARKERS = UHCI_MARKERS + (
    "umass0: SCSI/Bulk-Only driver ready",
    "umass0: QEMU QEMU HARDDISK",
    "uhci0: port1 device attached speed=full",
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
        "process-image: vfs",
        "syscall-open: ok",
        "syscall-read: ok",
        "syscall-lseek: ok",
        "syscall-close: ok",
        "fd-vfs: ok",
        "fd-fork-shared-offset: ok",
        "fd-exit-close: ok",
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
        "rootfs: ok",
        "elf32-user: ok",
        "user-stack: ok",
        "pci: mechanism=1",
        "pci-host: 0x80861237",
        "pci-isa: 0x80867000",
        "pci-ide: 0x80867010",
        "pci-platform: intel",
        "ide-primary-master: ata",
        "ide-sectors: 0x00000100",
        "ide-lba28: ok",
        "ide-backend-read: ok",
        "ide-lba0: ok",
        "ide-bounds: ok",
        "disk-attach: read-only",
        "disk-write-open: erofs",
        "disk-device: whole",
        "disk-strategy-read: ok",
        "disk-strategy-eof: ok",
        "disk-strategy-write: erofs",
        "vfs-root: ufs,romdisk,read-only",
        "vfs-exec-init: ok",
        "disk-close: ok",
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
    image = parser.add_mutually_exclusive_group(required=True)
    image.add_argument("--kernel", type=pathlib.Path)
    image.add_argument("--bios-image", type=pathlib.Path)
    parser.add_argument("--disk", type=pathlib.Path)
    parser.add_argument("--usb-disk", type=pathlib.Path)
    parser.add_argument("--ohci-keyboard", action="store_true")
    parser.add_argument("--uhci-keyboard", action="store_true")
    parser.add_argument("--uhci-disk", type=pathlib.Path)
    parser.add_argument("--timeout", type=float, default=8.0)
    parser.add_argument("--expect-exception", choices=tuple(EXCEPTION_MARKERS))
    parser.add_argument("--expect-no-disk", action="store_true")
    args = parser.parse_args()
    if args.expect_no_disk:
        if args.disk is not None or args.expect_exception is not None:
            parser.error("--expect-no-disk cannot be combined with disk/trap")
    elif args.disk is None:
        parser.error("--disk is required unless --expect-no-disk is used")
    if args.bios_image is not None and args.expect_exception is not None:
        parser.error("--bios-image cannot be combined with --expect-exception")
    if args.usb_disk is not None and args.expect_exception is not None:
        parser.error("--usb-disk cannot be combined with --expect-exception")
    if args.ohci_keyboard and args.expect_exception is not None:
        parser.error(
            "--ohci-keyboard cannot be combined with --expect-exception"
        )
    if args.uhci_keyboard and args.expect_exception is not None:
        parser.error(
            "--uhci-keyboard cannot be combined with --expect-exception"
        )
    if args.uhci_disk is not None and args.expect_exception is not None:
        parser.error("--uhci-disk cannot be combined with --expect-exception")
    if args.uhci_keyboard and args.uhci_disk is not None:
        parser.error("--uhci-keyboard and --uhci-disk use the same UHCI port")
    if args.usb_disk is not None and args.uhci_disk is not None:
        parser.error("only one USB mass-storage device is supported")
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
    if args.usb_disk is not None:
        command.extend(
            [
                "-device",
                "usb-ehci,id=ehci",
                "-drive",
                (
                    f"file={args.usb_disk},format=raw,if=none,"
                    "id=usbmass,snapshot=on"
                ),
                "-device",
                "usb-storage,bus=ehci.0,port=1,drive=usbmass",
            ]
        )
    if args.ohci_keyboard:
        command.extend(
            [
                "-device",
                "pci-ohci,id=ohci",
                "-device",
                "usb-kbd,bus=ohci.0,port=1",
            ]
        )
    if args.uhci_keyboard or args.uhci_disk is not None:
        command.extend(["-device", "piix3-usb-uhci,id=uhci"])
    if args.uhci_keyboard:
        command.extend(
            ["-device", "usb-kbd,bus=uhci.0,port=1"]
        )
    if args.uhci_disk is not None:
        command.extend(
            [
                "-drive",
                (
                    f"file={args.uhci_disk},format=raw,if=none,"
                    "id=uhcimass,snapshot=on"
                ),
                "-device",
                "usb-storage,bus=uhci.0,port=1,drive=uhcimass",
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
    elif args.bios_image is not None:
        markers = BIOS_BOOT_MARKERS
    else:
        markers = BOOT_MARKERS
    if args.usb_disk is not None:
        markers += USB_MASS_STORAGE_MARKERS
        markers += (
            "sd1: 256 512-byte sectors (128 KB), removable"
            if args.disk is not None
            else "sd0: 256 512-byte sectors (128 KB), removable",
        )
    if args.ohci_keyboard:
        markers += OHCI_KEYBOARD_MARKERS
    if args.uhci_keyboard:
        markers += UHCI_KEYBOARD_MARKERS
    if args.uhci_disk is not None:
        markers += UHCI_MASS_STORAGE_MARKERS
        markers += (
            "sd1: 256 512-byte sectors (128 KB), removable"
            if args.disk is not None
            else "sd0: 256 512-byte sectors (128 KB), removable",
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
