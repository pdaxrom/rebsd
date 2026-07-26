#!/usr/bin/env python3
"""Boot the normal i686 system and require a working login shell."""

from __future__ import annotations

import argparse
import os
import pathlib
import select
import subprocess
import time


CORE_MARKERS = (
    "REBSD_I686_BOOT",
    "cpu: i686",
    "boot: linux-x86-2.02",
    "memory-map: ok",
    "interrupts: idt,pic ready",
    "memory-normalized: ok",
    "paging: on",
    "syscall-production: ok",
    "ReBSD 0.1-Resurgence (I686_PC)",
    "vm page: self-test ok",
    "pmap: self-test ok",
    "disk: block layer ready",
    "usb0: core ready",
    "pci: mechanism=1",
    "root dev  = (0,0)",
    "swap dev  = none",
    "root size = 1024 kbytes",
    "ReBSD/i686 0.1-Resurgence (console)",
    "login:",
    "REBSD_I686_SHELL_OK",
)

IDE_DISK_MARKERS = (
    "ide-primary-master: ata",
    "ide-lba28: ok",
    "ide-backend-read: ok",
    "ide-lba0: ok",
    "ide-bounds: ok",
    "sd0: 2048 512-byte sectors (1024 KB), read-only",
)

USB_MASS_STORAGE_MARKERS = (
    "ehci0: pci-id=0x808624cd",
    "ehci0: EHCI version=100",
    "umass0: QEMU QEMU HARDDISK",
    "ehci0: port1 device attached speed=high",
    "ehci0: irq 11 enabled",
)

OHCI_KEYBOARD_MARKERS = (
    "ohci0: pci-id=0x106b003f",
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
    "ukbd0: boot keyboard, interrupt in 0x81, 8 bytes",
    "uhci0: port1 device attached speed=full",
)

UHCI_MASS_STORAGE_MARKERS = UHCI_MARKERS + (
    "umass0: QEMU QEMU HARDDISK",
    "uhci0: port1 device attached speed=full",
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
    parser.add_argument("--usb-disk", type=pathlib.Path)
    parser.add_argument("--ohci-keyboard", action="store_true")
    parser.add_argument("--uhci-keyboard", action="store_true")
    parser.add_argument("--uhci-disk", type=pathlib.Path)
    parser.add_argument("--expect-no-disk", action="store_true")
    parser.add_argument("--timeout", type=float, default=15.0)
    args = parser.parse_args()

    if args.expect_no_disk and args.disk is not None:
        parser.error("--expect-no-disk cannot be combined with --disk")
    if args.uhci_keyboard and args.uhci_disk is not None:
        parser.error("--uhci-keyboard and --uhci-disk use the same UHCI port")
    if args.usb_disk is not None and args.uhci_disk is not None:
        parser.error("only one USB mass-storage device is supported")
    return args


def qemu_command(args: argparse.Namespace) -> list[str]:
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
        command.extend(["-device", "usb-kbd,bus=uhci.0,port=1"])
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
    return command


def expected_markers(args: argparse.Namespace) -> tuple[str, ...]:
    markers = CORE_MARKERS + (
        "boot-loader: bios-int13"
        if args.bios_image is not None
        else "boot-loader: linux-protocol",
    )
    if args.disk is None:
        markers += ("ide-primary-master: none",)
    else:
        markers += IDE_DISK_MARKERS
    if args.usb_disk is not None:
        markers += USB_MASS_STORAGE_MARKERS
        markers += (
            (
                "sd1: 2048 512-byte sectors (1024 KB), removable"
                if args.disk is not None
                else "sd0: 2048 512-byte sectors (1024 KB), removable"
            ),
        )
    if args.ohci_keyboard:
        markers += OHCI_KEYBOARD_MARKERS
    if args.uhci_keyboard:
        markers += UHCI_KEYBOARD_MARKERS
    if args.uhci_disk is not None:
        markers += UHCI_MASS_STORAGE_MARKERS
        markers += (
            (
                "sd1: 2048 512-byte sectors (1024 KB), removable"
                if args.disk is not None
                else "sd0: 2048 512-byte sectors (1024 KB), removable"
            ),
        )
    return markers


def main() -> None:
    args = parse_args()
    process = subprocess.Popen(
        qemu_command(args),
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if process.stdin is None or process.stdout is None:
        raise SystemExit("qemu-boot-smoke: failed to open QEMU pipes")

    output_bytes = bytearray()
    login_sent = False
    shell_command_sent = False
    deadline = time.monotonic() + args.timeout
    while time.monotonic() < deadline:
        ready, _, _ = select.select([process.stdout], [], [], 0.1)
        if ready:
            chunk = os.read(process.stdout.fileno(), 4096)
            if chunk:
                output_bytes.extend(chunk)
        if not login_sent and b"login: " in output_bytes:
            process.stdin.write(b"root\n")
            process.stdin.flush()
            login_sent = True
        if (
            login_sent
            and not shell_command_sent
            and b"\r\n# " in output_bytes
        ):
            process.stdin.write(b"echo REBSD_I686_SHELL_OK\n")
            process.stdin.flush()
            shell_command_sent = True
        if shell_command_sent and b"\r\nREBSD_I686_SHELL_OK\r\n" in output_bytes:
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
    missing = [
        marker for marker in expected_markers(args) if marker not in output
    ]
    if missing:
        raise SystemExit(
            "qemu-boot-smoke: missing serial markers: " + ", ".join(missing)
        )
    print("qemu-boot-smoke: ok")


if __name__ == "__main__":
    main()
