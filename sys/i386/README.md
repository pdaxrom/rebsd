# ReBSD i686/BIOS port

This is the GCC-only port for legacy BIOS PCs.  The first physical target is
the IBM PC 300GL 6563-W4G: Pentium III, VIA Apollo Pro 133, AGP, IDE-CF and
no floppy drive.

The normal kernel now enters the machine-independent `init_main`, mounts the
embedded read-only UFS through the common romdisk/VFS/UFS path, creates
process 1 through common process code and executes the regular userland
`/sbin/init`.  QEMU smoke logs in as root through the common console/TTY
stack and executes a command in `/bin/sh`; the normal boot has no diagnostic
`HALT` path.

## Build and QEMU

Use a separate object directory:

```sh
make -C sys/i386 BOARD=pc O=/work/rebsd-build/i686-pc kernel
make -C sys/i386 BOARD=pc O=/work/rebsd-build/i686-pc rootfs-smoke
make -C sys/i386 BOARD=pc O=/work/rebsd-build/i686-pc boot-smoke
make -C sys/i386 BOARD=pc O=/work/rebsd-build/i686-pc ide-smoke
make -C sys/i386 BOARD=pc O=/work/rebsd-build/i686-pc ps2-input-smoke
make -C sys/i386 BOARD=pc O=/work/rebsd-build/i686-pc \
    ohci-mouse-smoke uhci-mouse-smoke
```

`boot-smoke` boots without an external disk and proves this complete path:

```text
embedded UFS -> common init_main -> proc1 -> /sbin/init
             -> getty -> /bin/login -> /bin/sh
```

`ide-smoke` adds the UFS image as an external legacy ATA disk.  It must
appear as a read-only common `sd0`, but root remains the embedded romdisk at
block major 0, minor 0.  USB mass-storage tests use the same common `sdN`
namespace and the existing USB core, hubs, EHCI/OHCI/UHCI, `umass` BOT/SCSI
and disk code.  The romdisk never consumes an `sdN` number.

`ps2-input-smoke` injects keyboard and mouse traffic through QEMU's i8042
IRQs.  The keyboard logs in through the common console/TTY path, while the
mouse publishes events through `/dev/mouse0`.  The OHCI and UHCI mouse gates
use the same machine-independent mouse queue and `/dev/mouse1`; only the
transport-specific decoders differ.

The root image is built deterministically by the existing `tools/fsutil`.
Its initial GCC userland contains:

- `/sbin/init`;
- `/libexec/getty`;
- `/bin/login`;
- `/bin/sh`;
- `/bin/hostname`, `/bin/ls` and `/bin/stty`;
- the existing common account, profile and network configuration files.

The filesystem is read-only by policy during bring-up.  It is not FAT and
there is no i386-private filesystem, executable loader, disk layer, process
implementation or syscall table.

## Boot loaders

`rebsd-i686.bzimg` implements Linux/x86 boot protocol 2.02.  QEMU loads it
with `-kernel`; the IBM can load the same file directly from its existing
GRUB Legacy installation:

```text
title ReBSD i686
    root (hd0,2)
    kernel /boot/rebsd-i686.bzimg
```

`rebsd-i686.bzimg` is the only installation artifact produced by the default
build and the only image copied to target machines.  The separate raw-floppy
target is retained solely as an explicitly requested loader regression; it is
not part of `all` and is not a release or hardware-gate artifact.

## Architecture boundary

`sys/i386` owns only x86 hardware and ABI work: BIOS handoff, E820, paging,
IDT/PIC/PIT, TSS/context frames, COM1/VGA primitives, i8042 port and IRQ
transport, PCI discovery, legacy ATA PIO, PCI BAR mapping, USB
host-controller attachment and the `int 0x80` register adapter.

All policy and reusable subsystems remain in common code: VM, scheduler,
process lifecycle, exec, signal policy, syscall handlers, console/TTY,
keyboard mapping, PS/2 keyboard and mouse decoding, mouse event devices,
disk/partition handling, USB enumeration and HID class drivers, VFS/UFS
and file descriptors.  The no-swap debug configuration is represented by
`swap none`/`NODEV` in the normal kernel configuration path; there is no
i686 pager compatibility flag.  N64's existing Joybus mouse snapshot ABI is
unchanged; new transports use the common event API rather than duplicating
it in a board directory.

PCC is not part of the i686 build and must not be changed.

## Next QEMU-only work

1. Expand the GCC userland only as required by the normal boot and test
   environment.
2. Keep external IDE and USB devices on the common disk path; add ordinary
   mount support without changing the embedded UFS root policy.
3. Complete RTC validation; PS/2 keyboard/mouse and VGA-console input are
   QEMU-complete and await the real IBM gate.
4. Run the full QEMU RAM, IDE and USB matrix and common MIPS/N64 regressions.
5. Request a new IBM 6563-W4G test only after those gates are green.
