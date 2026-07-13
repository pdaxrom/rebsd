# ReBSD USB Source Origin

This document records the provenance of the classic BSD USB sources used as
the reference for the ReBSD USB host port.  It must be updated in the same
commit that imports a file or makes a material derivation from an additional
source.

## Primary Historical Source

- Release: NetBSD 3.1
- Archive: `syssrc.tgz`
- Official URL:
  <https://archive.netbsd.org/pub/NetBSD-archive/NetBSD-3.1/source/sets/syssrc.tgz>
- Archive size: 28,736,203 bytes
- SHA-256:
  `1de6647d340f05e10a61118e445ed11d259b02988b100c7e15bd08caff7ba9c6`
- Download and extraction date: 2026-07-12
- Extracted source root: `usr/src/sys/dev/usb/`

The archive was downloaded to `/tmp/netbsd-3.1-syssrc.tgz` and extracted
outside the repository for inspection.  The archive itself is not vendored.
The recorded digest covers the unmodified downloaded archive.

## Import Policy

NetBSD 3.1 is the primary source for USB protocol definitions, USB core object
relationships, hub handling, OHCI, EHCI, HID boot keyboard support, and USB
Mass Storage Bulk-Only Transport.  The code is adapted to ReBSD APIs; the
NetBSD autoconfiguration, device framework, bus_space, bus_dma, kernel thread,
mutex, PMF, sysmon, proplib, kauth, and kqueue frameworks are not imported to
satisfy dependencies.

Every imported or materially derived file must retain all applicable original
copyright and license notices.  Structural rewrites must still name their
historical source here.  A file described only as a reference has not
contributed copied code until its status is changed in the tables below.

OpenBSD's classic USB implementation may be consulted only as a secondary
reference for a specific bug fix or simplification.  Such use must record the
OpenBSD file, revision, purpose, and destination before the change is
committed.  Modern NetBSD USB, FreeBSD USB2, Linux USB core, and other large USB
frameworks are not source candidates for the generic ReBSD stack.

## Primary File Map

No NetBSD source file was imported at Phase 0.  The following comparison map
is updated as each later phase imports or materially adapts a source.

| NetBSD 3.1 source | ReBSD destination | Intended use | Status |
| --- | --- | --- | --- |
| `sys/dev/usb/usb.h` | `sys/dev/usb/usb.h` | USB protocol constants and descriptors | compact adaptation; original notice and RCS id retained |
| `sys/dev/usb/usbdi.h` | `sys/dev/usb/usbdi.h` | driver-facing transfer API | compact adaptation; original notice and RCS id retained |
| `sys/dev/usb/usbdivar.h` | `sys/dev/usb/usbvar.h` | core object relationships | bounded-pool adaptation; original notice and RCS id retained |
| `sys/dev/usb/usb_mem.h` | `sys/dev/usb/usb_mem.h` | USB DMA allocation contract | reference only |
| `sys/dev/usb/usb_quirks.h` | `sys/dev/usb/usb_quirks.h` | compact quirk flags if required | reference only |
| `sys/dev/usb/usb.c` | `sys/dev/usb/usb_core.c`, `sys/dev/usb/usb_task.c` | bus lifecycle and deferred task flow | lifecycle concepts combined into compact core; fixed task queue drained by ReBSD proc0 retains original notice and RCS id |
| `sys/dev/usb/usb_subr.c` | `sys/dev/usb/usb_subr.c` | enumeration and descriptor handling | descriptor traversal substantially rewritten around fixed bounds; original notice and RCS id retained |
| `sys/dev/usb/usbdi.c` | `sys/dev/usb/usb_core.c` | pipe and transfer operations | terminal-state and pipe model substantially adapted; original notice and RCS id retained |
| `sys/dev/usb/usb_mem.c` | `sys/dev/usb/usb_mem.c` | USB use of the ReBSD DMA API | reference only |
| `sys/dev/usb/uhub.c` | `sys/dev/usb/uhub.c` | root and external hubs | compact root-hub exploration/attach/detach adaptation; original notice and RCS id retained; external-hub path not yet imported |
| `sys/dev/usb/ohci.c` | `sys/dev/usb/ohci.c` | generic OHCI HCD | polling control, periodic interrupt-IN, and root-port/RHSC paths substantially adapted; original notice and RCS id retained |
| `sys/dev/usb/ohcireg.h` | `sys/dev/usb/ohcireg.h` | OHCI registers and descriptors | compact adaptation; original notice and RCS id retained |
| `sys/dev/usb/ohcivar.h` | `sys/dev/usb/ohcivar.h` | OHCI private state | compact bounded-state adaptation; original notice and RCS id retained |
| `sys/dev/usb/ehci.c` | `sys/dev/usb/ehci.c` | generic EHCI HCD | compact control/bulk, root-port, completion, abort, and companion-handoff adaptation; original notice and RCS id retained |
| `sys/dev/usb/ehcireg.h` | `sys/dev/usb/ehcireg.h` | EHCI registers and descriptors | compact adaptation; original notice and RCS id retained |
| `sys/dev/usb/ehcivar.h` | `sys/dev/usb/ehcivar.h` | EHCI private state | fixed-schedule bounded-state adaptation; original notice and RCS id retained |
| `sys/dev/usb/usbhid.h` | `sys/dev/usb/usbhid.h` | HID class requests needed by boot keyboards | compact adaptation; original notice and RCS id retained |
| `sys/dev/usb/uhidev.h` | `sys/dev/usb/uhidev.h` or compact equivalent | HID definitions needed by boot keyboards | reference only |
| `sys/dev/usb/ukbd.c` | `sys/dev/usb/ukbd.c` | HID boot keyboard only | compact boot-protocol adaptation; original notice and RCS id retained |
| `sys/dev/usb/ukbdmap.c` | `sys/dev/usb/ukbdmap.c` | basic US key map | compact ASCII/terminal adaptation; original notice and RCS id retained |
| `sys/dev/usb/umass.c` | `sys/dev/usb/umass.c` | single-LUN read-only BOT | reference only |
| `sys/dev/usb/umassvar.h` | `sys/dev/usb/umassvar.h` | compact umass state | reference only |

The generated NetBSD `usbdevs.h` and `usbdevs_data.h` product-name database is
not planned for import.  The first ReBSD implementation prints numeric vendor
and product IDs together with class, subclass, and protocol.

## Native ReBSD Infrastructure

The following files supply native ReBSD infrastructure. Unless a row notes a
classic API adaptation, they are not copied or materially derived from the
NetBSD USB sources:

| ReBSD file | Purpose |
| --- | --- |
| `sys/include/dma.h` | Small machine-independent DMA allocation and synchronization contract |
| `sys/kernel/subr_dma.c` | Bounded contiguous-pool allocator and ownership validation |
| `sys/mips/ci20/dma.c` | Ci20 uncached KSEG1 pool backend and ordering barriers |
| `sys/tests/dma/` | Host-side allocator, validation, exhaustion, and synchronization tests |
| `sys/dev/usb/usb_desc.h` | Bounded parser result structures, limits, and status API |
| `sys/tests/usb/` | Host compile gate and valid/malformed descriptor tests |
| `sys/dev/usb/usb_limits.h` | Compile-time core pool limits |
| `sys/dev/usb/usb_hcd.h` | Minimal native HCD operations contract |
| `sys/dev/usb/usb_mock_hcd.[ch]` | Deterministic hardware-independent test controller |
| `sys/dev/usb/usb_service.c` | Generic bounded-core service instance used by platform HCD attachments |
| `sys/dev/usb/usb_task.h` | Fixed task record adapted from the classic `usbdi.h` concept; original notice and RCS id retained |
| `sys/dev/usb/uhub.h` | Native bounded root-hub state and event interface |
| `sys/dev/usb/ukbd.h` | Bounded boot-report decoder and driver-registration interface |
| `sys/mips/ci20/usb_hw.[ch]` | Testable JZ4780 clock, PHY, reset, and VBUS sequence |
| `sys/mips/ci20/usb.c` | Ci20 MMIO callbacks, EHCI/OHCI attachment, IRQ routing, companion ownership, and enumeration diagnostics |

The DMA linker reservation and configuration entries are likewise native
integration code.  `usb_desc.h` and the parser tests are native code; the
parser implementation retains the `usb_subr.c` notice because its purpose,
descriptor model, and historical comparison point come from that file.  The
HCD interface, fixed limits, and mock controller are native ReBSD code.
JZ4780 PDMA code was neither imported nor referenced: OHCI and EHCI are bus
masters and do not require the SoC PDMA engine.

## Primary Source Revisions and Sizes

These values identify the exact files in the verified NetBSD 3.1 archive.

| File | RCS revision in archive | Bytes |
| --- | --- | ---: |
| `usb.h` | 1.71 | 21,329 |
| `usbdi.h` | 1.64 | 11,098 |
| `usbdivar.h` | 1.73.6.1 | 10,755 |
| `usb_mem.h` | 1.22 | 4,180 |
| `usb_quirks.h` | 1.20 | 3,314 |
| `usb.c` | 1.81.6.1 | 19,056 |
| `usb_subr.c` | 1.122.2.1 | 38,379 |
| `usbdi.c` | 1.106 | 30,947 |
| `usb_mem.c` | 1.28 | 10,672 |
| `uhub.c` | 1.74 | 19,580 |
| `ohci.c` | 1.157.2.1 | 87,238 |
| `ohcireg.h` | 1.19 | 9,955 |
| `ohcivar.h` | 1.36 | 5,110 |
| `ehci.c` | 1.91.2.9 | 83,470 |
| `ehcireg.h` | 1.20.2.2 | 12,414 |
| `ehcivar.h` | 1.17.8.2 | 6,173 |
| `uhidev.c` | 1.24 | 14,593 |
| `uhidev.h` | 1.4 | 3,340 |
| `ukbd.c` | 1.85.16.1 | 22,794 |
| `ukbdmap.c` | 1.13.10.1 | 20,079 |
| `ukbdvar.h` | 1.2 | 2,185 |
| `umass.c` | 1.117 | 54,006 |
| `umassvar.h` | 1.23 | 8,295 |

## Hardware-Specific References

Hardware references do not alter the origin of the generic USB stack.  They
are used only by the JZ4780/Ci20 attachment layer.

| Reference | Intended use | Current status |
| --- | --- | --- |
| `docs/JZ4780_pm.pdf` | JZ4780 register addresses, clocks, reset, and PHY | used by native Ci20 attachment; exact pages and digest in `docs/CI20_USB.md` |
| NetBSD `sys/arch/mips/ingenic/ingenic_ohci.c`, `ingenic_regs.h`, `apbus.c` | OHCI mapping, IRQ, clock gate, and suspend cross-check | exact commits recorded in `docs/CI20_USB.md`; no code imported |
| MIPS/CI20_linux JZ4780 USB, CGU, and DTS files | board VBUS, controller mapping, PHY/reset sequence cross-check | commit `7dff33297116643485ca37141d804eddd793e834`; hardware facts only |
| MIPS/CI20_u-boot `pll.c` and `ci20.c` | UHC source and board VBUS cross-check | commit `ef995a1611f0446a0b670ded9ec2609cb6dc51b7`; hardware facts only |
| Creator Ci20 quick start guide | physical J23 host and J24/J8 OTG connector topology | January 2016 connector table; hardware facts only |

`docs/CI20_USB.md` records each secondary source's exact URL or repository
revision, contributed hardware fact, register meaning, and reason for use.

## License Tracking

The NetBSD archive contains files with more than one BSD-family notice.  The
notice at the top of each selected source file is authoritative and will be
preserved in the corresponding ReBSD file.  This document supplements those
headers; it does not replace them.  No generated vendor database, firmware, or
binary object from the archive is included.
