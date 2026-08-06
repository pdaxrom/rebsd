# Creator Ci20 USB Host Bring-up

This document records every JZ4780/Ci20-specific fact used by the first ReBSD
USB host attachment. The generic USB core and OHCI/EHCI HCDs do not include
these register definitions; they enter through the HCD and DMA interfaces.

## Verified Polling Hardware Gate

The first hardware gate was polling-only and control-only:

1. enable Ci20 host VBUS through GPF15;
2. select the shared OTG PHY as the 48 MHz UHC clock, wait for the divider
   change, and ungate UHC;
3. configure PHY port 1, release forced suspend, toggle PHY POR, and pulse the
   UHC soft reset;
4. start OHCI at physical address `0x134a0000` with interrupts disabled;
5. power and reset root port 1;
6. enumerate one full- or low-speed device that is present at boot;
7. print its numeric vendor/product IDs and every interface's class tuple.

This gate was verified on a real Ci20 with a low-speed `1c4f:0002` composite
HID device. It proved VBUS, clocks, PHY, controller MMIO, the DMA schedule,
endpoint-zero control traffic, address assignment, descriptor parsing, and
configuration. DM9000 DHCP and Internet traffic continued to work after USB
enumeration.

## Verified HID Keyboard Gate

The first periodic slice adds keyboard input without putting JZ4780 details
into the USB core or class driver:

1. the generic HID boot-keyboard driver sends `SET_PROTOCOL` and `SET_IDLE`,
   then keeps one eight-byte interrupt-IN transfer armed;
2. generic OHCI owns one independent periodic ED/TD region, programs the HCCA
   interrupt table, preserves data toggle, and acknowledges writeback-done;
3. the Ci20 attachment unmasks JZ4780 IRQ 5 only after a periodic transfer has
   been installed and forwards it to the generic OHCI handler;
4. the keyboard decoder submits a bounded basic-US character sequence through
   the common console input function.

The gate was verified on a real Ci20 on 2026-07-13 with the low-speed
`1c4f:0002` composite device. Interface 0 attached as an eight-byte boot
keyboard at a 10 ms interval, JZ4780 IRQ 5 was enabled, and keyboard input was
used at the live ReBSD console to log in as root and run `ls`.

That UART log verifies a keyboard already present during boot. It does not by
itself verify the deferred root-hub path described below.

## Verified Root-Hub Hotplug Gate

The verified implementation replaces the Ci20 boot-only port policy with
a machine-independent root-hub layer adapted from NetBSD 3.1 `uhub.c`:

1. generic OHCI translates root-port state and change bits through HCD ops;
2. IRQ 5 acknowledges and masks RHSC, then queues one coalescing USB task;
3. the existing ReBSD proc0 scheduler loop performs debounce, reset,
   enumeration, driver attach/detach, and address reuse outside IRQ context;
4. RHSC is reenabled without clearing a change that arrived while masked;
5. the Ci20 file owns only MMIO, IRQ routing, VBUS/PHY setup, and diagnostics.

Host tests cover an initially absent device, deferred disconnect/reconnect,
rapid unplug/replug coalescing, address-1 reuse, keyboard transfer abort, a
second RHSC event while masked, and keyboard input after reconnect. The proc0
runner and RHSC path were verified on Creator Ci20 on 2026-07-13. One UART
capture boots with the keyboard connected and proves disconnect, reconnect,
address reuse, and working input after reconnect. A second capture boots with
an empty port, attaches after the login prompt, and survives more than five
detach/reconnect cycles with working keyboard input and no panic.

The first hardware candidate (`ci20.uImage` SHA-256 `40e15f420336d89088439ac0f2e7831eaa94908f40b70be7624f6bdfe7b9b887`)
failed on disconnect. Its worker was a permanent child of init, but the legacy
init performs a startup `shutdown()`/`wait()` pass over its children. That
pass timed out, and a later RHSC wakeup encountered the invalid sleeper and
panicked. The corrected candidate creates the worker from `proc0` after PID 1
has been reserved for init, so the worker is normally PID 2 with parent PID 0.

The second candidate (`ci20.uImage` SHA-256 `33e203e3ade3d4a60fcfa81bbd7187c2f5fc7aa751ea91e49b408a9a90c01747`)
fixed that panic but still failed to detach. On real hardware the keyboard TD
reported WDH/NOT_RESPONDING before the root hub asserted RHSC. The WDH error
path disabled the global OHCI MIE bit, so the later RHSC remained pending
without an IRQ.

The third candidate (`ci20.uImage` SHA-256 `defcb353ce302f0673dc0685d1196224b3798ffa89bc2d8c21e03c671f52720b`)
tried to leave MIE enabled while RHSC was armed. It reached the login prompt,
but neither UART nor USB input worked; this is consistent with IRQ5
starvation, although that image did not yet have a storm register dump.

The verified candidate instead masks WDH, RHSC, and MIE on a periodic error and
directly queues a deferred root-port probe.

The fourth candidate (`ci20.uImage` SHA-256 `b86963ff8bc2c1193c8f3fe3ff6f95b9151e5ff4553c05e1853212460be8ae79`)
proved that the remaining wakeup panic was not fixed by OHCI interrupt policy.
It accepted UART and keyboard input at login, then unplug printed the periodic
I/O error and a subsequent wakeup panicked. PID 2 was absent from `ps` even
though its ready line had printed: the `newproc()`-based SSYS child had left a
stale hashed-sleep-queue entry.

The verified candidate creates no USB process. ReBSD `newproc()` is a
swappable user-process constructor, not a kernel-thread API. The fixed task
queue is drained by the existing proc0 swapper loop, which wakes on its native
`runin`/`runout` channels and rechecks the USB queue before sleeping. Proc0
reenables RHSC/MIE
after inspecting the port, and a later RHSC stays pending while masked. A
fake-MMIO regression injects WDH error and RHSC as separate events. Ci20 also
quarantines IRQ5 after more than 32 deliveries in one 10 ms clock interval,
printing the OHCI status, enable, control, and port registers while leaving
UART and the rest of the system usable. All failed UART captures are retained
under `docs/usb-logs/`.

The successful captures are
`docs/usb-logs/ci20-ohci-hotplug-proc0-preconnected-20260713.txt` and
`docs/usb-logs/ci20-ohci-hotplug-proc0-boot-empty-20260713.txt`. They correspond
to `ci20.uImage` SHA-256
`5d2b3249bd83331e7a3e871de33fb9fb58db7a22a44c6d82665ac005118695fa`.

## Ci20 USB Connector Topology

Creator Ci20 does not wire its two USB-A sockets as two ports of the OHCI root
hub. The current controller reports one root port, matching the board wiring:

- right-hand USB-A J23 is connected to the UHC/EHCI host block; EHCI is
  hardware-verified for USB 2.0 high speed and OHCI is its hardware-verified
  USB 1.x companion;
- left-hand USB-A J24 is paralleled with mini-OTG J8 and is a separate OTG
  connection; J24 and J8 must not be used at the same time;
- JP2 selects VBUS behavior for the OTG connection.

Consequently, adding generic EHCI at `0x13490000`, IRQ 20 will add USB 2.0
high-speed service to J23 but will not activate J24. Supporting J24 as a host
is a separate DWC2 host-mode task at `0x13500000`, IRQ 21, including OTG PHY,
role, and VBUS/JP2 policy. DWC2 is outside the first OHCI/EHCI host-port scope.

## Verified EHCI Host Gate

The verified Phase 9 implementation attaches generic EHCI before its OHCI
companion. The two buses share one machine-independent USB core, but keep
separate HCD schedules, root hubs, address maps, and IRQs. Ci20 platform code
performs the shared VBUS/clock/PHY/reset sequence once, selects the
controller-side 16-bit UTMI interface, then starts EHCI at physical
`0x13490000` and OHCI at `0x134a0000`.

The JZ4780-specific EHCI additions are:

| Item | Value and reason | Source |
| --- | --- | --- |
| controller MMIO | physical `0x13490000`, uncached KSEG1 `0xb3490000` | Ci20 Linux DTS and NetBSD Ingenic register map |
| interrupt | INTC IRQ 20, unmasked only after the root hub is ready | Ci20 Linux DTS and NetBSD `apbus.c` |
| controller UTMI width | EHCI-local offset `0xb0`, physical `0x134900b0`, set bit 6 before generic start and repeat it after `HCRESET` for 16-bit UTMI | Ci20 Linux sets it in the CGU start path and repeats it after `usb_add_hcd`; exact files and revision below |
| companion policy | EHCI Port Owner clear for high speed; set for direct low/full speed; clear again after OHCI detach | EHCI specification behavior as adapted from NetBSD 3.1 `ehci.c`; no JZ register write |

The generic driver owns controller reset, periodic and asynchronous schedule
addresses, high-speed port reset, control/bulk completion, and Port Owner.
The Ci20 layer owns only MMIO callbacks, the three hardware facts above, shared
PHY/VBUS setup, IRQ dispatch, and diagnostics. IRQ 20 has the same per-tick
32-delivery quarantine used for IRQ 5; any `ehci0: irq storm quarantined` line
is a failed gate, with the printed status/enable/command/port values retained.

The first candidate, SHA-256 `7f6df5c7...`, booted on Creator Ci20 and
verified EHCI capabilities, IRQ 20, empty-port hotplug, and low-speed Port
Owner handoff to the working OHCI keyboard path. EHCI control enumeration
still returned `I/O error`, so that image did not pass the high-speed gate.

The second candidate fixed the high-speed default control endpoint maximum
packet size and reserved Current-qTD bits. Its hardware diagnostic decoded as
an enabled high-speed port (`PORTSC=0x1005`) followed by the very first SETUP
qTD exhausting all three transactions with eight bytes still outstanding
(`0x00080248`). The data and status qTDs remained active and untouched. This
proved that the controller reads the asynchronous schedule, but the device
was not yet responding after reset.

The third candidate waited the existing `USB_PORT_RESET_RECOVERY`
interval between high-speed reset and the first SETUP, and observes
`USB_SET_ADDRESS_SETTLE` before the first request at the new address. The fake
EHCI test asserts both temporal relationships. Failure output now also names
the last USB request and device address. Its `ci20.uImage` SHA-256 was
`f21e339d70fb100f9e4215cbaf9603692b42965fe66cb2ea8637c0dbd7901945`.
Hardware produced the same transaction error on the first address-zero
`GET_DESCRIPTOR` SETUP, so reset recovery timing was not the cause.

Cross-checking current NetBSD, OpenBSD, FreeBSD, and Linux found no remaining
descriptor-format difference that explained the first-SETUP transaction
error. Later candidates mirrored the active Linux UHC clock path and added
complete QH/qTD diagnostics. They still failed the first SETUP and therefore
ruled out clock selection, reset recovery, descriptor layout, and DMA
schedule visibility.

Comparing the final register sequence with both FreeBSD and NetBSD exposed
the missing `USBPCR1.WORD_IF0`: JZ4780 requires both `WORD_IF0` and
`WORD_IF1` together with the controller-local UTMI-width bit. The verified
image reached final `UHCCDR=0x60000018` and `USBPCR1=0x8ace3370`.

On Creator Ci20, a direct `1005:b113` flash drive attached at high speed as a
class-8/subclass-6/protocol-80 device with two endpoints, detached, and
reattached at address 1. Replacing it with the `1c4f:0002` low-speed keyboard
verified EHCI-to-OHCI ownership handoff, console input, detach, and reconnect.
The image SHA-256 is
`05adb071ddd9b12f584192e80542173b7ad91cb885f99d6a3f25c98b284025fa`.
The retained captures are
`docs/usb-logs/ci20-ehci-high-speed-verified-20260713.txt` and
`docs/usb-logs/ci20-ehci-companion-verified-20260713.txt`.

## External High-Speed Hub Gate

The machine-independent `uhub` driver supports a bounded high-speed external
hub, powers and explores its downstream ports, and keeps one interrupt-IN
status transfer armed. Low/full-speed children carry the parent hub address,
downstream port, and transaction-translator metadata into generic EHCI; no
Ci20 policy is present in the hub or split-transaction paths.

The hardware topology used a `214b:7000` four-port high-speed hub with a
high-speed `1005:b113` flash drive and low-speed `1c4f:0002` boot keyboard.
The 2026-07-15 reconnect candidate had SHA-256
`861ae15e0ba79af9a2a6364c66f593656fcbf8c6274626b49425f437002e66fa`.

Earlier candidates rebuilt the complete periodic schedule around a QH update
or rearmed a completed split QH while it was still visible to the controller.
On JZ4780 this eventually produced `MISSEDMICRO`, dead keyboard input, and
unreliable child or complete-hub reconnects. The final implementation links
and unlinks periodic QHs atomically without cycling PSE. A split QH is
unlinked before its single fixed qTD and overlay are rearmed, and linked back
only after the new transaction is fully published. Root-device removal also
tears down hub children recursively before releasing the hub address.

That image passed its original hardware gate on 2026-07-15. Its full UART capture
shows simultaneous keyboard and storage enumeration, repeated child recovery,
five complete populated-hub disconnect/reconnect cycles, working console
input, and a final read-only FAT32 mount. It is retained as
`docs/usb-logs/ci20-ehci-hub-reconnect-verified-20260715.txt`. Earlier
complete captures remain failure evidence and are not presented as proof of
the current implementation. Later cold boots exposed intermittent dormant
keyboard qTDs, and a second complete-hub reconnect exposed a race which left
new periodic QHs published while PSE was disabled.

The final 2026-07-16 image has SHA-256
`5c88e85e1cf9b3fc02802a77054fc3682625f18c168ba8a0d0de8ac9e9c7be05`.
It uses three Linux-style complete-split windows, publishes a split QH outside
the JZ4780 prefetch window, rescans interrupt qTDs with a 100 ms watchdog, and
tracks periodic-QH count/generation so a late unlink cannot leave a rebuilt
schedule stopped. The retained cold-boot capture contains seven complete
populated-hub reconnects. One reconnect reproduced the PSE race; the driver
printed `ehci: recovered periodic schedule with 2 QHs`, restored `USBCMD` to
`0x31`, and keyboard and storage operation continued. The capture is
`docs/usb-logs/ci20-ehci-periodic-recovery-verified-20260716.txt`.

## Known JZ4780 WAIT Workaround and Follow-up

USB storage write stress exposed a board-level idle failure which initially
looked like a dead EHCI or filesystem path. Both the USB keyboard and UART
stopped accepting input, disconnecting or reconnecting USB devices produced
no log output, and the machine printed no panic. An FT2232D/XBurst JTAG halt
followed by resume restored both UART and USB operation. This distinguishes
the failure from an EHCI-only stall and is consistent with the JZ4780 core
remaining asleep across a lost interrupt wakeup. The FT2232D wiring, patched
OpenOCD-XBurst build, one-shot probes, persistent debug-server workflow, and
safe snapshot procedure are documented in `tools/ci20/README.md`.

The Ci20 scheduler enters `idle()` with interrupts disabled. The old code
called `spl0()` and then executed `WAIT`; an interrupt becoming pending around
that transition can leave XBurst1 asleep with `Cause.IP2` pending. The current
Ci20-specific workaround in `sys/mips/ci20/machdep.c` keeps the short
interrupt-enabled idle window but executes `nop` instead of `WAIT`. The
intentional `WAIT` in the final halt/reboot loop is not affected. This avoids
lost TCU wakeups at the cost of higher idle CPU and board power consumption.

The merged GCC image with SHA-256
`f8efa97ac06a4dda0db46e9f17d0c18c4f74b6bc590f10857a7e15395d44535d`
passed its Ci20 hardware gate on 2026-07-17 with this workaround present.

Reenabling low-power `WAIT` remains explicit future work. Do not remove the
workaround until all of these items are complete:

1. capture CP0 Status/Cause and the JZ4780 INTC/TCU pending state immediately
   before and after the idle transition, using the checked-in XBurst tools;
2. implement a JZ4780-specific, race-free idle sequence with the required CP0
   interrupt ordering and execution-hazard barriers, rather than changing the
   generic USB, disk, or scheduler paths;
3. pass repeated cold and warm boots with a populated high-speed hub, keyboard
   input, FAT write/overwrite/sync traffic, UART input, and child plus complete
   hub reconnects without using JTAG halt/resume as recovery;
4. compare idle current and CPU utilization before and after restoring
   `WAIT`, so the low-power benefit is measured rather than assumed.

## Programming Manual Cross-check

The checked-in `docs/JZ4780_pm.pdf` is 16,628,911 bytes with SHA-256
`c31707c6975dfb73268fa25632e298169f311a47e10409961236ba981e02bae5`.
The PDF page index is 34 pages greater than the printed manual page number in
this part of the document.

| Register | Address and fields used | Manual location |
| --- | --- | --- |
| `UHCCDR` | `0x1000006c`; source in 31:30, `CE_UHC` 29, `UHC_BUSY` 28, `UHC_STOP` 27, divider 7:0; the hardware candidate uses MPLL / 25 for 48 MHz as Ci20 Linux does | PDF 453-454, printed 419-420, section 18.1.2.11 |
| `CLKGR0` | `0x10000020`; bit 24 set stops UHC, clear supplies its clock | PDF 484-485, printed 450-451, section 18.2.2.9 |
| `OPCR` | `0x10000024`; bit 6 `SPENDN1`, set means port 1 is not forced into suspend | PDF 489-490, printed 455-456, section 18.2.2.16 |
| `USBPCR` | `0x1000003c`; bit 22 PHY `POR`, bit 21 `SIDDQ`, bit 20 `OTG_DISABLE` | PDF 462-463, printed 428-429, section 18.1.2.22 |
| `USBPCR1` | `0x10000048`; reference source 27:26, reference frequency 25:24, port-1 D-/D+ pulldowns 23/22, port-1 reset 20, shared PHY UTMI widths `WORD_IF0`/`WORD_IF1` 19/18 | PDF 465-466, printed 431-432, section 18.1.2.25 |
| `SRBC` | `0x100000c4`; bit 14 `UHC_SR` | PDF 486-487, printed 452-453, section 18.2.2.11 |
| EHCI local UTMI register | `0x134900b0`; bit 6 selects the controller's 16-bit UTMI interface | not documented in the programming manual; exact Ci20 Linux files recorded below |

The manual overview identifies the integrated host as OHCI/USB 1.1 compatible
with an embedded USB 2.0 PHY. The manual does not define the Ci20 board's VBUS
GPIO or the OHCI/EHCI memory map, so those come from the sources below.

## Exact Secondary Sources

No Linux or modern NetBSD USB-core code is imported. These sources contribute
only SoC addresses, bit meanings, board wiring, or ordering cross-checks.

| Source | Exact revision and use |
| --- | --- |
| NetBSD `ingenic_regs.h` | commit `5f9bb4f96149a41b56de05d37caf547d9700b435`, RCS 1.28; CPM fields and OHCI `0x134a0000` / EHCI `0x13490000` bases: <https://github.com/NetBSD/src/blob/5f9bb4f96149a41b56de05d37caf547d9700b435/sys/arch/mips/ingenic/ingenic_regs.h> |
| NetBSD `apbus.c` | commit `22265e4b013b9496c7197110393955e714051a68`, RCS 1.23; UHC gate, `SPENDN1`, OHCI IRQ 5, EHCI IRQ 20: <https://github.com/NetBSD/src/blob/22265e4b013b9496c7197110393955e714051a68/sys/arch/mips/ingenic/apbus.c> |
| NetBSD `ingenic_ohci.c` | commit `c7fb772b85b2b5d4cfb282f868f454b4701534fd`, RCS 1.7; OHCI base and 0x1000 register window: <https://github.com/NetBSD/src/blob/c7fb772b85b2b5d4cfb282f868f454b4701534fd/sys/arch/mips/ingenic/ingenic_ohci.c> |
| CI20 Linux `ci20.dts` | MIPS/CI20_linux commit `7dff33297116643485ca37141d804eddd793e834`; OHCI and EHCI use GPF15 for VBUS: <https://github.com/MIPS/CI20_linux/blob/7dff33297116643485ca37141d804eddd793e834/arch/mips/boot/dts/ci20.dts> |
| CI20 Linux `jz4780.dtsi` | same commit; OHCI base/IRQ `0x134a0000`/5, EHCI base/IRQ `0x13490000`/20, and DWC2 OTG base/IRQ `0x13500000`/21: <https://github.com/MIPS/CI20_linux/blob/7dff33297116643485ca37141d804eddd793e834/arch/mips/boot/dts/jz4780.dtsi> |
| CI20 Linux `ehci-jz4780.c` | same commit; writes bit 6 of `EHCI_REG_UTMI_BUS` after adding the HCD to select the 16-bit controller interface: <https://github.com/MIPS/CI20_linux/blob/7dff33297116643485ca37141d804eddd793e834/drivers/usb/host/ehci-jz4780.c> |
| CI20 Linux CGU source/header | same commit; selects MPLL as the UHC parent, requests 48 MHz, configures the PHY reference, `SPENDN1`, port-1 pulldowns/16-bit UTMI, one-millisecond PHY POR, and 300-microsecond UHC reset pulse: <https://github.com/MIPS/CI20_linux/blob/7dff33297116643485ca37141d804eddd793e834/drivers/clk/jz47xx/jz4780-cgu.c>, <https://github.com/MIPS/CI20_linux/blob/7dff33297116643485ca37141d804eddd793e834/arch/mips/include/asm/mach-jz4740/jz4780-cgu.h> |
| NetBSD Ci20 EHCI initialization | commit `1ae1c2335b4b975da841c91c1a3210d617daf618`; adds the JZ4780 host-PHY sequence, including both `WORD_IF0` and `WORD_IF1`: <https://github.com/NetBSD/src/commit/1ae1c2335b4b975da841c91c1a3210d617daf618> |
| FreeBSD JZ4780 EHCI/CGU | stable/13 before MIPS removal; selects the 48 MHz PHY reference, enables both UTMI interface-width bits, pulses PHY POR, and pulses the shared UHC reset using the same short timing as Ci20 Linux: <https://github.com/freebsd/freebsd-src/blob/stable/13/sys/mips/ingenic/jz4780_clock.c>, <https://github.com/freebsd/freebsd-src/blob/stable/13/sys/mips/ingenic/jz4780_ehci.c> |
| CI20 U-Boot `pll.c` | MIPS/CI20_u-boot commit `ef995a1611f0446a0b670ded9ec2609cb6dc51b7`; selects `OTG_PHY` for `UHCCDR`: <https://github.com/MIPS/CI20_u-boot/blob/ef995a1611f0446a0b670ded9ec2609cb6dc51b7/arch/mips/cpu/xburst/jz4780/pll.c> |
| CI20 U-Boot `ci20.c` | same commit; drives GPF15 high as `SYS_POWER_IND` / VBUS on: <https://github.com/MIPS/CI20_u-boot/blob/ef995a1611f0446a0b670ded9ec2609cb6dc51b7/board/imgtec/ci20/ci20.c> |
| Creator Ci20 quick start guide | January 2016 connector table: J23 is the right-hand host connector; J24 is the left-hand connector paralleled with mini-OTG J8; JP2 controls OTG VBUS: <https://docs.rs-online.com/e2ba/0900766b815516a3.pdf> |

ReBSD explicitly reprograms all required state and does not depend on U-Boot
having left the clock, suspend, or GPIO registers configured.

## EHCI/OHCI Routing and Keyboard Hardware Test

Build the GCC Ci20 image with the normal project command:

```sh
make -C sys/mips BOARD=ci20 \
    KERNEL_COMPILER=gcc USERLAND_COMPILER=gcc kernel
```

The object profile places the result at
`../rebsd-usb-support-build/ci20-kgcc-ugcc-mips32r2-hard-little-elf/obj/sys/mips/ci20/ci20.uImage`.
The final FAT maintenance-tools image is a clean full GCC build with a 32 MiB
rootfs. Its `ci20.uImage` SHA-256 is
`0b357a655e984a18161f7355b6b362e912fa83b7908e0778a3d8657b16555720`.

Boot the image with the existing Ci20/U-Boot procedure while capturing UART4.
Use only the right-hand J23 type-A host port. J24/J8 is the separate OTG block
and must remain empty during this test.

With J23 empty, success first reaches lines equivalent to:

```text
dma: Ci20 uncached pool phys=... size=65536 align=4096
disk: block layer ready, MBR partitions
usb0: initializing core
usb0: core ready
ukbd0: HID boot-keyboard driver ready
umass0: SCSI/Bulk-Only driver ready
uhub0: external hub driver ready
ehci0: attach, EHCI phys=13490000
usb-host: init: enable VBUS
usb-host: init: settle VBUS
usb-host: init: configure UHC clock
usb-host: init: ungate UHC clock
usb-host: init: configure host PHY
usb-host: init: configure EHCI UTMI width
usb-host: init: pulse PHY reset
usb-host: init: pulse UHC reset
usb-host: init: hardware ready
usb-host: Ci20 VBUS on, cpm clkgr0=... opcr=... usbpcr=... usbpcr1=... uhccdr=... srbc=...
ehci0: JZ4780 UTMI bus=... width=16-bit
ehci0: EHCI version=100 ports=1 companions=.../... async-control/bulk periodic-interrupt-IN split-transactions
ehci0: irq 20 enabled for async/periodic/root-hub changes
ehci0: port1 powered, no high-speed device; hotplug ready
ohci0: attach, OHCI phys=134a0000
ohci0: OHCI revision=10 ports=1 control-polling periodic-interrupt-IN
ohci0: irq 5 enabled for periodic/root-hub changes
ohci0: port1 powered, no device; hotplug ready
usb0: deferred task runner uses proc0
```

Run the routing cases separately and retain the complete UART capture:

1. Connect a known USB 2.0 high-speed flash drive directly to J23. It must
   remain on EHCI and print `umass0` inquiry text followed by `sd0` capacity.
   A valid classic MBR additionally prints any bounded `sd0a` through `sd0d`
   entries. There must be no OHCI handoff for this device.
2. Run `fdisk -p /dev/sd0`, then
   `dd if=/dev/sd0 of=/dev/null bs=1024 count=16`. If the MBR reports a first
   partition, also run
   `dd if=/dev/sd0a of=/dev/null bs=1024 count=16`.
3. Unplug the idle flash drive. The log must report `sd0: detached`,
   `umass0: detached`, and the EHCI port disconnect without a panic. Reconnect
   it and repeat the `fdisk` and whole-disk `dd` commands.
4. Unplug the flash drive and connect the verified low-speed boot keyboard.
   EHCI must print `port1 handoff to ohci0 speed=low`; OHCI must then enumerate
   it and `ukbd0` must attach. A full-speed direct device, if available, must
   take the same route with `speed=full`.
5. Use the USB keyboard rather than UART input to enter `echo usb-ok`, and
   check Shift, Backspace, and Ctrl-C.
6. Confirm there was no `Something is hung` warning, the proc0 runner line
   appeared, and `ps axl` shows init as PID 1 with no `usbtask` process.
7. Unplug the keyboard and wait for `ukbd0: detached`,
   `ohci0: port1 device disconnected`, and
   `ehci0: port1 reclaimed from ohci0`.
8. Reconnect the keyboard and wait for a fresh EHCI-to-OHCI handoff, `ukbd0`
   attach, and numeric descriptors. Type `echo usb-reconnected` through it.
9. Repeat keyboard unplug/reconnect and typing at least five times, including
   one quick unplug/replug, and confirm both UART and USB remain responsive.

The read-only storage procedure above passed on 2026-07-13 with direct
high-speed device `1005:b113`. The board reported 30,299,520 sectors and MBR
partition type `0x0c`; 16 KiB reads from both `/dev/sd0` and `/dev/sd0a`
succeeded before and after idle detach/reconnect. The raw UART capture is
`usb-logs/ci20-umass-readonly-verified-20260713.txt`.

The later FAT32 gate mounted `/dev/sd0a` read-only on `/mnt`, listed short and
long names, traversed nested directories, and rejected `touch` with
`Read-only file system`. Its retained capture is
`usb-logs/ci20-fat32-readonly-verified-20260713.txt`. A separate 16 MiB file
read completed in the same hardware session, but that short excerpt is not in
the retained full capture.

### Writable Mass-Storage Hardware Test

Run destructive raw-device tests only in a sector range already proven to be
unused, and save that exact range before writing it. For the verified
`1005:b113` medium, MBR partition 1 starts at sector 63; the test therefore
used whole-disk sectors 32 through 61 (15 ReBSD blocks):

```sh
dd if=/dev/sd0 of=/var/usb-gap.before bs=1024 skip=16 count=15
dd if=/bin/sh of=/var/usb-gap.pattern bs=1024 count=15
dd if=/var/usb-gap.pattern of=/dev/sd0 bs=1024 seek=16 count=15
sync
dd if=/dev/sd0 of=/var/usb-gap.after bs=1024 skip=16 count=15
cmp /var/usb-gap.pattern /var/usb-gap.after
dd if=/var/usb-gap.before of=/dev/sd0 bs=1024 seek=16 count=15
sync
dd if=/dev/sd0 of=/var/usb-gap.restored bs=1024 skip=16 count=15
cmp /var/usb-gap.before /var/usb-gap.restored
mount -t fat /dev/sd0a /mnt
ls /mnt
umount /mnt
```

This raw-block gate passed through the populated high-speed hub on 2026-07-15: both
`cmp` commands succeeded, the original bytes were restored, and the FAT32
partition mounted and listed normally afterward. The exact UART capture is
`usb-logs/ci20-umass-write-verified-20260715.txt`. It verifies raw block write
and flush behavior. Subsequent gates mounted FAT32 read-write and verified
file create/overwrite/truncate/remove, nested directory create/remove,
same-directory file and non-empty-directory rename, remount persistence, and
strict `-r` behavior. The same storage stack passed the 64-bit seek smoke with
both target compilers, and `mkfs.fat`/`fsck.fat` passed on both a scratch FAT16
image and the existing FAT32 partition. The retained command transcript is
`usb-logs/ci20-storage-filesystem-verified-20260715.txt`.

For the external-hub gate, connect the high-speed hub with the flash drive and
keyboard already attached. Expected lines include:

```text
uhub0: 4 ports, high-speed hub addr=1, powered
uhub0: port... device attached speed=high addr=... vendor=1005 product=b113
ehci: periodic addr=... endpoint=81 ... smask=... cmask=...
ukbd0: boot keyboard, interrupt in 0x81, 8 bytes every 10 ms
uhub0: port... device attached speed=low addr=... vendor=1c4f product=2
```

Run a long raw-device or FAT file read while typing through the keyboard.
Reconnect each child repeatedly, then reconnect the complete populated hub at
least five times. Each disconnect must detach only the affected topology and
each reconnect must produce a fresh attach and working I/O. A transfer error
caused by physical removal is acceptable only when it is followed by detach
and successful fresh enumeration. A spontaneous periodic failure, missing
reconnect notification, stale `sdN`, panic, or loss of UART is a failed gate.

A low-speed keyboard already present at boot must produce the ownership and
attach sequence below before the login prompt:

```text
ehci0: port1 handoff to ohci0 speed=low
ohci0: attach, OHCI phys=134a0000
ukbd0: boot keyboard, interrupt in 0x81, 8 bytes every 10 ms
ohci0: port1 device attached speed=low
ohci0: usb addr=1 vendor=... product=... config=1 interfaces=...
ohci0: irq 5 enabled for periodic/root-hub changes
```

Also boot once with J23 empty and connect the keyboard only after the login
prompt. It must take the same handoff path and provide working input.

Any `ehci0: irq storm quarantined ...` or
`ohci0: irq storm quarantined ...` line is a failed USB gate. Record the four
register values. The affected USB IRQ is disabled deliberately, but UART
input must remain usable for diagnostics.

The early `init` markers are printed before each potentially faulting MMIO
group, so the last marker identifies the operation to inspect if the board
stops before the full register dump. An EHCI `controller start failed` line
also prints the capability and structural registers. `root hub start failed`
narrows a fault to the root-port HCD contract or port power; an enumeration
error means endpoint-zero traffic started and its printed USB status is the
next diagnostic.

Ci20 microsecond delays use dedicated TCU1 channel 3 at 750 kHz. Channel 0
remains the 100 Hz system clock. The JZ4780 manual documents the independent
16-bit channels and clock divisors on PDF pages 494-498 (printed pages
460-464), and the automatic `TCNT` wrap-and-continue behavior on PDF page 499
(printed page 465). Hardware bring-up showed that the previous CP0 Count based
delay did not complete, so USB and DM9000 now share the bounded TCU3 backend.

## Verification Without Hardware

`make -C sys/tests/usb test` executes descriptor, core/mock-HCD, deferred-task,
root/external-hub, boot-report decoder, full fake-OHCI control/periodic/RHSC
scheduling, compact fake-EHCI asynchronous/periodic/split scheduling and
routing, fake-JZ4780 register sequencing, and BOT/SCSI tests.
`make -C sys/tests/disk test` covers the common 64-bit disk/MBR/GPT layer,
busy revalidation and byte-exact `fdisk` ABI. `make -C sys/tests/gpt test`
uses a 3 TiB sparse image to cover both-copy CRCs, fallback, explicit repair,
valid-but-different copies, protective-prefix preservation and overlap rejection;
`make -C sys/tests/fat test` covers FAT parsing, reads, writes,
allocation, truncation, removal, directory mutation, and rename. The
`sys/tests/fsck_fat` and `sys/tests/mkfs_fat` suites cover clean/corrupt images,
repair policy, FAT copies, cluster chains, FAT32 metadata, and format geometry.
The fake OHCI test covers IRQ
acknowledgement, data-toggle carry, rearm, simultaneous control traffic,
masked-RHSC delivery, and deferred disconnect/reconnect. The fake EHCI test covers controller startup, schedule
alignment, high-speed enumeration, bulk IN/OUT, short transfers, toggle,
stall, automatic timeout, abort, busy submission, single completion,
periodic interrupt-IN, transaction-translator split masks, atomic periodic QH
link/unlink, external-hub removal/reconnect, companion handoff/change
draining/reclaim, and a later high-speed attach.
The Ci20 sequence test verifies VBUS-first ordering, `UHCCDR_BUSY` timeout,
final gate/suspend state, PHY POR, and UHC reset deassertion. Full GCC and PCC
kernel/rootfs builds link and produce images containing the common disk nodes,
`fdisk`, and `gpt`. The real-board tests verified
OHCI periodic interrupt-IN, console input, proc0 deferred exploration, boot
with an empty port, late attach, detach, address reuse, repeated reconnect,
EHCI high-speed enumeration/reconnect, EHCI-to-OHCI ownership handoff, raw
mass-storage reads and writes, FAT32 read/write mutation and persistence,
64-bit file offsets, FAT maintenance tools, simultaneous keyboard/storage
operation through a high-speed hub, child recovery, and seven complete
populated-hub reconnects with automatic periodic-schedule recovery.
The final capture is retained as
`docs/usb-logs/ci20-ehci-periodic-recovery-verified-20260716.txt`.
Earlier failed candidate captures remain in `docs/usb-logs/` as regression
evidence.

## 64-bit Disk and GPT Hardware Gate

This milestone adds a transport-independent 64-bit block-number and LBA API,
primary/backup GPT parsing, sixteen `sdNa` through `sdNp` partition minors,
and `/sbin/gpt`. The target-side create/add/delete and payload-preserving
MBR-to-GPT image migration smoke passed on Creator Ci20 on 2026-07-16 with:

```text
cfbdad4c9423d574c42103a88f8a77d24769e3eb50d6951197636bd3021af78b  ci20.uImage
```

This GPT image also contained the then-pending EHCI three-window split schedule
and dropped-IOC watchdog. Passing the GPT smoke did not validate those
keyboard-stability changes. They were subsequently completed with periodic
schedule accounting and verified in the separate 2026-07-16 USB image and
capture documented above.

The original verified command was:

```sh
/root/gpt-image-smoke.sh
```

The current candidate extends this smoke by corrupting the primary GPT
signature, selecting the backup read-only, running `gpt -r`, and validating
the rebuilt primary. It requires a new target run before that repair path is
recorded as hardware verified.

The smoke is non-destructive and touches only a sparse 4 MiB `/var` image. Its
first hardware run exposed the legacy `dd(1)` output-truncation limitation;
the passing image adds standard `conv=notrunc`, and the smoke uses it to patch
only the nonzero MBR and payload bytes. The exact transcript is retained in
`usb-logs/ci20-gpt-image-smoke-verified-20260716.txt`.

Actual kernel GPT partition attachment must be tested only on a sacrificial
USB medium: `gpt -c /dev/sdN` destroys its existing partition table. Until a
GPT medium attaches as `sdNa`, survives data I/O and reconnect, and has a
retained UART capture, kernel GPT media attachment remains an implementation
candidate rather than a hardware claim.

An existing legacy MBR can be checked for payload-preserving migration with:

```sh
gpt -m -n /dev/sd0
```

The `-n` preflight opens the disk read-only. It passed on the existing FAT32
USB medium without changing it, reporting type `0x0c`, start LBA 63, and
30,298,527 sectors. Migration is accepted only when
all used entries are ordinary primary partitions with known GPT type mappings,
do not overlap, start at or after LBA 34, and end before the backup GPT area.
The observed FAT32 partition at LBA 63 satisfies these geometric constraints.
After a backup and an explicit hardware decision, `gpt -m /dev/sd0` writes the
backup GPT first, the primary GPT second, and the protective MBR last while
preserving every partition start and size. Extended/logical MBR layouts are
deliberately rejected.
