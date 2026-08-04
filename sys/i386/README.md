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
make -C sys/i386 BOARD=pc O=/work/rebsd-build/i686-pc \
    ide-pio-smoke ide-dma-smoke ide-auto-smoke
make -C sys/i386 BOARD=pc O=/work/rebsd-build/i686-pc ps2-input-smoke
make -C sys/i386 BOARD=pc O=/work/rebsd-build/i686-pc \
    ohci-mouse-smoke uhci-mouse-smoke
```

`boot-smoke` boots without an external disk and proves this complete path,
then runs representative programs from the full root filesystem (`ls`,
`uname`, `md5`, `awk`, `free`, `df` and the stack-growth regression in
`netstat`).  It verifies the common `/dev/null` and `/dev/zero` operations,
requires the writable UFS `/dev/ram0` filesystem to be mounted on `/var`,
requires `lo0` to own `127.0.0.1` and completes an ICMP echo exchange through
that address:

```text
embedded UFS -> common init_main -> proc1 -> /sbin/init
             -> getty -> /bin/login -> /bin/sh
```

The common IPv4 stack and loopback interface are initialized independently
of physical network-device discovery.  Consequently `lo0` and `127.0.0.1`
are always available on a normal i686 boot even when no Ethernet adapter is
present.  i686 uses the same socket, protocol, interface and loopback sources
as the MIPS boards.  The interrupt-return boundary immediately drains network
work produced by handlers of the current IRQ.  Work which was already pending
is drained at system-call or timer return, so unrelated PS/2 IRQ1 and IRQ12
never execute the deferred network stack.  PCI INTx lines are programmed as
level-triggered through the PC ELCR before they are unmasked; fixed ISA
edge-triggered lines keep their ISA trigger mode.

Wall-clock time uses the machine-independent BSD TODR layer in
`sys/kernel/todr.c`.  The PC attachment only supplies MC146818 CMOS register
access through ports `0x70`/`0x71`; BCD/binary and 12/24-hour decoding,
Gregorian conversion, provider selection and `settimeofday` write-back are
shared with the MIPS boards.  At boot a valid CMOS value takes precedence
over the embedded filesystem timestamp.  QEMU `boot-smoke` verifies that the
selected provider is `mc146818` and that the kernel reports a current UTC
hardware time.

The IBM PCI Ethernet adapter `10ec:8169` is exposed as `re0`.  PCI bus
enumeration, BAR probing, resource access and the RTL8169/RTL8110 hardware
driver are machine-independent code under `sys/pci`; i686 supplies only PCI
configuration mechanism 1, I/O/MMIO mapping and legacy INTx routing.  The
driver supports the original RTL8169/RTL8110 MAC versions 2 through 6, uses
common DMA allocation and the same `ifnet`/ARP/IPv4 path as the MIPS Ethernet
drivers.  Its register and MAC-version definitions are checked against the
[upstream Linux r8169 driver](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/realtek/r8169_main.c).

QEMU 11 has no RTL8169 device model.  The exact controller path is therefore
covered by a fake-hardware host test (PCI config and BARs, DMA rings, RX/TX,
link interrupt and PCI system-error recovery):

```sh
make -C sys/tests/pci test
```

The IDE gates add the UFS image as an external legacy ATA disk and exercise
forced PIO, forced PCI bus-master MWDMA and automatic mode selection.  The
machine-independent `sys/pci/pciide.c` driver owns IDENTIFY, PIO and DMA
transfers, controller timings, IRQ completion, reset and the permanent
DMA-error-to-PIO transition.  The i386 attachment only supplies the fixed
compatibility ports, IRQ14 and scheduler wait/wakeup boundary.  QEMU's PIIX3
and the IBM's VIA 82C596B/82C571 path are supported.  Host fake-hardware
tests cover PIO and DMA reads/writes/cache flushes, PIIX/VIA timing
programming, automatic capability fallback and injected DMA error/timeout.
The external IDE disk is writable through the same common disk strategy.
The forced PIO and DMA QEMU gates overwrite one sector, read it back, restore
the original contents, flush the device and verify the restoration.  QEMU
runs these destructive checks in a temporary snapshot, so their backing disk
images remain unchanged.

The PCI IDE backend publishes its 128 KiB maximum DMA command size to the
existing machine-independent disk read-ahead contract.  Each ATA command uses
a standard two-entry PRDT whose 64 KiB regions do not cross a 64 KiB boundary.
Sequential buffered reads of `/dev/wd0` therefore issue one 256-sector ATA
command per window instead of 128 one-kilobyte commands.  PIO remains the same
backend fallback; no alternate disk path or cache implementation is used.

The common disk layer assigns BSD device-name classes independently of the
transport attachment order.  ATA/IDE disks use `wdN` and the raw `rwdN`
character devices; direct-access SCSI disks, including USB mass storage, use
`sdN` and `rsdN`.  Thus the IBM IDE-CF device is writable `wd0`/`rwd0`, and a
USB disk is `sd0`/`rsd0` even when both devices are attached.  Raw I/O uses the
same common disk strategy, `rawrw512()` and backend while bypassing the 1 KiB
buffer cache.  Requests must be aligned to 512-byte sectors.

The i686 kernel uses the shared `sys/fs/fat` FAT16/FAT32 VFS implementation.
Filesystems are mounted through block partition devices, for example
`mount -t fat -r /dev/wd0a /mnt`; raw `rwdN`/`rsdN` character devices remain
for aligned direct I/O and filesystem utilities.  `fat-mount-smoke` builds an
MBR/FAT16 test disk and verifies the complete `/dev/wd0a` mount, directory
read and unmount path under QEMU.
The FAT32 geometry gate additionally runs `fsck.fat -y` against an oversized
test filesystem, reopens it read-only, and verifies that the corrected primary
and backup boot sectors persist.  That repair also runs in a QEMU snapshot.

Root remains the embedded romdisk at block major 0, minor 0.  USB mass-storage
tests reuse the existing USB core, hubs, EHCI/OHCI/UHCI, `umass` BOT/SCSI and
common disk code.  The romdisk consumes neither a `wdN` nor an `sdN` number.

`ps2-input-smoke` injects keyboard and mouse traffic through QEMU's i8042
IRQs.  The keyboard logs in through the common console/TTY path and the test
deliberately corrects both the login name and a shell command with Backspace.
The i386 VGA adapter only renders text cells and programs the hardware cursor;
VT100 parsing and line-edit redraw sequences are owned by the shared
`sys/console/vtconsole.c` core also used by the N64 and Ci20 framebuffer
consoles.  The mouse publishes events through `/dev/mouse0`.  The OHCI and
UHCI mouse gates use the same machine-independent mouse queue and
`/dev/mouse1`; only the transport-specific decoders differ.

The 16 MiB root image is built deterministically by the existing
`tools/fsutil`.  Its GCC userland uses the shared full-rootfs profile in
`mk/rootfs-userland.mk`: the normal libraries, administrative tools, shells,
editors, network utilities, diagnostics, manual pages and the existing common
account/profile/network configuration.  MIPS boards consume that same profile
and add only their architecture-specific tools.  PCC remains enabled for the
N64 and MIPS board images, but is deliberately absent from i686.

The embedded root filesystem is read-only by policy during bring-up.  As on
Ci20 and N64, `/var` is a writable UFS filesystem created at boot on the
common directly-addressable RAM block driver; i686 attaches a 1 MiB backing
store as `/dev/ram0`.  `/tmp` remains the standard symlink to `/var/tmp`.
Root is not FAT and there is no i386-private filesystem, executable loader,
disk layer, process implementation or syscall table.

## Boot loaders

`rebsd-i686.bzimg` implements Linux/x86 boot protocol 2.02.  QEMU loads it
with `-kernel`; the IBM can load the same file directly from its existing
GRUB Legacy installation:

```text
title ReBSD i686
    root (hd0,2)
    kernel /boot/rebsd-i686.bzimg ata=auto
```

`ata=auto` selects the highest common MWDMA mode and otherwise stays in PIO.
For diagnostics, `ata=pio` forces the fallback path and `ata=dma` requires
DMA attachment.  These are selections of the same driver, not separate
compatibility implementations.

`rebsd-i686.bzimg` is the only installation artifact produced by the default
build and the only image copied to target machines.  The separate raw-floppy
target is retained solely as an explicitly requested loader regression; it is
not part of `all` and is not a release or hardware-gate artifact.

## Architecture boundary

`sys/i386` owns only x86 hardware and ABI work: BIOS handoff, E820, paging,
IDT/PIC/PIT, TSS/context frames, COM1/VGA primitives, i8042 port and IRQ
transport, PCI configuration mechanism 1 and resource/INTx mapping, legacy
ATA compatibility-port and IRQ14 attachment, USB
host-controller attachment and the `int 0x80` register adapter.

All policy and reusable subsystems remain in common code: VM, scheduler,
process lifecycle, exec, signal policy, syscall handlers, console/TTY,
keyboard mapping, PS/2 keyboard and mouse decoding, mouse event devices,
disk/partition handling, PCI enumeration and BAR contracts, RTL8169 Ethernet,
USB enumeration and HID class drivers, VFS/UFS and file descriptors.  The
kernel configuration keeps `swap none`/`NODEV` because swap devices are
selected at run time rather than compiled into an i686 image.  The common VM
pager is active and accepts Linux swap v1 devices through `mkswap`, `swapon`
and `swapoff`; there is no i686 pager compatibility flag.  N64's existing
Joybus mouse snapshot ABI is
unchanged; new transports use the common event API rather than duplicating
it in a board directory.

The low-linked kernel includes the embedded UFS image.  The linker verifies
that the complete kernel stays below the 32 MiB user virtual-address base,
and early paging maps the complete kernel rather than assuming a 4 MiB image.
User physical pages are still demand allocated; the virtual-address boundary
does not reserve 32 MiB of RAM per process.

PCC is not part of the i686 build and must not be changed.

## IBM 6563-W4G hardware gate

The initial IBM/VIA hardware gate was completed on 2026-08-04.  The physical
machine has confirmed all of the following with the normal
`rebsd-i686.bzimg` build:

1. Backspace/readline redraw and VGA hardware-cursor placement through the
   shared VT100 console.
2. PS/2 keyboard and mouse plus USB keyboard and mouse through the VIA UHCI
   controller.
3. USB mass storage on the common SCSI disk path alongside the IDE disk:
   IDE remains `wd0`/`rwd0`, while USB mass storage is `sd0`/`rsd0`.
4. Read-write FAT mounting, file and directory mutation, sync, unmount,
   remount and a clean subsequent `fsck.fat -n`, without changing the
   embedded read-only UFS root policy.
5. Automatic VIA ATA selection, UDMA4 direct scatter/gather I/O, write and
   cache-flush persistence across reboot; forced PIO remains the tested
   fallback.
6. MC146818 persistence after `date`/`settimeofday` and power removal.
7. `re0` attach, level-triggered INTx, link, IPv4/ARP/ICMP latency and
   sustained traffic on the installed `10ec:8169`, including concurrent
   IDE DMA, USB and PS/2 activity without growing interface error counters.

Swap is now under user control and is not tied to the kernel build.  For
example, an IDE partition can be formatted and enabled with:

```text
mkswap /dev/rwd0b
swapon /dev/wd0b
free
swapoff /dev/wd0b
```

Several devices may be enabled simultaneously.  `swapoff` migrates resident
pages before detaching the selected device.  The same common pager accepts a
runtime RAM block device; compression belongs to that block device, so the
device can hold either swap or a filesystem:

```text
ramctl create /dev/ram1 backing=8M size=24M compression
mkswap /dev/ram1
swapon /dev/ram1
ramctl status /dev/ram1
swapoff /dev/ram1
ramctl destroy /dev/ram1
```

`size` is the logical capacity.  `backing` is the maximum RAM consumed by the
compressed store; it is not a second swap-specific limit.  Uncompressed RAM
devices omit `compression` and normally use `size=VALUE` or `size=all`.
