# VM Baseline

This file records the fixed-memory implementation that must remain functional
while the replacement VM is developed.  All intervals are half-open
`[start, end)`, all addresses in the physical maps are physical, and all sizes
are bytes unless stated otherwise.

## Address width and ABI

Every current kernel and user ABI is 32-bit:

| Port | CPU ISA used by the kernel | Kernel ABI | User toolchain/ABI |
| --- | --- | --- | --- |
| Malta | MIPS32 | o32, big-endian | 32-bit MIPS |
| MaltaEL | MIPS32 | o32, little-endian | 32-bit MIPSEL |
| Ci20 | MIPS32 | o32, little-endian | 32-bit MIPSEL |
| Malta64 | MIPS III/R4000 | o32, big-endian | 32-bit MIPS |
| N64 | MIPS III/VR4300 | o32, big-endian | 32-bit MIPS PCC |

Malta64's small loader is built with `-mabi=64`, but the kernel it loads is
still built with `-mabi=32`.  The N64 CPU is also 64-bit capable, but its
kernel is built with `-mabi=32`.  Consequently the current `vm_vaddr_t`,
`vm_uaddr_t`, `vm_paddr_t`, `vm_size_t`, VPN, PFN, and protection types are
fixed-width 32-bit types.  Object/file offsets may be 64-bit.  A future 64-bit
kernel or user ABI is a separate porting project, not an implicit widening of
these types.

## Malta, MaltaEL, and Malta64

The three ports use the same physical layout.  `RAM_SIZE` is selected by the
build (normally 32 or 64 MiB), `ROOTFS_SIZE` is likewise configurable, and
`KEND` is `_end` converted from KSEG0 and rounded up to a 4 KiB VM page.

| Physical interval | Current owner |
| --- | --- |
| `[0x00000000, 0x00100000)` | firmware, exception-vector backing, unused low RAM |
| `[0x00100000, KEND)` | linked kernel image and BSS |
| `[KEND, 0x002fc000)` | currently unused RAM |
| `[0x002fc000, 0x00300000)` | fixed `u0` and `u` areas, 8 KiB each |
| `[0x00300000, 0x00700000)` | physical backing of the fixed user window |
| `[0x00700000, 0x00800000)` | `/var` RAM disk |
| `[0x00800000, 0x00800000 + ROOTFS_SIZE)` | linked root filesystem, clipped to installed RAM |
| following 2 MiB | in-RAM cartflash sparse backing, clipped to installed RAM |
| following configured range | RAM swap, clipped to installed RAM |
| all remaining RAM | currently unused RAM |

The cartflash storage and the Ci20 DMA pool described below are linked kernel
storage or explicitly reserved RAM; they are not MMIO apertures.  Malta PCI,
UART, chipset, and other MMIO ranges are outside this RAM map and must remain
device mappings rather than allocatable pages.

The CPU TLB is cleared at startup.  Malta, MaltaEL, and Malta64 use 16 entries.
Entries 0 and 1 are then wired as two 1 MiB page pairs, mapping user virtual
`[0x00400000, 0x00800000)` to physical
`[0x00300000, 0x00700000)`.  Both halves are global, valid, dirty, and use the
current coherent/cacheable attribute.  There is no per-process ASID mapping;
the scheduler and swap code reuse this one physical window.

## Ci20

Only the first 256 MiB low-memory bank is currently described.  `KEND` has the
same meaning as above.

| Physical interval | Current owner |
| --- | --- |
| `[0x00000000, 0x00100000)` | exception-vector backing and unused low RAM |
| `[0x00100000, KEND)` | linked kernel image and BSS |
| `[KEND, 0x002fc000)` | currently unused RAM |
| `[0x002fc000, 0x00300000)` | fixed `u0` and `u` areas |
| `[0x00300000, 0x00700000)` | physical backing of the fixed user window |
| `[0x00700000, 0x00800000)` | `/var` RAM disk |
| following configured rootfs range from `0x00800000` | linked root filesystem |
| following configured range | RAM swap |
| all remaining RAM | currently unused RAM |

The 256 KiB aligned Ci20 DMA pool is a `.dma` object inside kernel BSS and is
therefore covered by `[0x00100000, KEND)`.  JZ4780 peripherals and other MMIO
are outside the RAM bank and are not entries in the allocatable physical map.

Ci20 has 32 TLB entries.  Entries 0 and 1 have the same wired global mapping
as Malta: user virtual `[0x00400000, 0x00800000)` to physical
`[0x00300000, 0x00700000)` using two 1 MiB page pairs.

## Nintendo 64

The installed RDRAM size is detected as either 4 or 8 MiB.  The ROM root
filesystem and cartridge/device registers are not RDRAM and do not appear in
the physical-page map.  `KEND` is the kernel `_end`, converted from KSEG0 and
rounded to 4 KiB.

Common low RDRAM ownership is:

| Physical interval | Current owner |
| --- | --- |
| `[0x00000000, 0x00001000)` | exception-vector backing (the stage0 vector at `0x180` is included) |
| `[0x00001000, KEND)` | linked kernel image and BSS |
| `[KEND, 0x000f0000)` | currently unused RDRAM |
| `[0x000f0000, 0x000f4000)` | fixed `u0` and `u` areas |
| `[0x000f4000, 0x00100000)` | currently unused RDRAM |

On a 4 MiB machine:

| Physical interval | Current owner |
| --- | --- |
| `[0x00100000, 0x00300000)` | physical backing of the 2 MiB fixed user window |
| `[0x00300000, 0x00340000)` | resident stage0/restart image reserve |
| `[0x00340000, 0x00380000)` | deliberate stage0/framebuffer alias |
| `[0x00380000, 0x003a0000)` | `/var` RAM disk |
| `[0x003a0000, 0x00400000)` | RAM swap store |

On a normal 8 MiB machine:

| Physical interval | Current owner |
| --- | --- |
| `[0x00100000, 0x00300000)` | fixed user-window backing |
| `[0x00300000, 0x00380000)` | fixed user-window/stage0 alias |
| `[0x00380000, 0x00500000)` | fixed user-window backing |
| `[0x00500000, FB_END)` | framebuffer reserve (`0x00540000` normally, `0x005a0000` in high-resolution builds) |
| `[FB_END, FB_END + 0x00100000)` | `/var` RAM disk |
| remaining RDRAM to `0x00800000` | RAM swap store |

UART-only debug builds on an 8 MiB machine do not reserve a framebuffer:
`/var` is `[0x00500000, 0x00600000)` and swap is
`[0x00600000, 0x00800000)`.

The resident stage0 includes the embedded kernel ELF.  It is limited by the
linker to `[0x00300000, 0x00380000)`.  On a 4 MiB machine that reserve aliases
the framebuffer by design; on an 8 MiB machine it aliases part of the legacy
user window.  The diagnostic map names these aliases explicitly so a future
allocator cannot mistake the pages for independent ownership.

N64 has 32 TLB entries.  A 4 MiB machine wires one 1 MiB page pair for user
virtual `[0x00400000, 0x00600000)` to physical
`[0x00100000, 0x00300000)`.  An 8 MiB machine wires two pairs, extending both
ranges by 2 MiB.  Framebuffer mappings use 64 KiB pages, are uncached, and are
wired beginning at index 2.  The normal 256 KiB reserve consumes two such
entries; the 640 KiB high-resolution reserve consumes five.  Because the
framebuffer base index is fixed at 2, entry 1 remains invalid on a 4 MiB
machine.  User mappings are global, valid, and dirty, so they are not isolated
by ASID.

## Diagnostic bootstrap map

Phase 1 builds the same ownership map without allocating from it.  Board code
registers RAM and reservations, machine-independent code verifies page
alignment, ordering, overlap, overflow, and that available plus reserved bytes
equal detected RAM.  Boot prints one line of the form:

```
vm phys: <available>K available, <reserved>K reserved, <regions> regions
```

The legacy user window, fixed user areas, TLB setup, swap behavior, rootfs,
RAM disks, DMA, and framebuffer continue to operate exactly as before.
