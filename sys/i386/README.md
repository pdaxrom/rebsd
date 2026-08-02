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
programming, automatic capability fallback and injected DMA error/timeout;
the external disk published by the i686
kernel deliberately remains read-only, so the real CF is not written.

The external ATA disk must appear as a read-only common `sd0`, while root
remains the embedded romdisk at block major 0, minor 0.  USB mass-storage
tests use the same common `sdN`
namespace and the existing USB core, hubs, EHCI/OHCI/UHCI, `umass` BOT/SCSI
and disk code.  The romdisk never consumes an `sdN` number.

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
no-swap debug configuration is represented by
`swap none`/`NODEV` in the normal kernel configuration path; there is no
i686 pager compatibility flag.  N64's existing Joybus mouse snapshot ABI is
unchanged; new transports use the common event API rather than duplicating
it in a board directory.

The low-linked kernel includes the embedded UFS image.  The linker verifies
that the complete kernel stays below the 32 MiB user virtual-address base,
and early paging maps the complete kernel rather than assuming a 4 MiB image.
User physical pages are still demand allocated; the virtual-address boundary
does not reserve 32 MiB of RAM per process.

PCC is not part of the i686 build and must not be changed.

## Next hardware gates

1. Recheck Backspace/readline redraw and VGA cursor placement with the shared
   VT100 console build on the IBM 6563-W4G.
2. Verify USB keyboard/mouse input on the IBM; PS/2 keyboard/mouse attachment
   and input are already confirmed.
3. Keep external IDE and USB devices on the common disk path and validate
   ordinary mounts without changing the embedded read-only UFS root policy.
4. Validate automatic ATA selection on the VIA controller.  Forced PIO and
   forced MWDMA reads are already confirmed with the CF exposed read-only.
5. Validate CMOS persistence after `date`/`settimeofday` and USB mass storage
   on the VIA Apollo Pro 133.
6. Validate `re0` attach, level-triggered INTx, link, static IPv4, ARP, ICMP
   RX/TX latency and sustained traffic on the installed `10ec:8169` PCI
   adapter.
