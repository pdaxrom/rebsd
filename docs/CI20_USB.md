# Creator Ci20 USB Host Bring-up

This document records every JZ4780/Ci20-specific fact used by the first ReBSD
USB host attachment. The generic USB core and OHCI HCD do not include these
register definitions; they enter through the HCD and DMA interfaces.

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

This implementation still explores only a device already connected during boot.
Post-boot root-port hotplug, external hubs, bulk transfers, and general HID
report parsing remain later work.

The gate was verified on a real Ci20 on 2026-07-13 with the low-speed
`1c4f:0002` composite device. Interface 0 attached as an eight-byte boot
keyboard at a 10 ms interval, JZ4780 IRQ 5 was enabled, and keyboard input was
used at the live ReBSD console to log in as root and run `ls`.

## Programming Manual Cross-check

The checked-in `docs/JZ4780_pm.pdf` is 16,628,911 bytes with SHA-256
`c31707c6975dfb73268fa25632e298169f311a47e10409961236ba981e02bae5`.
The PDF page index is 34 pages greater than the printed manual page number in
this part of the document.

| Register | Address and fields used | Manual location |
| --- | --- | --- |
| `UHCCDR` | `0x1000006c`; source `OTG_PHY` in 31:30, `CE_UHC` 29, `UHC_BUSY` 28, `UHC_STOP` 27, divider 7:0; OTG PHY requires divider zero | PDF 453-454, printed 419-420, section 18.1.2.11 |
| `CLKGR0` | `0x10000020`; bit 24 set stops UHC, clear supplies its clock | PDF 484-485, printed 450-451, section 18.2.2.9 |
| `OPCR` | `0x10000024`; bit 6 `SPENDN1`, set means port 1 is not forced into suspend | PDF 489-490, printed 455-456, section 18.2.2.16 |
| `USBPCR` | `0x1000003c`; bit 22 PHY `POR`, bit 21 `SIDDQ`, bit 20 `OTG_DISABLE` | PDF 462-463, printed 428-429, section 18.1.2.22 |
| `USBPCR1` | `0x10000048`; reference source 27:26, reference frequency 25:24, port-1 D-/D+ pulldowns 23/22, port-1 reset 20, port-1 UTMI width 18 | PDF 465-466, printed 431-432, section 18.1.2.25 |
| `SRBC` | `0x100000c4`; bit 14 `UHC_SR` | PDF 486-487, printed 452-453, section 18.2.2.11 |

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
| CI20 Linux `jz4780.dtsi` | same commit; OHCI base/IRQ `0x134a0000`/5 and EHCI base/IRQ `0x13490000`/20: <https://github.com/MIPS/CI20_linux/blob/7dff33297116643485ca37141d804eddd793e834/arch/mips/boot/dts/jz4780.dtsi> |
| CI20 Linux CGU source/header | same commit; 48 MHz PHY reference, `SPENDN1`, port-1 pulldowns/16-bit UTMI, one-millisecond PHY POR, and 300-microsecond UHC reset pulse: <https://github.com/MIPS/CI20_linux/blob/7dff33297116643485ca37141d804eddd793e834/drivers/clk/jz47xx/jz4780-cgu.c>, <https://github.com/MIPS/CI20_linux/blob/7dff33297116643485ca37141d804eddd793e834/arch/mips/include/asm/mach-jz4740/jz4780-cgu.h> |
| CI20 U-Boot `pll.c` | MIPS/CI20_u-boot commit `ef995a1611f0446a0b670ded9ec2609cb6dc51b7`; selects `OTG_PHY` for `UHCCDR`: <https://github.com/MIPS/CI20_u-boot/blob/ef995a1611f0446a0b670ded9ec2609cb6dc51b7/arch/mips/cpu/xburst/jz4780/pll.c> |
| CI20 U-Boot `ci20.c` | same commit; drives GPF15 high as `SYS_POWER_IND` / VBUS on: <https://github.com/MIPS/CI20_u-boot/blob/ef995a1611f0446a0b670ded9ec2609cb6dc51b7/board/imgtec/ci20/ci20.c> |

ReBSD explicitly reprograms all required state and does not depend on U-Boot
having left the clock, suspend, or GPIO registers configured.

## HID Keyboard Hardware Test

Build the GCC Ci20 image with the normal project command:

```sh
make -C sys/mips BOARD=ci20 \
    MIPS_KERNEL_COMPILER=gcc MIPS_ROOTFS_COMPILER=gcc kernel
```

The object profile places the result at
`../rebsd-usb-support-build/ci20-kgcc-ugcc-mips32r2-hard-little-elf/obj/sys/mips/ci20/ci20.uImage`.
Boot it with the existing Ci20/U-Boot procedure while capturing UART4. Connect
a USB boot-protocol keyboard to the type-A host port before boot and leave it
connected. A mouse or flash drive cannot exercise this path.

Success reaches lines equivalent to:

```text
dma: Ci20 uncached pool phys=... size=65536 align=4096
usb0: initializing core
usb0: core ready
ukbd0: HID boot-keyboard driver ready
ohci0: attach, OHCI phys=134a0000
ohci0: init: enable VBUS
ohci0: init: settle VBUS
ohci0: init: configure UHC clock
ohci0: init: ungate UHC clock
ohci0: init: configure host PHY
ohci0: init: pulse PHY reset
ohci0: init: pulse UHC reset
ohci0: init: hardware ready
ohci0: Ci20 VBUS on, cpm clkgr0=... opcr=... usbpcr=... usbpcr1=... uhccdr=... srbc=...
ohci0: OHCI revision=10 ports=1 control-polling periodic-interrupt-IN
ohci0: port1 status=... change=..., resetting
ohci0: port1 enabled status=... speed=low
ukbd0: boot keyboard, interrupt in 0x81, 8 bytes every 10 ms
ohci0: usb addr=1 vendor=... product=... config=1 interfaces=...
ohci0: if0 class=... subclass=... protocol=... endpoints=...
ohci0: irq 5 enabled for periodic transfers
```

After the login prompt appears, use the USB keyboard rather than UART input to
enter `echo usb-ok`, including Return. Also check Shift, Backspace, and Ctrl-C.
The first test deliberately does not include unplug/replug because post-boot
root-port exploration is not implemented yet.

The early `init` markers are printed before each potentially faulting MMIO
group, so the last marker identifies the operation to inspect if the board
stops before the full register dump. If no device is present, the expected
non-fatal endpoint is `port1 powered, no device detected`. `controller start
failed` narrows the fault to OHCI MMIO/reset/revision; `port1 reset failed`
narrows it to VBUS/PHY/line state; `enumeration failed` means endpoint-zero
traffic started and its printed USB status is the next diagnostic.

Ci20 microsecond delays use dedicated TCU1 channel 3 at 750 kHz. Channel 0
remains the 100 Hz system clock. The JZ4780 manual documents the independent
16-bit channels and clock divisors on PDF pages 494-498 (printed pages
460-464), and the automatic `TCNT` wrap-and-continue behavior on PDF page 499
(printed page 465). Hardware bring-up showed that the previous CP0 Count based
delay did not complete, so USB and DM9000 now share the bounded TCU3 backend.

## Verification Without Hardware

`make -C sys/tests/usb test` executes descriptor, core/mock-HCD, boot-report
decoder, full fake-OHCI control/periodic scheduling, and fake-JZ4780 register
sequence tests. The fake OHCI test covers IRQ acknowledgement, data-toggle
carry, rearm, simultaneous control traffic, and software disconnect/reconnect.
The Ci20 sequence test verifies VBUS-first ordering, `UHCCDR_BUSY` timeout,
final gate/suspend state, PHY POR, and UHC reset deassertion. A full GCC kernel
links and produces the hardware-test image. The modified translation units
also pass the configured MIPS PCC compiler gate. The real-board test above
verified the periodic interrupt-IN and console-input path.
