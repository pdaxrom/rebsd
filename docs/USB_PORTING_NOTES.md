# ReBSD USB Porting Notes

This is the Phase 0 map between the NetBSD 3.1 USB subsystem and the existing
ReBSD kernel.  USB implementation code must not be added until this map and
`docs/USB_ORIGIN.md` are committed.

## Scope

The first hardware target is the dedicated Creator Ci20 USB host port on the
Ingenic JZ4780, using OHCI for USB 1.x and EHCI for USB 2.0 high speed.  DWC2
OTG host mode, isochronous transfers, generic HID, USB networking, USB serial,
audio, video, and power management are outside the first port.

The first completed hardware milestone is OHCI root-hub enumeration plus a HID
boot keyboard feeding the ReBSD console, including reliable disconnect and
reconnect.  EHCI and read-only mass storage are later milestones and are not
part of the OHCI-keyboard definition of done.

## Baseline

The source baseline is commit `b0af5168` on branch `usb-support`.

The following clean out-of-tree Ci20 build completed successfully on
2026-07-12:

```sh
make -C sys/mips BOARD=ci20 \
    MIPS_KERNEL_COMPILER=gcc MIPS_ROOTFS_COMPILER=gcc all
```

The generated object profile is
`ci20-kgcc-ugcc-mips32r2-hard-little-elf`.  The final kernel linked and
`ci20.uImage` was generated.

The corresponding PCC baseline also completed successfully:

```sh
make -C sys/mips BOARD=ci20 \
    MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc all
```

The generated object profile is
`ci20-kpcc-upcc-mips32r2-hard-little-elf`.  PCC reported
`Portable C Compiler 1.2.0.DEVEL 20231021 for mipsel-unknown-rebsd`; the final
kernel linked and `ci20.uImage` was generated.  These are build-only baseline
results, not Creator Ci20 hardware results.

## NetBSD to ReBSD Mapping

| NetBSD concept | ReBSD equivalent or porting decision |
| --- | --- |
| `device_t` | No equivalent.  Use bounded static softc objects and explicit attach functions. |
| device softc | A normal C structure owned by the selected service/controller; no device framework base object. |
| `config_found` | A small USB interface-driver registry and matcher inside the USB core; platform controller attachment remains explicit. |
| autoconfiguration | Existing `tools/kconfig` `device`/`service` entries generate compile defines and `conf_service_init`. |
| `bus_space_read_4` | Small native MMIO accessors using volatile KSEG1 addresses and an explicit MIPS ordering barrier. |
| `bus_space_write_4` | Same native accessor layer; generic OHCI/EHCI receives register callbacks or an HCD register handle and never accesses JZ4780 CPM/GPIO. |
| `bus_dmamem_alloc` | New compact ReBSD `dma_alloc` over a reserved contiguous pool. |
| `bus_dmamem_free` | New `dma_free` returning a bounded pool extent. |
| `bus_dmamap_load` | Not imported.  Initial USB buffers are allocated from the DMA pool and already have one physical segment. |
| `bus_dmamap_sync` | New `dma_sync_for_device` and `dma_sync_for_cpu`; the first Ci20 backend uses an uncached KSEG1 mapping plus barriers. |
| `malloc/free` | No general ReBSD kernel byte heap exists.  USB uses fixed configurable pools and the DMA pool. |
| `tsleep/wakeup` | Existing ReBSD `tsleep`/`wakeup` in process context. |
| NetBSD callout | Existing fixed-table `timeout`/`untimeout`; no callout framework import. |
| kernel threads | No current ReBSD kernel-thread API.  The polling milestone runs from process context; interrupt completion requires one bounded native deferred-work mechanism, not a NetBSD kthread import or one thread per device. |
| mutex/condvar | UP ownership plus short `splhigh`/`splx` critical sections; never sleep with interrupts masked. |
| soft interrupt/task queue | No general equivalent.  Add only the minimum bounded USB completion/task queue needed before interrupt-driven OHCI. |
| `splusb` | Use the existing global interrupt masking primitive through a small USB critical-section wrapper. |
| root-hub child attach | USB core creates a `usb_device`; the HCD exposes root-hub control and port status through the common HCD operations. |
| disk attach | Add a static `bdevsw` entry and a ReBSD `strategy(struct buf *)` adapter. |
| `scsipi` | No equivalent is present.  Implement only BOT plus the required single-LUN read-only SCSI commands. |
| wscons keyboard | No equivalent.  Add a small keyboard-input registration/submission API above `ttyinput`. |

## Existing Device Attachment Model

`tools/kconfig` generates feature defines such as `CI20_DM9000_ENABLED` and a
`conf_service_init` table from `device` and `service` declarations.  Services
are called from `init_main.c` after the basic kernel tables and clock have been
initialized.  Ci20 early configuration currently calls `ci20_uart_attach()`
directly from `kconfig()`.

USB will use the existing configuration grammar rather than introduce Linux
Kconfig symbols.  Generic files will be `optional usb`, and HCD/class files
will be optional on their own services or devices.  A Ci20 configuration may
produce defines equivalent to:

- `USB_ENABLED`
- `OHCI_ENABLED`
- `EHCI_ENABLED`
- `UHUB_ENABLED`
- `UKBD_ENABLED`
- `UMASS_ENABLED`
- `USB_DEBUG_ENABLED`

When USB is absent, no USB object, driver table, transfer pool, vendor table,
or DMA pool is linked.  In particular, the N64 configuration remains
unchanged and pays no USB memory cost.

## Interrupts and Deferred Work

Ci20 interrupt dispatch is currently hard-coded in
`sys/mips/ci20/uart.c:mips_board_intr()`.  It checks the TCU, GPIOE/DM9000, and
UART4 pending bits directly.  There is an exported unmask helper, but no
general interrupt registration API.  `ci20_uart_attach()` also initializes and
masks the two JZ4780 INTC banks, so attach order matters.

The JZ4780 USB platform phase needs a small Ci20-native interrupt dispatcher
that can register OHCI and EHCI handlers without putting USB knowledge in the
UART driver.  This is platform infrastructure, not part of the generic USB or
generic HCD layers.  The change must preserve the existing TCU, UART4, and
DM9000 behavior.

The ownership contract is:

- hardware interrupt context acknowledges controller status, detaches a done
  list from hardware, and queues bounded completion work;
- USB deferred context processes completions, explores hubs, and performs
  attach/detach work;
- process context issues synchronous requests and sleeps with `tsleep`;
- no descriptor parsing, attachment, filesystem call, or long wait occurs in
  a hardware interrupt handler;
- no DMA object is freed until the HCD has stopped referencing it.

ReBSD has no general deferred-work worker.  Initial OHCI enumeration therefore
starts in polling mode from process context.  Before interrupt-driven OHCI, a
single bounded USB task queue and a safe execution mechanism must be added and
tested.  A clock callout alone must not be treated as process context, and a
NetBSD kernel-thread framework will not be imported.

## Sleep, Wakeup, and Timeouts

`tsleep(ident, priority, ticks)` and `wakeup(ident)` are available and already
handle timeout races through the existing scheduler.  `tsleep` depends on
`u.u_procp` and is therefore a process-context primitive.  Synchronous USB
transfers will sleep on their `usb_xfer` while asynchronous transfers use one
final callback.

`timeout` and `untimeout` use a global statically sized callout array.  The
default `NCALL` is `16 + 2 * MAXUSERS`, currently 18 for the normal MIPS
configuration.  USB must not allocate an unbounded callout per object.  The
eventual configuration will add a documented USB-dependent allowance or use a
small shared timeout scheduler.

Every transfer has one terminal transition from `ACTIVE` or `QUEUED` to one of
`COMPLETED`, `CANCELLED`, `TIMED_OUT`, or disconnected error completion.
Normal completion, timeout, abort, and detach compete under one short critical
section; only the winner removes the transfer and produces the callback or
wakeup.

## Memory Allocation

The kernel has no general-purpose byte allocator.  `malloc()` and `mfree()` in
`subr_rmap.c` allocate numeric resource ranges from fixed maps; they do not
return kernel memory.  Network mbufs and core kernel tables use statically
allocated arrays.

USB will follow the same bounded model:

- statically configured pools for USB devices, interfaces, endpoints, pipes,
  transfers, hubs, and HCD software descriptors;
- explicit maximum counts checked before allocation;
- no large USB product-name database;
- no unbounded configuration-descriptor allocation;
- a separate physically contiguous DMA pool for hardware descriptors and
  transfer payloads.

Pool limits will be visible in one USB limits header and may be overridden by
board configuration.  Exhaustion returns an explicit no-memory error; it does
not panic or silently discard an active object.

## Physical Mapping and DMA

Ci20 currently uses the first 256 MiB of RAM.  `layout.h` provides direct
mapping helpers:

- physical to cached KSEG0: `0x80000000 | paddr`;
- physical to uncached KSEG1: `0xa0000000 | paddr`;
- KSEG virtual to physical: `vaddr & 0x1fffffff`.

No reusable `bus_dma` or DMA mapping API exists.  The only historical generic
DMA-related code is an old network contiguous-area helper in `uipc_mbuf.c`,
which is not suitable as the USB API.

The first Ci20 DMA implementation will reserve a linker-defined, aligned,
physically contiguous region below the 32-bit DMA limit and access it only
through its uncached KSEG1 alias after initialization.  It will not declare
ordinary cached kernel buffers coherent.  Class drivers copy between DMA
payload buffers and normal kernel buffers.  This is deliberately simple and
safe; future architectures may provide a cached backend with real range cache
maintenance.

The generic API must validate:

- power-of-two alignment;
- offset and length overflow;
- allocation ownership and double free;
- `DMA_32BIT`, `DMA_CONTIGUOUS`, `DMA_COHERENT`, and `DMA_ZERO` semantics;
- sync direction and range;
- reuse after free.

Ci20 has 32-byte L1 data-cache lines.  Existing machine code contains local
cache instructions for user instruction synchronization, but there is no
general exported D-cache range API.  The uncached first backend avoids relying
on those local routines while preserving sync calls in the machine-independent
contract.

The JZ4780 PDMA controller is not a dependency of USB OHCI/EHCI.  OHCI and EHCI
are bus-master controllers and consume their own DMA descriptor schedules.
System PDMA support remains a separate future driver.

## MMIO and Platform Boundary

ReBSD currently uses direct volatile register access.  USB will introduce only
small native 32-bit little-endian MMIO helpers with ordering, not the NetBSD
bus_space framework.  Generic OHCI/EHCI owns only standardized controller
registers.  It must not touch JZ4780 clocks, CPM reset, PHY, GPIO, or VBUS.

The platform layer under `sys/mips/ci20/` owns:

- physical controller addresses and KSEG1 mapping;
- raw JZ4780 interrupt numbers and registration;
- UHC clock selection and gating;
- reset assertion and release;
- PHY power and host-mode setup;
- Ci20 VBUS GPIO control;
- EHCI/OHCI companion routing and JZ4780-specific quirks;
- HCD attachment and DMA backend selection.

Each platform register write will be recorded in `docs/CI20_USB.md` with the
register address or offset, bit definition, source, and reason.

## Block Device Interface

ReBSD block drivers expose `open`, `close`, `strategy`, `psize`, and `ioctl`
through `bdevsw`.  A strategy request receives `struct buf`; completion uses
`biodone`, and errors set `b_error` plus `B_ERROR`.

`DEV_BSIZE` is 1024 bytes, while the first USB storage target uses 512-byte
logical sectors.  The umass adapter must translate a ReBSD block number to two
512-byte SCSI logical blocks and correctly handle `b_bcount`, residuals, end of
media, and read-only errors.  It must not silently reinterpret ReBSD block
numbers as 512-byte sectors.

There is no SCSI or SCSIPI subsystem in the current tree.  The first umass
implementation therefore contains only BOT framing and `INQUIRY`,
`TEST UNIT READY`, `REQUEST SENSE`, `READ CAPACITY(10)`, and `READ(10)` for one
LUN.  Write support is a later commit.

## Console Input

The common MIPS console owns `cnttys[0]` and feeds received characters through
a private `cninput()` wrapper to `ttyinput()`.  There is no keyboard source
registration API.  The HID phase will add a small machine-independent input
submission interface so UART and USB keyboard sources can feed the console
without the USB driver knowing about the console implementation and without
hard-wiring USB checks into `cnread()`.

The first HID driver supports only boot-protocol keyboards, 8-byte reports,
modifier state, press/release tracking, and a basic US map.  A generic HID
report parser and wscons-style framework are not imported.

## Locking Model

The current MIPS kernel is effectively uniprocessor.  `splbio`, `spltty`,
`splclock`, `splnet`, and `splhigh` all disable interrupts globally.  USB will
use this only for short queue and state transitions.  It will not hold an
interrupt-disabled critical section across `tsleep`, a control transfer,
descriptor parsing, driver attach/detach, or block I/O completion.

Object ownership and terminal transfer transitions will be written explicitly
in `docs/USB_ARCHITECTURE.md` before the mock HCD/core commit.  The design will
avoid assumptions that prevent later SMP locking, but full MP safety is not a
goal of the first Ci20 port.

## Test Strategy Established in Phase 0

Machine-independent tests will run without USB hardware and cover descriptor
parsing, malformed descriptors, address allocation, driver matching, mock-HCD
enumeration, HID boot reports, disconnect/reconnect, timeout arbitration,
cancellation, and DMA allocator rules.

Generic USB compile tests must not define `CI20` or include JZ4780 headers.
Ci20 build gates will use both GCC and PCC.  QEMU may validate a generic
emulated OHCI/EHCI controller only when the exact machine and controller are
recorded; QEMU Malta is not evidence of JZ4780 hardware operation.

Creator Ci20 hardware success requires a committed UART log naming the board,
device, connection topology, detected speed, selected HCD, and test result.
No hardware milestone is reported from compilation or register-level
inspection alone.
