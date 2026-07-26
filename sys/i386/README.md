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

The root image is built deterministically by the existing `tools/fsutil`.
Its initial GCC userland contains:

- `/sbin/init`;
- `/libexec/getty`;
- `/bin/login`;
- `/bin/sh`;
- `/bin/hostname` and `/bin/stty`;
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

The deterministic `rebsd-i686-bios-floppy.img` also contains the complete
embedded UFS payload.  Its native loader uses CHS reads into a 64 KiB staging
buffer and the standard BIOS `INT 15h/AH=87` service to move each chunk to
high memory.  QEMU verifies that this path reaches the same root shell.

## Architecture boundary

`sys/i386` owns only x86 hardware and ABI work: BIOS handoff, E820, paging,
IDT/PIC/PIT, TSS/context frames, COM1/VGA primitives, PCI discovery, legacy
ATA PIO, PCI BAR mapping, USB host-controller attachment and the `int 0x80`
register adapter.

All policy and reusable subsystems remain in common code: VM, scheduler,
process lifecycle, exec, signal policy, syscall handlers, console/TTY,
disk/partition handling, USB enumeration and class drivers, VFS/UFS and
file descriptors.  The no-swap debug configuration is represented by
`swap none`/`NODEV` in the normal kernel configuration path; there is no
i686 pager compatibility flag.

PCC is not part of the i686 build and must not be changed.

## Next QEMU-only work

1. Expand the GCC userland only as required by the normal boot and test
   environment.
2. Keep external IDE and USB devices on the common disk path; add ordinary
   mount support without changing the embedded UFS root policy.
3. Complete PS/2 keyboard, RTC and VGA-console validation for machines
   without a serial terminal.
4. Run the full QEMU RAM, IDE and USB matrix and common MIPS/N64 regressions.
5. Request a new IBM 6563-W4G test only after those gates are green.
