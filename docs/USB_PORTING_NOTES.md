# ReBSD USB Porting Notes

This is the Phase 0 map between the NetBSD 3.1 USB subsystem and the existing
ReBSD kernel.  USB implementation code must not be added until this map and
`docs/USB_ORIGIN.md` are committed.

## Scope

The first hardware target is the dedicated Creator Ci20 USB host port on the
Ingenic JZ4780, using OHCI for USB 1.x and EHCI for USB 2.0 high speed.  DWC2
OTG host mode, isochronous transfers, generic HID, USB networking, USB serial,
audio, video, and power management are outside the first port.

The first target milestone is OHCI root-hub enumeration plus a HID boot
keyboard feeding the ReBSD console. Preconnected input, boot with an empty
port, late attach, repeated post-boot disconnect/reconnect, EHCI high-speed
enumeration/reconnect, and low-speed companion handoff are hardware verified
on 2026-07-13. The read-only Bulk-Only/SCSI backend and common disk layer are
also hardware-verified for capacity, MBR, whole/partition reads, and idle
detach/reconnect. A filesystem mount remains the next Phase 10 milestone. The
separate J24/J8 DWC2 OTG connection is outside this first host-port scope.

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
| kernel threads | No general ReBSD kernel-thread API is imported. `newproc()` is a user-process constructor and proved unsafe for a permanent SSYS child. The existing proc0 scheduler loop drains the one bounded USB task queue. |
| mutex/condvar | UP ownership plus short `splhigh`/`splx` critical sections; never sleep with interrupts masked. |
| soft interrupt/task queue | A USB-local fixed queue holds at most eight coalescing tasks. IRQ code schedules work and wakes proc0; the proc0 scheduler loop performs root-hub exploration, enumeration, attach, and detach. |
| `splusb` | Use the existing global interrupt masking primitive through a small USB critical-section wrapper. |
| root-hub child attach | USB core creates a `usb_device`; the HCD exposes root-hub control and port status through the common HCD operations. |
| disk attach | `sys/disk` owns the static `bdevsw` entry, units/minors, MBR regions and `strategy(struct buf *)`; USB, SD/MMC, IDE and SATA attach through one backend contract. |
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

The eventual general ownership contract is:

- hardware interrupt context acknowledges controller status, detaches a done
  list from hardware, and queues bounded completion work;
- USB deferred context processes completions, explores hubs, and performs
  attach/detach work;
- process context issues synchronous requests and sleeps with `tsleep`;
- no descriptor parsing, attachment, filesystem call, or long wait occurs in
  a hardware interrupt handler;
- no DMA object is freed until the HCD has stopped referencing it.

ReBSD has no general deferred-work framework, so USB now supplies one bounded
queue drained by the existing proc0 scheduler loop. The OHCI hard interrupt handles
the bounded eight-byte boot-keyboard completion directly, but root-hub RHSC
only acknowledges/masks the source and schedules a coalescing task. Proc0
performs debounce delays, control transfers, enumeration, attach, and detach,
then reenables RHSC without clearing an event that may have arrived while it
was masked. A clock callout is not treated as process context, and no NetBSD
kernel-thread framework is imported.

Creator Ci20 hardware showed that WDH/NOT_RESPONDING can precede the
disconnect RHSC. A failed periodic transfer therefore masks WDH, RHSC, and
MIE and directly queues a deferred root-port probe. Process context reads and
clears the port state before reenabling RHSC/MIE; an RHSC that arrives in the
meantime remains pending. Simply leaving MIE enabled caused interrupt
starvation on a later hardware candidate and is not used.

No USB process is forked. An initial attempt to use `newproc()` first made the
worker an init child, blocking init's startup wait. Parenting it to proc0
removed that wait but still left a stale process in the hashed sleep queue,
causing `panic: wakeup` after disconnect. The final design wakes proc0 on
`runin` and `runout`; scheduler-side pending checks before both sleeps
prevent a task from being stranded by a lost wakeup.

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

## Protocol Definitions and Descriptor Parser

The compact `sys/usb/usb.h` keeps the NetBSD 3.1 descriptor layout,
request constants, hub status bits, initial class codes, and unaligned
little-endian byte-array accessors.  Userland ioctl structures, event support,
and the generated vendor/product database were not imported.  Numeric vendor,
product, class, subclass, and protocol values are sufficient for initial
diagnostics.

`sys/usb/usb_subr.c` provides allocation-free parsing.  It copies accepted
device, configuration, interface-zero-alternate, and endpoint descriptors into
bounded result structures while preserving raw-buffer offsets.  Other
alternate settings are validated and counted but are not retained as active
interfaces.  The parser never follows a descriptor-provided pointer and never
advances beyond the validated `wTotalLength`.

Initial compile-time limits are:

- configuration descriptor bytes: 1024;
- active interfaces: 8;
- endpoints per interface alternate: 8;
- interface alternate descriptors: 16;
- descriptors traversed in one buffer: 64.

The host compile and test gates are:

```sh
make -C sys/tests/usb compile
make -C sys/tests/usb test
```

Tests cover exact packed sizes and little-endian access, a HID boot-keyboard
configuration, alternate settings, every truncation of the valid
configuration, zero and undersized lengths, oversized `wTotalLength`, missing
and excess interfaces/endpoints, duplicate or invalid endpoints, descriptor
count exhaustion, and malformed string lengths.  The same test also passes
with AddressSanitizer and UndefinedBehaviorSanitizer.  The parser source has
compile-only coverage with both target MIPS GCC and PCC and includes no Ci20
header or register definition.

## Physical Mapping and DMA

Ci20 currently uses the first 256 MiB of RAM.  `layout.h` provides direct
mapping helpers:

- physical to cached KSEG0: `0x80000000 | paddr`;
- physical to uncached KSEG1: `0xa0000000 | paddr`;
- KSEG virtual to physical: `vaddr & 0x1fffffff`.

No reusable `bus_dma` or DMA mapping API exists.  The only historical generic
DMA-related code is an old network contiguous-area helper in `uipc_mbuf.c`,
which is not suitable as the USB API.

The first Ci20 DMA implementation reserves a 64 KiB linker-defined region,
aligned to 4096 bytes, physically contiguous, and below the 32-bit DMA limit.
`dmaattach()` converts its linked KSEG0 address to a physical address and
registers only the uncached KSEG1 alias with the generic allocator.  It does
not declare ordinary cached kernel buffers coherent.  Class drivers copy
between DMA payload buffers and normal kernel buffers.  This is deliberately
simple and safe; future architectures may provide a cached backend with real
range cache maintenance.

The implemented interface in `sys/include/dma.h` uses `struct dma_mem` to keep
the virtual address, 32-bit physical address, allocation size, alignment,
capabilities, and an opaque ownership cookie together.  It provides:

- `dma_pool_init`, `dma_pool_ready`, `dma_pool_size`, and
  `dma_pool_available`;
- `dma_alloc` and checked `dma_free`;
- `dma_sync_for_device` and `dma_sync_for_cpu` with explicit direction;
- a two-callback machine backend for architectures that need cache work.

`sys/kernel/subr_dma.c` uses first-fit allocation over at most 33 free ranges
and records at most 32 live allocations.  Both limits are compile-time
bounded.  Pool operations use short `splhigh` critical sections in the kernel;
the same source is compiled by the host test with no target dependencies.

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

## USB Core and Mock HCD Verification

The first core implementation preserves the classic bus, device, interface,
endpoint, pipe, transfer, and interface-driver relationships, but allocates
all objects from the fixed pools in `usb_limits.h`.  Enumeration is a single
transaction: any descriptor, transfer, address, configuration, or driver
attach error returns every partial object and address to its pool.

`usb_hcd.h` is the only controller boundary used by the core.  Its start,
stop, pipe, transfer, root-control, and poll methods are sufficient for both
the initial polling OHCI milestone and later interrupt completion.  The core
contains no Ci20 include, register value, DMA schedule, or controller type
test.

`make -C sys/tests/usb test` uses `usb_mock_hcd.c` to verify a HID boot
keyboard enumeration, address allocation and first-fit reuse, match/attach and
reverse detach, synchronous control success and STALL, asynchronous transfer
completion, explicit cancellation, one-winner completion, timeout/abort,
disconnect/reconnect, and malformed-configuration cleanup with no leaked pool
objects.  The suite passes ASan/UBSan.  `usb_subr.c`, `usb_core.c`, and the mock
HCD also compile independently with target MIPS GCC and PCC.

Detailed ownership and terminal-state rules are in
`docs/USB_ARCHITECTURE.md`.

## OHCI Control and First Periodic Milestone

The generic OHCI slice allocates one 4096-byte DMA slab containing an exact
256-byte HCCA, separate control and interrupt ED/TD records, the setup packet,
and bounded control and interrupt payloads. All hardware records use explicit
little-endian conversion and retain the classic OHCI register and
condition-code definitions.

Controller start performs revision validation, host-controller reset, HCCA
installation, frame timing setup, interrupt masking, and transition to the
operational state.  Control submission builds setup, optional data, status,
and dummy-tail TDs.  The polling completion path observes the ED head/tail,
maps OHCI condition codes, calculates short IN lengths, copies from the DMA
payload, and terminates through the common USB completion function.  Abort
sets ED skip and removes the control head before the core publishes timeout or
cancellation.

Root-port helpers provide status translation, per-port power, and reset. The
first periodic slice supports one interrupt-IN pipe, programs the HCCA table
at a normalized 1/2/4/8/16/32-frame interval, preserves the ED data-toggle
carry, and rearms an eight-byte HID transfer from the callback. The compact
root-hub layer translates RHSC into deferred port exploration and supports
direct-device attach, detach, debounce, reset, and address reuse. External
hubs, bulk, multiple periodic pipes, and a general deferred transfer
completion queue remain later slices; unsupported transfer types return an
explicit error.

`sys/tests/usb/ohci_test.c` supplies fake OHCI MMIO and a fake full-speed USB
device.  It executes the real HCCA/ED/TD schedule through the generic DMA and
USB core, including the six control requests needed to enumerate and
configure a HID boot interface. It also drives real periodic ED/TD completion,
IRQ acknowledgement, deferred RHSC, an RHSC event arriving while masked,
boot-key decoding, control traffic while the periodic pipe is active,
disconnect/reconnect, port reset, STALL, timeout, abort, and DMA reuse.
`usb_task.c`, `uhub.c`, `ohci.c`, `usb_service.c`, the Ci20 attachment, and
the init integration compile with target MIPS GCC and PCC.

### DMA Phase Verification

The host-side test is run with:

```sh
make -C sys/tests/dma test
```

It covers initialization validation, physical/virtual alignment, contiguous
address translation, zero filling, unsupported capabilities, sync callback
arguments and range checks, reinitialization with a live allocation, reuse,
double free, allocation-record exhaustion, pool exhaustion, and full-range
coalescing after free.

Both Ci20 compiler profiles linked `ci20.uImage` with the DMA service enabled.
The final images place `.dma` at a 4096-byte-aligned address with an exact
64 KiB size.  The generated service order is `creatorattach()` followed by
`dmaattach()`.  These results validate the build and link layout only; they do
not claim a Ci20 hardware run.

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

`DEV_BSIZE` is 1024 bytes, while the first storage backends use 512-byte
logical sectors. `sys/disk` translates a ReBSD block number to two backend
sectors and handles `b_bcount`, residuals, partition bounds, end of media, and
read-only errors. It must not silently reinterpret ReBSD block numbers as
512-byte sectors.

The disk layer is not part of USB. `sys/disk` owns the common `sdN` namespace,
five minors per unit (whole disk plus four primary MBR entries), media ioctls,
MBR revalidation, and the `read`/`write`/`flush`/`present` backend contract.
The USB Mass Storage driver implements only one read-only backend using BOT
and SCSI. Future SD/MMC, IDE/ATA, and SATA/AHCI drivers will implement the same
backend contract and will not depend on USB.

Filesystems sit above block devices and are configured independently of every
transport. New FAT, exFAT, and ext-family implementations belong under
`sys/fs`, not under `sys/usb` or a board directory; disabling USB must not
remove a filesystem, and disabling a filesystem must not remove a disk
transport.

There is no SCSI or SCSIPI subsystem in the current tree.  The first umass
implementation therefore contains only BOT framing and `INQUIRY`,
`TEST UNIT READY`, `REQUEST SENSE`, `READ CAPACITY(10)`, and `READ(10)` for one
LUN.  Write support is a later commit.

## Console Input

The common MIPS console owns `cnttys[0]` and feeds received characters through
`cninput()` to `ttyinput()`. The boot-keyboard driver uses this same bounded
machine-independent submission point, so it does not know about the console
implementation and no USB check is hard-wired into `cnread()`.

The first HID driver supports only boot-protocol keyboards, 8-byte reports,
modifier state, press/release tracking, and a basic US map.  A generic HID
report parser and wscons-style framework are not imported.

## Locking Model

The current MIPS kernel is effectively uniprocessor. `splbio`, `spltty`,
`splclock`, `splnet`, and `splhigh` all disable interrupts globally. USB uses
this only for short queue and state transitions. Proc0 drains tasks after
`spl0()`; the scheduler checks queue state under interrupt masking before it
sleeps on either native channel. Control transfers, descriptor parsing,
driver attach/detach, and debounce delays run with interrupts enabled. Current
deferred tasks use bounded polling/delay operations and must not sleep;
supporting arbitrary sleeping USB jobs would require a real kernel-thread
facility.

Object ownership and terminal transfer transitions will be written explicitly
in `docs/USB_ARCHITECTURE.md` before the mock HCD/core commit.  The design will
avoid assumptions that prevent later SMP locking, but full MP safety is not a
goal of the first Ci20 port.

## Test Strategy Established in Phase 0

Machine-independent tests run without USB hardware and cover descriptor
parsing, malformed descriptors, address allocation, driver matching, mock-HCD
enumeration, HID boot reports, disconnect/reconnect, timeout arbitration,
cancellation, and DMA allocator rules.

Generic USB compile tests must not define `CI20` or include JZ4780 headers.
Ci20 build gates will use both GCC and PCC.  QEMU may validate a generic
emulated OHCI/EHCI controller only when the exact machine and controller are
recorded; QEMU Malta is not evidence of JZ4780 hardware operation.

Creator Ci20 hardware success requires a captured UART log naming the board,
device, connection topology, detected speed, selected HCD, and test result. No
hardware milestone is reported from compilation or register-level inspection
alone.

## Ci20 OHCI Attachment

The machine-independent service in `usb_service.c` owns the default bounded
USB core. The generic OHCI source still knows only register callbacks, DMA,
and the HCD contract. Ci20-specific code is split into a fake-register
testable `usb_hw.c` sequence and `usb.c`, which supplies KSEG1 MMIO, GPF15
VBUS, delays, IRQ 5 dispatch, and board diagnostics. Root-port policy is now
owned by the machine-independent `uhub.c` layer.

The board sequence selects the shared OTG PHY as the 48 MHz UHC source,
ungates UHC, configures port-1 reference clock/pulldowns/UTMI width, releases
forced suspend, toggles PHY POR, and pulses UHC reset. It then starts OHCI at
`0x134a0000`. The generic root-hub layer powers port 1, enumerates an initial
full-/low-speed device when present, and leaves RHSC armed for a later attach
or detach. Every register and board-wiring source is recorded in
`docs/CI20_USB.md`.

Host tests cover successful sequencing and a stuck `UHCCDR_BUSY` path. Real
Ci20 hardware has verified VBUS, clocks/PHY/reset, low-speed root-port reset,
OHCI control enumeration, descriptor parsing, TCU3 delays, and coexistence
with DM9000 networking. A real-board test on 2026-07-13 also verified OHCI IRQ
5, one periodic interrupt-IN pipe, the boot-keyboard class driver, and keyboard
input at the live ReBSD console using the low-speed `1c4f:0002` device.
Post-boot detach/reconnect, the proc0 deferred runner, and the RHSC path were
verified on Creator Ci20 on 2026-07-13 with both a preconnected keyboard and a
keyboard first attached after boot. The successful raw captures and exact
image digest are recorded in `docs/USB_TESTING.md`.

## Ci20 EHCI Attachment

The machine-independent EHCI driver uses one fixed 16 KiB coherent DMA slab
for its 1024-entry periodic frame list, asynchronous head, bounded queue heads
and qTDs, setup packet, and an 8 KiB transfer bounce buffer. It implements
high-speed root-hub enumeration and one active control or bulk transfer, with
polling and interrupt completion, short packets, toggle tracking, STALL,
timeout, abort, and deterministic schedule reclamation. Split transactions,
isochronous transfers, and periodic endpoint scheduling are not implemented.

The Ci20 layer attaches EHCI at `0x13490000` on IRQ 20 before OHCI. It owns the
shared JZ4780 VBUS, clock, PHY, and UHC reset sequence and programs the
EHCI-local UTMI width bit both before the shared reset and again after the
generic EHCI controller reset, matching the JZ4780 Linux sequence. A direct
high-speed device remains on EHCI; a direct low- or full-speed device is
handed to the OHCI companion with the EHCI Port Owner bit and reclaimed after
OHCI detach.

Host tests exercise the real generic schedule against fake EHCI MMIO and a
fake high-speed device, including bulk IN/OUT, short packets, STALL, timeout,
abort, busy submission, low-speed companion handoff, change-bit draining,
reclaim, and a later high-speed attach. Fake Ci20 hardware tests also cover
both UTMI-width writes and readback failure. These tests and the Ci20 GCC/PCC
compile and link gates pass. Creator Ci20 testing additionally verifies a
direct high-speed mass-storage-class device, detach/reconnect, and low-speed
keyboard handoff to OHCI.

The first Creator Ci20 run verified controller startup, EHCI capabilities,
IRQ 20, empty-port hotplug, and low-speed companion handoff, but EHCI control
enumeration returned `I/O error`. Review found two fake-MMIO blind spots: the
core had used an 8-byte default-control maximum packet at high speed instead
of the required 64, and QH Current qTD carried a terminate bit in a reserved
field. The second candidate fixes both, asserts them in the EHCI host test,
and records bounded QH/qTD diagnostics for the next hardware run.

The second run showed `PORTSC=0x1005` and qTD token `0x00080248`: high-speed
reset and schedule fetch succeeded, but the first SETUP exhausted all retries
without transferring its eight bytes. The third candidate therefore applies
the existing reset-recovery delay before enumeration and the address-settle
delay before traffic at a newly assigned address. The fake HCD records reset
release, first control execution, SET_ADDRESS, and the following request, and
asserts both minimum intervals.

Later diagnostics proved the controller fetched the QH, all qTDs, and the
correct SETUP bytes, but received a transaction error on the wire. FreeBSD and
NetBSD JZ4780 platform code both enable `USBPCR1.WORD_IF0` together with
`WORD_IF1`; adding the missing shared-PHY width bit produced
`USBPCR1=0x8ace3370` and passed the real high-speed gate. The verified image
SHA-256 and raw captures are recorded in `docs/USB_TESTING.md`.
