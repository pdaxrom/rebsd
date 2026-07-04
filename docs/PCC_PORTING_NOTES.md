# PortableCC Porting Notes

Initial upstream layout notes after importing PortableCC:

- MIPS backend already exists in `src/dev/pcc/pcc/arch/mips`.
- OS-specific compiler configuration lives under `src/dev/pcc/pcc/os/<os>`.
- Driver defaults and predefined macros are centralized in
  `src/dev/pcc/pcc/cc/driver/platform.c` and `os/<os>/ccconfig.h`.
- `configure.ac` maps target triples to `targmach`, `targos`, `abi`, and
  endianness. ReBSD should add `rebsd*` OS handling there, with
  `mipseb`/`mips` mapping to `targmach=mips` and big-endian output.
- Existing `os/none` is too freestanding for native `/usr/bin/cc`; existing
  `os/bsd` is close historically but only covers older non-MIPS assumptions.

Likely first local glue files:

- `src/dev/pcc/pcc/os/rebsd/ccconfig.h`
- `src/dev/pcc/pcc/configure.ac`
- generated `src/dev/pcc/pcc/configure`, unless the build flow gains an
  explicit autoreconf step
- `src/dev/pcc/pcc/cc/driver/platform.c`, only if `ccconfig.h` is not enough
  to express ReBSD include paths, library paths, startup files, and macros

Current ReBSD target behavior is conservative:

- target triples: `mips-rebsd`, `mips-unknown-rebsd`
- compatibility aliases: `mips-retrobsd`, `mips-unknown-retrobsd`
- target machine: big-endian MIPS o32
- object/link path: existing ReBSD a.out tools
- default startup: `/usr/lib/crt0.o`
- default libraries: `-lpcc -lc -lpcc` from `/usr/lib`
- no shared libraries, PIC, TLS, C++, or kernel build switch in the supported
  C milestone

## Selectable Userland Compiler

The default kernel and userland compiler remains the existing GCC flow.  PCC is
supported as an explicit opt-in userland/rootfs compiler:

- Malta/Malta64: `MIPS_ROOTFS_COMPILER=pcc`
- N64: `N64_USERLAND_COMPILER=pcc`

Both selectors accept `gcc` and `pcc`.  They are aliases for the same compiler
mode at different make entry points: Malta/Malta64 use the shared MIPS rootfs
wrapper, which forwards the selected mode into the N64/common rootfs userland
builder.  If both variables are set and disagree, make aborts before any
build.  In PCC mode the imported compiler frontend builds target userland
while ReBSD continues to use its own a.out `as`, `ld`, `ar`, and `ranlib`.
The selector does not change the kernel or N64 stage0 compiler.

## Host Cross Smoke

Current milestone status:

- `configure.ac`, generated `configure`, and `config.sub` recognize
  `mips-rebsd` and `mips-retrobsd` targets.
- `os/rebsd/ccconfig.h` defines ReBSD identity macros, RetroBSD compatibility
  macros, MIPS big-endian/o32 macros, ReBSD include paths, `crt0.o`, and
  default `-lpcc -lc -lpcc` linkage.
- The MIPS backend selects big-endian output for `TARGET_BIG_ENDIAN`.
- ReBSD disables unsupported MIPS ABI/PIC assembler pseudo-ops such as
  `.abicalls`, `.cpload`, and `.cprestore`.
- ReBSD C output suppresses `.file`, which the current ReBSD assembler rejects.
- Out-of-tree host build succeeds in `/private/tmp/rebsd-pcc-build` with
  install prefix `/private/tmp/rebsd-pcc-install`.
- A trivial program compiles with
  `/private/tmp/rebsd-pcc-install/bin/mips-rebsd-pcc` through assembly, object,
  and linked a.out output using the ReBSD `as` and `ld`.
- The linked smoke binary boots and runs under QEMU Malta when temporarily
  staged as `/root/rebsd-pcc-smoke`; expected output `rebsd-pcc-smoke:6` was
  observed.
- `make -C sys/mips/malta smoke-host-portablecc` builds and installs a host
  `mips-rebsd-pcc` cross compiler in `/private/tmp/rebsd-pcc-install`.
- The expanded host smoke compiles `types-smoke.c`, `ll-smoke.c`, and
  `ll-abi-smoke.c` through `-S`, `-c`, and linked a.out binaries.
- The standalone FPU smoke compiles `pcc-fpu-smoke.c` through `-S`, `-c`, and
  linked a.out output; generated assembly uses hardware double instructions
  including `l.d`, `s.d`, `mul.d`, `c.lt.d`, and `bc1f`.
- The expanded Malta runtime smoke passes with `types-status:0`, `ll-status:0`,
  and `llabi-status:0`.
- The standalone Malta FPU smoke passes with `fpu-status:0`.

The ReBSD linker now supports `-L` and `-l` library search flags, so PCC can
use normal library arguments while still producing ReBSD a.out output.

The current ReBSD/MIPS native PCC regression gate has 316 total checks:
286 compile/link pass, 30 expected compile/link fail, and 276/276 runtime
candidates pass.  The same target-side gate passes on Malta and on the
Malta64/R4000 QEMU profile used as the closest automated N64-class CPU check.
The real N64 hardware smoke also passes with the normal PCC rootfs.

## Runtime Libraries

`src/dev/pcc/pcc-libs/libpcc` is staged as `/usr/lib/libpcc.a` for ReBSD/MIPS.
It is built as a ReBSD a.out archive by the same native runtime path that
builds `crt0.o`, `libc.a`, and `libm.a`; the wrapper uses GCC for C-to-assembly
for now, then the ReBSD assembler and archive tools for target object format.

`libpccsoftfloat.a` is not staged for the current Malta/N64 hardware-FPU
target.  Add it only if tests show a real soft-float dependency.

`src/dev/pcc/pcc-libs/csu` has the upstream PCC `crtbegin.o`/`crtend.o`
implementation for global constructors/destructors.  ReBSD does not enable it
in the default C link path yet.  The current a.out toolchain and libc startup
support flat linker-defined ctor/dtor ranges, which is enough for C
constructor/destructor attributes in the current gate.  Full C++ still needs a
startup policy for `crtbegin.o`/`crtend.o`, sentinel entries, constructor
priorities, destructor ordering, and partial links before `/usr/bin/p++` is
published.

## MIPS Backend Fixes

The old RetroBSD `src/cmd/ccom/arch-mips` backend is the reference for
ReBSD-specific MIPS behavior while the imported PortableCC backend is brought
up.  Fixes already carried into `src/dev/pcc/pcc/arch/mips` include:

- suppressing unsupported ABI/PIC assembly for ReBSD: `.mdebug.abi32`,
  `.abicalls`, `.cpload`, and `.cprestore`;
- emitting `.set noreorder` for ReBSD so assembler pseudo-ops such as `la` are
  not reordered into branch delay slots;
- big-endian `long long` memory load/store and stack argument word order;
- big-endian `CLASSB` register word selection and overlapping register moves;
- direct `SOREG` conversion table entries for scalar and `long long`
  narrowing/widening, matching the old backend's `offchg()` expectations;
- big-endian offset handling for narrowing conversions and stack-passed
  sub-word ABI slots;
- static initializer emission for big-endian `long long`, `float`, `double`,
  and `long double`;
- synthetic floating literal symbols with target alignment before `defloc()`.
