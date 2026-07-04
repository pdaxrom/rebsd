# ReBSD PCC Porting Notes

This document records the current ReBSD/MIPS Portable C Compiler integration
state.  The supported milestone is a C userland compiler for static ReBSD
a.out systems.  Kernel, stage0, and target binary tools still use the existing
ReBSD/GCC build flow unless a command explicitly says otherwise.

## Target Contract

- Target triples: `mips-rebsd`, `mips-unknown-rebsd`.
- Compatibility aliases: `mips-retrobsd`, `mips-unknown-retrobsd`.
- Target machine: big-endian MIPS o32.
- Object format: ReBSD a.out.
- Target tools: ReBSD `as`, `ld`, `ar`, `ranlib`, `nm`, and `size`.
- Startup file: `/usr/lib/crt0.o`.
- Default system include path: `/usr/include`.
- Default system library path: `/usr/lib`.
- Default PCC helper linkage: `-lpcc -lc -lpcc`.
- Default target sysroot: `/` when no `--sysroot` is supplied.

PCC defines both ReBSD identity macros and RetroBSD compatibility macros because
parts of the tree still carry RetroBSD-era conditionals.  The target also
defines the normal Unix and MIPS big-endian/o32 preprocessor surface.

ReBSD/MIPS currently treats `long double` as IEEE64, matching `double`.  PCC and
the public headers must therefore report the target `long double` limits rather
than assuming an extended 80-bit or 128-bit format.

ReBSD/MIPS keeps `wchar_t` as a 16-bit ABI type for PCC and libc.  The current
wide-character conversion policy is single-byte ASCII-oriented; full locale or
UTF-8 semantics are future policy work.

## Compiler Selection

GCC remains the default compiler for kernel and userland builds.  PCC is a
supported opt-in C userland compiler:

```sh
make -C sys/mips/malta MIPS_ROOTFS_COMPILER=pcc native-pcc-regress-runtime
make -C sys/mips/malta64 MIPS_ROOTFS_COMPILER=pcc native-pcc-regress-runtime
make -C sys/mips/n64 N64_USERLAND_COMPILER=pcc kernel.z64 preflight.z64
```

`MIPS_ROOTFS_COMPILER` and `N64_USERLAND_COMPILER` are aliases for the same
userland compiler choice at different make entry points.  If only one is set,
the other entry point inherits it.  If both are set and disagree, make aborts.
The supported values are `gcc` and `pcc`.

PCC mode changes the target userland/rootfs compiler only.  It does not switch
the kernel, N64 stage0, host bootstrap tools, or target a.out binary tools away
from the existing flow.

## Native Rootfs Layout

In a PCC-built rootfs the native C compiler is installed as:

- `/usr/bin/cc`
- `/usr/bin/pcc`
- `/usr/bin/cpp`
- `/usr/libexec/pcc/cpp`
- `/usr/libexec/pcc/ccom`

The default rootfs does not install the legacy `cc`/`pcc`, old `cpp`, `lcc`,
`lccom`, `smallc`, or `smlrc` compiler stack.  It also does not publish
`/usr/bin/p++` or `/usr/libexec/pcc/cxxcom`.

Development headers live under `/usr/include`.  Static system libraries and
start files live under `/usr/lib`.  The root directory should not contain
development trees such as `/include`, `/share/man`, or static libraries under
`/lib`.

Manual pages for the native toolchain are installed under `/usr/share/man`.
Preformatted cat pages are generated with `nroff -Tascii` so target consoles do
not see UTF-8 typographic hyphen bytes as corrupted text.

## Runtime Libraries

`src/dev/pcc/pcc-libs/libpcc` is staged as `/usr/lib/libpcc.a`.  It owns
compiler-private helper routines that PCC-generated code may reference directly,
including arithmetic, conversion, and stack-protector helpers.

Public C, POSIX, BSD, math, and compatibility APIs belong in ReBSD libc or libm,
not in `libpcc.a`.  When a PCC-built userland exposes a missing public symbol,
add the related API family to the owning system library instead of growing a
collection of isolated stubs.

`libpccsoftfloat.a` is not staged for the current hard-float Malta, Malta64, or
N64 targets.  Add it only if a no-FPU target or a concrete test failure proves a
runtime dependency.

`src/dev/pcc/pcc-libs/csu` provides upstream PCC `crtbegin.o` and `crtend.o`,
but ReBSD does not enable them in the default C link path.  The current a.out
toolchain and libc startup support flat linker-defined ctor/dtor ranges, which
is enough for C constructor/destructor attributes in the C gate.

## Toolchain Requirements

The ReBSD a.out toolchain is part of the supported PCC target:

- `ld` accepts `-L`, `-l`, and `--sysroot[=DIR]`, defaults sysroot to `/`, and
  applies sysroot only to standard library directories.
- `ld` preserves 8-byte text/data/BSS alignment needed by MIPS FPU literals and
  `double` storage.
- `as` accepts the GCC/PCC MIPS syntax used by the imported backend, including
  ctor/dtor sections, `.init_array`/`.fini_array`, `.eh_frame`, `neg`/`negu`,
  absolute `la`, `symbol+-offset` expressions, UTF-8 symbol bytes, and the
  relocation forms needed by PCC-built shell/login paths.
- `as` has a VR4300-compatible instruction checking mode used by N64 and the
  Malta64/R4000 compatibility gate.
- Target `cc` invokes absolute `/usr/bin/as` and `/usr/bin/ld` so boot-time and
  non-login environments do not depend on shell `PATH`.
- Target `cc` honors `TMPDIR` for compiler temporaries.

The imported PCC MIPS backend carries ReBSD fixes for big-endian `long long`,
sub-word stack arguments, hard-float o32 helper calls, aggregate return ABI,
stack alignment, unsigned narrow memory loads, MIPS unsigned comparisons,
computed goto, static initializer string references, floating NaN/Inf folding,
and FCSR-safe `DEBUGFP` oracle checks.

## Validation Gates

The current C gate is green in these environments:

- Host cross smoke with ReBSD `as`/`ld`.
- Malta QEMU PCC userland.
- Malta64/R4000 QEMU PCC userland using
  `qemu-system-mips64 -M malta -cpu R4000 -m 32M -nographic`.
- Real N64 hardware normal PCC rootfs smoke.

The native PCC regression gate currently reports:

- 316 total compile/link checks.
- 286 compile/link passes.
- 30 expected compile/link failures.
- 0 unexpected compile/link failures.
- 276 runtime candidates.
- 276 runtime passes.
- 0 unexpected runtime failures.

The expected compile/link failures are outside the current static C gate:

- `gcccompat/typeof001` embeds x86 inline assembly constraints.
- `misc/shlib3` requires shared-library/PIC support.
- `pcclist/init004` requires TLS.

The N64 hardware smoke uses `/root/pcc-smoke-all.sh` from the normal PCC rootfs.
It covers native PCC compile/link/run, shell/login-sensitive paths, repeated
`ccom`, libc/math smoke tests, DHCP receive support, and Linpack binaries built
with both GCC and PCC.

## Out Of Scope For The C Gate

C++ is not published.  Before enabling `/usr/bin/p++` or
`/usr/libexec/pcc/cxxcom`, ReBSD needs a real C++ frontend gate and a runtime
startup policy for `crtbegin.o`/`crtend.o`, sentinel entries, constructor
priorities, destructor ordering, and partial links.

TLS and shared libraries are not supported by the current static a.out target.
Keep the corresponding upstream PCC tests expected-fail unless ReBSD grows a
concrete ABI and runtime policy for them.

Locale-aware multibyte and UTF-8 wide-character semantics are not part of the
current PCC milestone.  The existing wide-character libc surface documents the
current single-byte execution character model.

## Build Artifacts

Generated rootfs stages, native PCC build trees, port object files, ROM images,
catman outputs, and temporary manifests are build artifacts.  They must stay
ignored unless a generated file is already a tracked project source input.
