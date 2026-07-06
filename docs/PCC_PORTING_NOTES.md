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
- Soft-float helper override path: `/usr/lib/softfloat/libpcc.a`.
- Default target sysroot: `/` when no `--sysroot` is supplied.

PCC defines both ReBSD identity macros and RetroBSD compatibility macros because
parts of the tree still carry RetroBSD-era conditionals.  The target also
defines the normal Unix and MIPS big-endian/o32 preprocessor surface.  The
selected float ABI controls whether PCC defines `__mips_hard_float` or
`__mips_soft_float`.

ReBSD/MIPS currently treats `long double` as IEEE64, matching `double`.  PCC and
the public headers must therefore report the target `long double` limits rather
than assuming an extended 80-bit or 128-bit format.

ReBSD/MIPS keeps `wchar_t` as a 16-bit ABI type for PCC and libc.  The current
wide-character conversion policy is single-byte ASCII-oriented: byte/wide
conversion routines accept `0x00..0x7f`, reject non-ASCII bytes or wide
characters with `(size_t)-1` or `WEOF` as appropriate, and keep `mbstate_t`
stateless.  Full locale or UTF-8 semantics are future policy work.

`sys/cdefs.h` is a supported BSD-compatibility surface for the C gate.  It
provides declaration wrappers, `__P`, BSD string/concatenation helpers, selected
compiler attributes, branch prediction helpers, `__arraycount`, compiler version
predicates, and the `__RCSID`/`__COPYRIGHT`/`__KERNEL_RCSID`/`__IDSTRING`
metadata macros used by imported BSD/PCC sources.  It does not promise C++
startup, TLS, shared-library/PIC, or locale/multibyte behavior.

## Compiler Selection

GCC remains the default compiler for kernel and userland builds.  PCC is a
supported opt-in C userland compiler:

```sh
make -C sys/mips/malta MIPS_ROOTFS_COMPILER=pcc native-pcc-regress-runtime
make -C sys/mips/malta MIPS_ROOTFS_COMPILER=pcc MIPS_ROOTFS_CPU=mips32r2 MIPS_ROOTFS_FLOAT=soft linpack-smoke-runtime
make -C sys/mips/malta64 MIPS_ROOTFS_COMPILER=pcc native-pcc-regress-runtime
make -C sys/mips/malta64 MIPS_ROOTFS_COMPILER=pcc MIPS_ROOTFS_CPU=vr4300 MIPS_ROOTFS_FLOAT=soft linpack-smoke-runtime
make -C sys/mips/n64 N64_USERLAND_COMPILER=pcc kernel.z64 preflight.z64
```

`MIPS_ROOTFS_COMPILER` and `N64_USERLAND_COMPILER` are aliases for the same
userland compiler choice at different make entry points.  If only one is set,
the other entry point inherits it.  If both are set and disagree, make aborts.
The supported values are `gcc` and `pcc`.

PCC mode changes the target userland/rootfs compiler only.  It does not switch
the kernel, N64 stage0, host bootstrap tools, or target a.out binary tools away
from the existing flow.

The rootfs selectors shared by Malta, Malta64, and N64 are:

- `MIPS_ROOTFS_CPU=vr4300|mips32r2`
- `MIPS_ROOTFS_FLOAT=hard|soft`
- `MIPS_ROOTFS_ENDIAN=big`

`MIPS_ROOTFS_ENDIAN=little` is reserved for the future mipsel Malta port and is
rejected by the current big-endian `mips-rebsd` PCC target.

## Active PCC Work Queue

The current PCC milestone is a selectable MIPS CPU userland compiler plus
compiler-level MIPS soft-float support.  Endianness remains fixed by the PCC
target build: the current `mips-rebsd` target is big-endian, and a future Malta
little-endian port should use a separate `mipsel-rebsd` target rather than a
runtime `-EL` switch on this compiler.

Completed for this milestone:

- ReBSD/MIPS PCC CPU switches for `vr4300` and `mips32r2`.  Accept both
  GCC-style aliases (`-march=vr4300`, `-mips3`, `-march=mips32r2`,
  `-mips32r2`) and pass the selected CPU to both `ccom` and `as`.
- The selected CPU is part of the C ABI layout.  `vr4300` keeps the current
  8-byte alignment policy; `mips32r2` uses the 4-byte Malta o32 layout.
- Keep big-endian/little-endian selection target-build-time only for PCC.
  Reject little-endian command-line mode on the current big-endian target
  instead of silently producing mixed-mode objects.
- The ReBSD assembler's VR4300 instruction checking and mips32r2/VR4300
  text alignment are selected by the `-march`/`-mips*` mode passed by PCC, not
  only by the assembler binary's compile-time default.
- Make selectors for userland/rootfs CPU mode include CPU in build stamps so
  Malta, Malta64/R4000, and N64 rootfs stages cannot be accidentally reused
  across incompatible CPU ABI variants.
- MIPS soft-float mode in PCC updates preprocessor macros, code generation,
  helper calls, and libc runtime helper symbols.
- Rootfs/userland float ABI selectors (`hard` and `soft`) are part of the ABI
  stamp, so hard-float and soft-float runtime, native PCC, rootfs, and smoke
  artifacts do not share directories.
- PCC userland runtime uses a wrapper-free two-stage bootstrap: build standalone
  cross a.out tools plus cross PCC without target runtime libraries, then use
  that cross PCC to build `crt0.o`, libc, libm, and `libpcc.a`, then build
  native PCC and the rootfs against the PCC-built runtime.
- Cross SDK and target rootfs layouts keep hard-float and soft-float compiler
  runtime libraries separate.  The normal runtime lives in `lib/libpcc.a`; the
  soft-float override lives in `lib/softfloat/libpcc.a`.
- Soft-float `libpcc.a` includes the compiler-private compiler-rt helpers that
  PCC-generated code and other `libpcc` helpers can reference directly.

Remaining work:

- Run the updated PCC userland/rootfs gate on real N64 hardware in both
  hard-float and soft-float modes after the Malta64/R4000 and Malta QEMU
  matrix stays stable.  Default N64 hardware builds use `N64_ZSWAP=1` so the
  old whole-process swapper gets a larger logical swap map without reducing the
  4 MiB Expansion Pak user window.

## Standalone Cross SDK

The standalone SDK is built by `sys/mips/tools/build-cross-pcc-sdk.sh` or
`sys/mips/sdk.mk`:

```sh
sys/mips/tools/build-cross-pcc-sdk.sh --cpu vr4300 --float soft --endian big --prefix /path/cross-pcc
make -C sys/mips -f sdk.mk cross-pcc-sdk CPU=mips32r2 FLOAT=hard ENDIAN=big MIPS_SDK_PREFIX=/path/cross-pcc
make -C sys/mips -f sdk.mk cross-pcc-sdk-tools CPU=mips32r2 FLOAT=soft ENDIAN=big MIPS_SDK_PREFIX=/path/cross-pcc
make -C sys/mips -f sdk.mk cross-pcc-sdk-runtime CPU=mips32r2 FLOAT=soft ENDIAN=big MIPS_SDK_PREFIX=/path/cross-pcc
```

The intended installed layout is:

```text
cross-pcc/bin/mips-rebsd-pcc
cross-pcc/bin/mips-rebsd-cc
cross-pcc/bin/mips-rebsd-as
cross-pcc/bin/mips-rebsd-ld
cross-pcc/bin/mips-rebsd-aout
cross-pcc/mips-rebsd/lib/crt0.o
cross-pcc/mips-rebsd/lib/libc.a
cross-pcc/mips-rebsd/lib/libm.a
cross-pcc/mips-rebsd/lib/libpcc.a
cross-pcc/mips-rebsd/lib/softfloat/libpcc.a
```

The SDK is usable directly through the installed binaries; no PCC wrapper is
required for normal compile, assemble, link, or `aout` inspection paths.  GCC
wrapper scripts remain only for the legacy GCC-based userland path.

Rootfs builds normally use the standalone SDK with:

```sh
make -C sys/mips/malta MIPS_ROOTFS_COMPILER=pcc \
    MIPS_PCC_PROVIDER=cross MIPS_PCC_HOST_PREFIX=/path/cross-pcc \
    linpack-smoke-runtime
```

`MIPS_PCC_PROVIDER=system` remains valid for a ReBSD build host with a
compatible native compiler and target tools; a host PCC build is not mandatory
in that case.

## Known Kernel PCC Blocker

Kernel PCC builds are not a supported gate yet.  Experimental selectors may be
used to reproduce the current blocker:

```sh
make -C sys/mips/malta64 MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc native-pcc-smoke-runtime
make -C sys/mips/malta MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc native-pcc-smoke-runtime
make -C sys/mips/n64 N64_KERNEL_COMPILER=pcc N64_USERLAND_COMPILER=pcc kernel.z64
```

As of the 2026-07-05 investigation, the first Malta64 kernel C compile stops in
`sys/kernel/exec_aout.c` on the prototyped `rdwri()` call:

```text
../../kernel/exec_aout.c, line 82: warning: illegal combination of pointer and integer
../../kernel/exec_aout.c, line 82: warning: illegal combination of pointer and integer
../../kernel/exec_aout.c, line 82: cannot recover from earlier errors: goodbye!
error: /private/tmp/rebsd-pcc-install/libexec/mips-rebsd-ccom terminated with status 1
```

The same frontend issue is expected at the equivalent `rdwri()` calls in
`sys/kernel/exec_elf.c`.  The prototype is visible to PCC:

```c
int rdwri(enum uio_rw rw, struct inode *ip, caddr_t base, int len,
    off_t offset, int ioflg, int *aresid);
```

The original kernel call passes `0` for the optional `aresid` pointer, which is
a valid null pointer constant in C.  A reduced standalone call with the same
basic typedefs compiles, but the full preprocessed kernel translation unit
emits the two pointer/integer diagnostics and can also hit the internal PCC
frontend error `compiler error: strmemb` when `-Werror` is removed.  Treat this
as a PCC frontend bug, not as a kernel source issue.  Do not add local kernel
workarounds for these `rdwri()` call sites just to make PCC proceed.

PCC now has compiler-level MIPS soft-float support for targeted userland smoke
tests, but kernel PCC builds still need an explicit FPU policy before becoming
a supported gate.  Kernel builds should either use soft-float deliberately or
keep an explicit COP1/FPU instruction guard around PCC-generated kernel C
assembly.

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

The current C milestone treats compatibility surfaces as explicit library
policy, not as compiler-runtime helpers:

- C99 math classification, NaN constructors, and sign tests are libc/libm
  owned.
- Wide-character strings, memory routines, numeric parsers, byte/wide
  conversion helpers, and wide stdio are libc owned.
- `sys/cdefs.h` compatibility macros are named and limited to the C/static
  a.out target contract.
- Locale-aware multibyte, UTF-8, C++ startup, TLS, and shared-library behavior
  are intentionally not hidden behind placeholder functions.

Soft-float mode uses an ABI-specific compiler runtime archive staged as
`/usr/lib/softfloat/libpcc.a`.  The normal hard-float archive remains
`/usr/lib/libpcc.a`.  Keeping both archives in the rootfs and SDK lets target
PCC and future kernel builds select the correct compiler helpers without
wrapper scripts or rebuilding the SDK.

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
sub-word stack arguments, hard-float and soft-float o32 helper calls, aggregate
return ABI, stack alignment, mixed aggregate/64-bit stack slot accounting,
unsigned narrow memory loads, MIPS unsigned comparisons, computed goto, static
initializer string references, floating NaN/Inf folding, and FCSR-safe
`DEBUGFP` oracle checks.  The current branch also fixes the MIPS
call/register allocator path used by
indirect calls through `$25`, avoids clobbering 64-bit argument registers while
precomputing complex call arguments, corrects the `$t6` register-pair map, keeps
ReBSD `OFFSZ` at the target ABI width, prints unsigned 32-bit constants without
sign-extension in assembler output, and keeps the shell allocator safety guard
enabled for both `vr4300` and `mips32r2` userlands.

## Validation Gates

The current C gate is green in these environments:

- Host cross smoke with ReBSD `as`/`ld`.
- Malta QEMU PCC userland, including `mips32r2` hard-float and soft-float
  `/root/pcc-smoke-all.sh` plus LINPACK smoke.
- Malta64/R4000 QEMU PCC userland, including hard-float and soft-float LINPACK
  smoke for the `vr4300` ABI, using
  `qemu-system-mips64 -M malta -cpu R4000 -m 64M -nographic`.
- Real N64 hardware normal hard-float PCC rootfs smoke from the earlier gate.
  The updated hard/soft split still needs a fresh real-hardware pass.

The 2026-07-05 QEMU smoke matrix finished with:

```text
malta64 vr4300  hard  PCC_SMOKE_ALL_FAILURES 0  PCC_SMOKE_ALL_RC:0
malta64 vr4300  soft  PCC_SMOKE_ALL_FAILURES 0  PCC_SMOKE_ALL_RC:0
malta   mips32r2 hard  PCC_SMOKE_ALL_FAILURES 0  PCC_SMOKE_ALL_RC:0
malta   mips32r2 soft  PCC_SMOKE_ALL_FAILURES 0  PCC_SMOKE_ALL_RC:0
```

The same runs included `linpack-pcc`:

```text
malta64 vr4300 hard:   11384.472 and 11252.091 KFLOPS
malta64 vr4300 soft:     604.800, 775.384, and 775.385 KFLOPS
malta mips32r2 hard:   10996.368 and 10518.260 KFLOPS
malta mips32r2 soft:     817.298 and 742.086 KFLOPS
```

The Malta64 low-memory swap check on 2026-07-06 used the N64-sized profile
`MALTA_RAM_KBYTES=8192 MALTA_RAMSWAP_KBYTES=4608 MALTA_QEMU_RAM=64M` with the
rootfs linked at physical `0x00800000`.  Boot reported `phys mem = 8192 kbytes`,
`user mem = 4096 kbytes`, `root size = 16384 kbytes`, and
`swap size = 4608 kbytes`; `/root/pcc-smoke-all.sh` finished with
`PCC_SMOKE_ALL_FAILURES 0` and `PCC_SMOKE_ALL_RC:0`.  The `linpack-pcc` result
inside that run was `12096.004` and `12172.076` KFLOPS.

The 32-bit Malta low-memory swap check on 2026-07-06 used the same profile with
`MIPS_ROOTFS_CPU=mips32r2` and hard-float PCC userland.  Boot again reported
`phys mem = 8192 kbytes`, `user mem = 4096 kbytes`, `root size = 16384 kbytes`,
and `swap size = 4608 kbytes`; `/root/pcc-smoke-all.sh` finished with
`PCC_SMOKE_ALL_FAILURES 0` and `PCC_SMOKE_ALL_RC:0`.  The `linpack-pcc` result
inside that run was `10751.999`, `10294.467`, and `10132.773` KFLOPS.

On real 8 MiB N64 hardware, the native utility workload needs more writable
temporary space than the original 512 KiB `/var` RAM disk can provide.  The N64
default `/dev/ram0` reservation is therefore 1 MiB.  The 4 MiB user window is
unchanged; with zswap the expected 8 MiB boot report becomes
`user mem = 4096 kbytes` and `swap size = 3584 kbytes`.

QEMU Malta exposes only a 4 MiB BIOS/pflash ROM window.  The normal PCC rootfs
does not fit there, so the smoke layout keeps the rootfs outside guest
`physmem` rather than in true pflash ROM.  On Malta64, stage0 copies only the
kernel blob; the rootfs is a separate stage0 section at the same address used
by the kernel romdisk symbols.

The native PCC regression gate currently reports:

- 317 total compile/link checks.
- 287 compile/link passes.
- 30 expected compile/link failures.
- 0 unexpected compile/link failures.
- 277 runtime candidates.
- 277 runtime passes.
- 0 unexpected runtime failures.

The native smoke gate also runs `/root/libc-abi-smoke.sh` with both `cc` and
`pcc`.  It covers struct return/by-value calls, stack-passed 64-bit arguments,
qsort callbacks, varargs and `vsnprintf`, parsing, `setjmp`/`longjmp`, and stdio
file I/O.  This smoke caught a real target ABI/header mismatch: PCC did not
define `__mips_hard_float`, so `setjmp.h` exposed a 14-word `jmp_buf` while the
hard-float MIPS `setjmp.S` saved 47 words and overwrote the following global.
The target macro and smoke guard are now part of the C gate.

The expected compile/link failures are target-aware policy cases for
`mips-rebsd`, not generic PCC failures:

- `gcccompat/typeof001` embeds x86 inline assembly constraints.
- `misc/shlib3` requires shared-library/PIC support.
- `pcclist/init004` requires TLS.

The N64 hardware smoke uses `/root/pcc-smoke-all.sh` from the normal PCC rootfs.
Default hardware ROMs use `N64_ZSWAP=1`; use `N64_ZSWAP=0` only for raw
swap-regression comparison.  The smoke covers native PCC compile/link/run,
shell/login-sensitive paths, repeated `ccom`, libc/math/wide-character policy
smoke tests, DHCP receive support, real small utility rebuilds, and Linpack
binaries built with both GCC and PCC.

## Out Of Scope For The C Gate

C++ is explicitly deferred to future work.  `/usr/bin/p++` and
`/usr/libexec/pcc/cxxcom` are not published in any default rootfs.  The current
diagnostic `cxxcom` is not a usable ReBSD C++ compiler: it only handles a very
narrow C-like subset, still emits assembler directives that the ReBSD assembler
rejects, and fails on core C++ features such as classes, constructors,
references, overloads, templates, and `extern "C"`.  Before enabling C++, ReBSD
needs a real frontend gate plus a runtime startup policy for `crtbegin.o`,
`crtend.o`, sentinel entries, constructor priorities, destructor ordering, and
partial links.

TLS and shared libraries are explicitly deferred to future work.  They are not
supported by the current static a.out target.  Keep the corresponding upstream
PCC tests expected-fail unless ReBSD grows a concrete ABI and runtime policy for
them.

Locale-aware multibyte and UTF-8 wide-character semantics are not part of the
current PCC milestone.  The existing wide-character libc surface documents the
current single-byte execution character model.

## Build Artifacts

Generated rootfs stages, native PCC build trees, port object files, ROM images,
catman outputs, and temporary manifests are build artifacts.  They must stay
ignored unless a generated file is already a tracked project source input.
