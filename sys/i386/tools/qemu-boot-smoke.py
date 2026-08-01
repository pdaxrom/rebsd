#!/usr/bin/env python3
"""Boot the normal i686 system and require a working login shell."""

from __future__ import annotations

import argparse
import os
import pathlib
import re
import select
import shutil
import socket
import subprocess
import tempfile
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
    "todr: mc146818 registered priority 200",
    "todr: mc146818 20",
    "disk: block layer ready",
    "mouse0: source psm0",
    "i8042: controller ready, keyboard=present mouse=present",
    "pckbd0: PS/2 keyboard, set 2 with controller translation, irq 1",
    "psm0: PS/2 mouse, 3-byte packets, irq 12 as mouse0",
    "usb0: core ready",
    "pci: mechanism=1",
    "root dev  = (0,0)",
    "swap dev  = none",
    "root size = 16384 kbytes",
    "user mem  = 65536 kbytes",
    "ReBSD/i686 0.1-Resurgence (console)",
    "login:",
    "REBSD_I686_LS_OK",
    "REBSD_I686_UNAME_OK",
    "REBSD_I686_MD5_OK",
    "REBSD_I686_MEMDEV_OK",
    "REBSD_I686_AWK_OK",
    "REBSD_I686_FREE_OK",
    "REBSD_I686_TOP_OK",
    "/dev/ram0",
    "REBSD_I686_DF_OK",
    "REBSD_I686_LSUSB_OK",
    "REBSD_I686_LSPCI_OK",
    "REBSD_I686_LSPCI_NUMERIC_OK",
    "8086:1237",
    "0600: 8086:1237",
    "lo0",
    "127.0.0.1",
    "REBSD_I686_NETSTAT_OK",
    "REBSD_I686_LOOPBACK_OK",
    "REBSD_I686_TIME64_2040",
    "REBSD_I686_FULL_ROOTFS_OK",
    "REBSD_I686_SHELL_OK",
)

FORBIDDEN_MARKERS = (
    ": Out of memory",
    "\r\nno space\r\n",
    "exception:",
    "PANIC:",
    "panic:",
)

IDE_DISK_MARKERS = (
    "ide-primary-master: ata",
    "ide-lba28: ok",
    "ide-backend-read: ok",
    "ide-lba0: ok",
    "ide-bounds: ok",
    "sd0: 32768 512-byte sectors (16384 KB), read-only",
)

USB_MASS_STORAGE_MARKERS = (
    "ehci0: pci-id=0x808624cd",
    "ehci0: EHCI version=100",
    "umass0: QEMU QEMU HARDDISK",
    "ehci0: port1 device attached speed=high",
    "ehci0: irq ",
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

OHCI_MOUSE_MARKERS = (
    "ohci0: pci-id=0x106b003f",
    "ohci0: OHCI revision=10",
    "mouse1: source ums0",
    "ums0: HID boot mouse, interrupt in 0x81, 4 bytes",
    "ohci0: port1 device attached speed=full",
    "ohci0: irq enabled line=",
    "mouse1: input active",
)

UHCI_MOUSE_MARKERS = UHCI_MARKERS + (
    "mouse1: source ums0",
    "ums0: HID boot mouse, interrupt in 0x81, 4 bytes",
    "uhci0: port1 device attached speed=full",
    "mouse1: input active",
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
    parser.add_argument("--ohci-mouse", action="store_true")
    parser.add_argument("--uhci-keyboard", action="store_true")
    parser.add_argument("--uhci-mouse", action="store_true")
    parser.add_argument("--uhci-disk", type=pathlib.Path)
    parser.add_argument("--ps2-keyboard", action="store_true")
    parser.add_argument("--ps2-mouse", action="store_true")
    parser.add_argument("--expect-no-disk", action="store_true")
    parser.add_argument("--timeout", type=float, default=15.0)
    args = parser.parse_args()

    if args.expect_no_disk and args.disk is not None:
        parser.error("--expect-no-disk cannot be combined with --disk")
    if sum(
        (
            args.uhci_keyboard,
            args.uhci_mouse,
            args.uhci_disk is not None,
        )
    ) > 1:
        parser.error("UHCI keyboard, mouse, and disk use the same test port")
    if args.usb_disk is not None and args.uhci_disk is not None:
        parser.error("only one USB mass-storage device is supported")
    return args


def qemu_command(
    args: argparse.Namespace, monitor_path: pathlib.Path | None
) -> list[str]:
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
        (
            f"unix:{monitor_path},server=on,wait=off"
            if monitor_path is not None
            else "none"
        ),
        "-no-reboot",
        "-no-shutdown",
        "-nic",
        "none",
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
    if args.ohci_mouse:
        command.extend(
            [
                "-device",
                "pci-ohci,id=ohci",
                "-device",
                "usb-mouse,bus=ohci.0,port=1",
            ]
        )
    if args.uhci_keyboard or args.uhci_mouse or args.uhci_disk is not None:
        command.extend(["-device", "piix3-usb-uhci,id=uhci"])
    if args.uhci_keyboard:
        command.extend(["-device", "usb-kbd,bus=uhci.0,port=1"])
    if args.uhci_mouse:
        command.extend(["-device", "usb-mouse,bus=uhci.0,port=1"])
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
                "sd1: 32768 512-byte sectors (16384 KB), removable"
                if args.disk is not None
                else "sd0: 32768 512-byte sectors (16384 KB), removable"
            ),
        )
    if args.ohci_keyboard:
        markers += OHCI_KEYBOARD_MARKERS
    if args.ohci_mouse:
        markers += OHCI_MOUSE_MARKERS
    if args.uhci_keyboard:
        markers += UHCI_KEYBOARD_MARKERS
    if args.uhci_mouse:
        markers += UHCI_MOUSE_MARKERS
    if args.uhci_disk is not None:
        markers += UHCI_MASS_STORAGE_MARKERS
        markers += (
            (
                "sd1: 32768 512-byte sectors (16384 KB), removable"
                if args.disk is not None
                else "sd0: 32768 512-byte sectors (16384 KB), removable"
            ),
        )
    if (
        args.usb_disk is not None
        or args.ohci_keyboard
        or args.ohci_mouse
        or args.uhci_keyboard
        or args.uhci_mouse
        or args.uhci_disk is not None
    ):
        markers += ("Bus 001 Device 001: ID ",)
    return markers


def monitor_connect(
    path: pathlib.Path, deadline: float
) -> socket.socket:
    last_error: OSError | None = None

    while time.monotonic() < deadline:
        connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            connection.connect(str(path))
            connection.settimeout(0.2)
            try:
                connection.recv(4096)
            except TimeoutError:
                pass
            return connection
        except OSError as error:
            last_error = error
            connection.close()
            time.sleep(0.02)
    raise RuntimeError(f"QEMU monitor connection failed: {last_error}")


def monitor_command(connection: socket.socket, command: str) -> None:
    connection.sendall(command.encode("ascii") + b"\n")
    time.sleep(0.03)
    try:
        connection.recv(4096)
    except TimeoutError:
        pass


def monitor_send_text(connection: socket.socket, text: str) -> None:
    key_names = {
        "\n": "ret",
        " ": "spc",
    }

    for character in text:
        key = key_names.get(character, character)
        monitor_command(connection, f"sendkey {key}")


def main() -> None:
    args = parse_args()
    needs_monitor = (
        args.ps2_keyboard
        or args.ps2_mouse
        or args.ohci_mouse
        or args.uhci_mouse
    )
    monitor_dir = (
        pathlib.Path(tempfile.mkdtemp(prefix="rebsd-qemu-", dir="/tmp"))
        if needs_monitor
        else None
    )
    monitor_path = (
        monitor_dir / "monitor.sock" if monitor_dir is not None else None
    )
    process = subprocess.Popen(
        qemu_command(args, monitor_path),
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if process.stdin is None or process.stdout is None:
        raise SystemExit("qemu-boot-smoke: failed to open QEMU pipes")
    monitor = (
        monitor_connect(monitor_path, time.monotonic() + 3.0)
        if monitor_path is not None
        else None
    )

    output_bytes = bytearray()
    login_sent = False
    mouse_sent = False
    ps2_post_network_sent = False
    ps2_post_network_output_start = 0
    commands = (
        (b"/bin/ls /bin/l?\n", b"/bin/ls"),
        (b"echo REBSD_I686_LS_OK\n", b"\r\nREBSD_I686_LS_OK\r\n"),
        (
            b"/bin/uname && echo REBSD_I686_UNAME_OK\n",
            b"\r\nREBSD_I686_UNAME_OK\r\n",
        ),
        (
            b"/usr/bin/md5 /bin/sh && echo REBSD_I686_MD5_OK\n",
            b"\r\nREBSD_I686_MD5_OK\r\n",
        ),
        (
            b"/bin/dd if=/dev/zero of=/dev/null bs=16 count=1 && "
            b"echo REBSD_I686_MEMDEV_OK\n",
            b"\r\nREBSD_I686_MEMDEV_OK\r\n",
        ),
        (
            b"/usr/bin/awk 'BEGIN { print \"REBSD_I686_AWK_OK\" }' "
            b"/etc/passwd\n",
            b"\r\nREBSD_I686_AWK_OK\r\n",
        ),
        (
            b"/usr/bin/free && echo REBSD_I686_FREE_OK\n",
            b"\r\nREBSD_I686_FREE_OK\r\n",
        ),
        (
            b"/usr/bin/top -n 1 && echo REBSD_I686_TOP_OK\n",
            b"\r\nREBSD_I686_TOP_OK\r\n",
        ),
        (
            b"/bin/df && echo REBSD_I686_DF_OK\n",
            b"\r\nREBSD_I686_DF_OK\r\n",
        ),
        (
            b"/usr/bin/lsusb && echo REBSD_I686_LSUSB_OK\n",
            b"\r\nREBSD_I686_LSUSB_OK\r\n",
        ),
        (
            b"/usr/bin/lspci && echo REBSD_I686_LSPCI_OK\n",
            b"\r\nREBSD_I686_LSPCI_OK\r\n",
        ),
        (
            b"/usr/bin/lspci -n && echo REBSD_I686_LSPCI_NUMERIC_OK\n",
            b"\r\nREBSD_I686_LSPCI_NUMERIC_OK\r\n",
        ),
        (
            b"/usr/bin/netstat -ian && echo REBSD_I686_NETSTAT_OK\n",
            b"\r\nREBSD_I686_NETSTAT_OK\r\n",
        ),
        (
            b"/usr/bin/ping -n -c 1 127.0.0.1 && "
            b"echo REBSD_I686_LOOPBACK_OK\n",
            b"\r\nREBSD_I686_LOOPBACK_OK\r\n",
        ),
        (
            b"/bin/date -nu 204001020304.05 >/dev/null && "
            b"/bin/date -u -f REBSD_I686_TIME64_%Y && echo\n",
            b"\r\nREBSD_I686_TIME64_2040\r\n",
        ),
        (
            b"echo REBSD_I686_FULL_ROOTFS_OK REBSD_I686_SHELL_OK\n",
            b"\r\nREBSD_I686_FULL_ROOTFS_OK REBSD_I686_SHELL_OK\r\n",
        ),
    )
    command_index = 0
    command_sent = False
    command_output_start = 0
    completed = False
    deadline = time.monotonic() + args.timeout
    while time.monotonic() < deadline:
        ready, _, _ = select.select([process.stdout], [], [], 0.1)
        if ready:
            chunk = os.read(process.stdout.fileno(), 4096)
            if chunk:
                output_bytes.extend(chunk)
        if not login_sent and b"login: " in output_bytes:
            if args.ps2_keyboard:
                assert monitor is not None
                monitor_send_text(monitor, "root\n")
            else:
                process.stdin.write(b"root\n")
                process.stdin.flush()
            login_sent = True
        if (
            not mouse_sent
            and monitor is not None
            and b"ReBSD/i686 0.1-Resurgence (console)" in output_bytes
            and (args.ps2_mouse or args.ohci_mouse or args.uhci_mouse)
        ):
            monitor_command(monitor, "mouse_move 7 5")
            mouse_sent = True
        if (
            login_sent
            and command_index < len(commands)
            and not command_sent
            and output_bytes.endswith(b"\r\n# ")
        ):
            command_output_start = len(output_bytes)
            process.stdin.write(commands[command_index][0])
            process.stdin.flush()
            command_sent = True
        if (
            command_sent
            and commands[command_index][1]
            in output_bytes[command_output_start:]
            and output_bytes.endswith(b"\r\n# ")
        ):
            command_index += 1
            command_sent = False
            if command_index == len(commands):
                if args.ps2_keyboard:
                    assert monitor is not None
                    ps2_post_network_output_start = len(output_bytes)
                    monitor_send_text(monitor, "echo ps2postnetok\n")
                    ps2_post_network_sent = True
                else:
                    completed = True
                    break
        if (
            ps2_post_network_sent
            and b"\r\nps2postnetok\r\n# "
            in output_bytes[ps2_post_network_output_start:]
        ):
            completed = True
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
    if monitor is not None:
        monitor.close()
    if monitor_dir is not None:
        shutil.rmtree(monitor_dir)

    output = output_bytes.decode("utf-8", errors="replace")
    print(output, end="")
    if not completed:
        raise SystemExit(
            "qemu-boot-smoke: login command sequence did not complete"
        )
    missing = [
        marker for marker in expected_markers(args) if marker not in output
    ]
    if missing:
        raise SystemExit(
            "qemu-boot-smoke: missing serial markers: " + ", ".join(missing)
        )
    forbidden = [marker for marker in FORBIDDEN_MARKERS if marker in output]
    if forbidden:
        raise SystemExit(
            "qemu-boot-smoke: forbidden serial markers: "
            + ", ".join(forbidden)
        )
    free_match = re.search(
        r"Mem:\s+(\d+)\s+(\d+)\s+(\d+)\s+\d+", output
    )
    top_match = re.search(
        r"mem\s+(\d+)K total\s+(\d+)K used\s+(\d+)K free", output
    )
    if free_match is None or top_match is None:
        raise SystemExit("qemu-boot-smoke: missing memory summaries")
    free_total, free_used, free_free = map(int, free_match.groups())
    top_total, top_used, top_free = map(int, top_match.groups())
    if free_used + free_free != free_total:
        raise SystemExit("qemu-boot-smoke: inconsistent free memory summary")
    if top_used + top_free != top_total:
        raise SystemExit("qemu-boot-smoke: inconsistent top memory summary")
    if top_total != free_total:
        raise SystemExit(
            "qemu-boot-smoke: top/free memory totals differ: "
            f"{top_total} != {free_total}"
        )
    print("qemu-boot-smoke: ok")


if __name__ == "__main__":
    main()
