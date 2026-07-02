# PCC Baseline

This file records the pre-PortableCC baseline that the new pcc port must match
before it can become a selectable userland compiler.

## Build Defaults

- Kernel compiler: existing GCC-based MIPS/N64 toolchain.
- Userland compiler: existing GCC-based MIPS/N64 toolchain.
- Native compiler in rootfs: existing `/usr/bin/cc` and `/usr/bin/pcc` path.
- Userland object format: RetroBSD/ReBSD a.out, produced through the existing
  in-tree a.out tools and `elf2aout` where the current build uses ELF as an
  intermediate format.
- Default target ABI: big-endian MIPS o32.

`N64_USERLAND_COMPILER` is the userland compiler selector. Its default is
`gcc`. `pcc` is opt-in and builds userland through the imported PortableCC
frontend while continuing to use the in-tree ReBSD a.out assembler, linker,
archive tool, and ranlib.

## Compatibility Oracle

The current compiler and target toolchain remain the reference implementation:

- driver: `src/cmd/cc`
- old pcc backend: `src/cmd/ccom`
- preprocessor: `src/cmd/cpp`
- assembler: `src/cmd/as`
- linker: `src/cmd/ld`
- archive tools: `src/cmd/ar` and `src/cmd/ranlib`

The new pcc must use the existing assembler, linker, archive, and ranlib tools.
It must not import GNU binutils, LLVM tools, or a second archive format.

## Required Compatibility

The new pcc must preserve:

- big-endian MIPS output;
- o32 calling convention;
- 32-bit `int`, `long`, and pointer types;
- 64-bit `long long`;
- existing startup and library paths such as `/usr/lib/crt0.o` and
  `/usr/lib/libc.a`;
- static userland linking;
- ReBSD predefines plus RetroBSD compatibility predefines.

## Required Gate

QEMU Malta is the mandatory regression target before any N64 hardware testing.
At minimum, the pcc transition must pass:

- host compile/preprocess/assemble/link smoke tests;
- native `/usr/bin/cc` and `/usr/bin/pcc` smoke tests in Malta;
- ABI tests for integers, pointers, structures, varargs, and `long long`;
- archive and ranlib tests using the existing ReBSD tools;
- runtime execution of pcc-built binaries in Malta.
