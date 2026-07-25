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
make -C sys/i386 BOARD=pc O=/work/rebsd-build/i686-pc boot-smoke-matrix
```

The matrix boots 32, 64, 128, and 256 MiB QEMU configurations.  Build
artifacts are written under `O/obj/sys/i386/`; the source tree remains clean.

The default cross toolchain is:

```text
/Users/sash/Library/i686-toolchain/bin/i686-elf-
```

Override it with `I686_TOOLCHAIN=/path` or `I686_PREFIX=/path/prefix-`.
Override QEMU with `QEMU_I386=/path/qemu-system-i386`.

Current scope is deliberately small: real-mode setup, A20, BIOS E820, flat
protected mode, COM1, VGA text output, and a deterministic halt marker.  It
does not yet connect the machine-independent kernel, paging, interrupts,
storage, userland, or PCC.
