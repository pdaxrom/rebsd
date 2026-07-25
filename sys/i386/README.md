# Experimental i686/BIOS port

This directory contains the GCC-only bring-up port for legacy BIOS PCs.  The
first hardware target is an IBM PC 300GL 6563-W4G with a Pentium III, VIA
Apollo Pro 133 chipset, AGP video, and legacy IDE.

The initial image implements Linux/x86 boot protocol 2.02.  The same
`rebsd-i686.bzimg` is loaded directly by QEMU during bring-up and by LILO
`image=` for the BIOS hard-disk gate.

## Build and smoke test

Use a separate object root:

```sh
make -C sys/i386 BOARD=pc O=/work/rebsd-build/i686-pc all
make -C sys/i386 BOARD=pc O=/work/rebsd-build/i686-pc boot-smoke
make -C sys/i386 BOARD=pc O=/work/rebsd-build/i686-pc trap-smoke
make -C sys/i386 BOARD=pc O=/work/rebsd-build/i686-pc boot-smoke-matrix
```

The normal smoke validates E820 normalization, generic `vm_phys`/`vm_page`
bootstrap, physical page allocation, bootstrap paging with `CR0.WP`, IDT
entry/return, and ten PIT timer IRQs.
The trap smoke deliberately raises divide error (`#DE`), general protection
(`#GP`), and a write-protection page fault (`#PF`) and requires a diagnostic
panic.  The matrix boots 32, 64, 128, 256, 768, and 1024 MiB QEMU
configurations.
Build artifacts are written under `O/obj/sys/i386/`; the source tree remains
clean.

The low-level paging backend also self-tests map/unmap/protect/extract,
supervisor/user permissions, resident translation replacement, and targeted
TLB invalidation through `invlpg`.  The normalized E820 ranges back the
generic physical-page allocator; bootstrap and metadata pages stay reserved,
and the generic allocation/free/poison self-test runs in QEMU.  A permanent
kernel direct map covers the first 1 GiB of physical memory.  The public
i386 pmap self-test creates two address spaces, switches CR3, validates
isolation/protection/execution, and verifies that wired page-table pages are
reclaimed.  The image also links generic `vm_map`/`vm_object`/`vmspace`;
its self-test exercises anonymous faults, clone+COW and an actual recoverable
i386 page fault.  The generic-kernel integration audit is in
`docs/I386_MD_API.md`.

The default cross toolchain is:

```text
/Users/sash/Library/i686-toolchain/bin/i686-elf-
```

Override it with `I686_TOOLCHAIN=/path` or `I686_PREFIX=/path/prefix-`.
Override QEMU with `QEMU_I386=/path/qemu-system-i386`.

Current scope is deliberately small: real-mode setup, A20, BIOS E820, flat
protected mode, COM1, VGA text output, a 256-entry IDT, CPU exception
diagnostics, remapped dual 8259A PICs, PIT IRQ0 at 100 Hz, normalized
physical RAM, non-PAE 4 KiB bootstrap paging, and reusable low-level page
mapping primitives.  It links the machine-independent `vm_phys`/`vm_page`
allocator and implements the public pmap contract with per-process address
spaces, generic vmspace, anonymous memory and COW.  Early swap is explicitly
disabled.  It does not yet connect process context switching, the rest of
the generic kernel, storage, userland, or PCC.
