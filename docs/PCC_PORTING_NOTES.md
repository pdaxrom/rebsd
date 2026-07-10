# ReBSD PCC Porting Notes

This document records the current ReBSD/MIPS Portable C Compiler integration
state.  The supported milestone is a C compiler for static ReBSD ELF userland
while preserving kernel exec compatibility with legacy a.out binaries.  GCC
remains the default compiler.  PCC is supported for opt-in userland/rootfs
builds and for explicit kernel build gates on Malta, Malta64, and N64.  Stage0
and the target binary tools still use the existing ReBSD flow unless a command
explicitly says otherwise.

## Target Contract

- Target triples: `mips-rebsd`, `mipsel-rebsd`, `mips-unknown-rebsd`, and
  `mipsel-unknown-rebsd`.
- Compatibility aliases: `mips-retrobsd`, `mipsel-retrobsd`,
  `mips-unknown-retrobsd`, and `mipsel-unknown-retrobsd`.
- Target machine: MIPS o32, big-endian for `mips-rebsd` and little-endian for
  `mipsel-rebsd`.
- Object format: static ELF32 for new MIPS userland, with legacy ReBSD a.out
  support retained in the kernel and tools.
- Target tools: ReBSD `as`, `ld`, `ar`, `ranlib`, `nm`, `size`, and `aout`.
- Startup file: `/usr/lib/crt0.o`.
- Default system include path: `/usr/include`.
- Default system library path: `/usr/lib`.
- Default linker script path: `/usr/lib/ldscripts/elf32-bigmips.ld` or
  `/usr/lib/ldscripts/elf32-littlemips.ld`, selected by target endian.
- Default PCC helper linkage: `-lpcc -lc -lpcc`.
- Soft-float helper override path: `/usr/lib/softfloat/libpcc.a`.
- Default target sysroot: `/` when no `--sysroot` is supplied.

PCC defines both ReBSD identity macros and RetroBSD compatibility macros because
parts of the tree still carry RetroBSD-era conditionals.  The target also
defines the normal Unix and MIPS o32 preprocessor surface, including endian
macros selected by the target triple.  The selected float ABI controls whether
PCC defines `__mips_hard_float` or `__mips_soft_float`.

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
supported opt-in userland/rootfs compiler:

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

These selectors change the target userland/rootfs compiler only.  The kernel
compiler is selected independently with `MIPS_KERNEL_COMPILER` for Malta and
Malta64, or `N64_KERNEL_COMPILER` for N64.  PCC kernel builds are explicit
gates; the default remains GCC.

The rootfs selectors shared by Malta, Malta64, and N64 are:

- `MIPS_ROOTFS_CPU=vr4300|mips32r2`
- `MIPS_ROOTFS_FLOAT=hard|soft`
- `MIPS_ROOTFS_ENDIAN=big|little`

The N64 entry point also accepts the alias variables
`N64_USERLAND_CPU`, `N64_USERLAND_FLOAT`, and `N64_USERLAND_ENDIAN`.  If both
the N64 and common `MIPS_ROOTFS_*` names are set, they must agree.

`MIPS_ROOTFS_ENDIAN=little` selects the `mipsel-rebsd` cross target for PCC
SDK/rootfs flows.  The checked-in `maltael` default remains
`MIPS_ROOTFS_COMPILER=gcc`, while the opt-in PCC kernel/rootfs hard-float and
soft-float gates are validated on QEMU.

## MIPS ISA And Tuning

PCC models instruction legality separately from scheduling and cost choices.
`-march` selects the ISA; `-mtune` selects the processor model.  The supported
ReBSD combinations are:

- `-march=vr4300`: compatibility shorthand for MIPS III plus VR4300 tuning.
- `-mips3`: compatibility alias with the same VR4300 behavior.
- `-march=mips3`: MIPS III legality with generic tuning unless `-mtune` is set.
- `-march=mips32r2` or `-mips32r2`: MIPS32r2 legality with generic r2 tuning
  unless `-mtune` is set.
- `-mtune=generic|vr4300|r4000|24kc|34kc|74kc|jz4780`.

PCC does not currently implement a MIPS32 Release 1 profile.  It rejects
`-march=mips32` with a diagnostic instead of treating it as MIPS32r2.  VR4300
and R4000 tuning require MIPS III; the 24Kc, 34Kc, 74Kc, and JZ4780 profiles
require MIPS32r2.

The target model records capability bits for legal instructions, 64-bit GPRs,
HI/LO, load interlocks, branch delay slots, and the VR4300 FP-multiply erratum.
Consequently, `-mfix4300` only inserts the workaround for MIPS III with VR4300
tuning; `-mno-fix4300` disables it.  QEMU is not expected to reproduce the
hardware fault, so the host smoke checks emitted assembly directly.

The initial cost table contains generic MIPS III, VR4300, and generic MIPS32r2
profiles.  VR4300 normal-path values use NEC manual U10504EJ7V0UM00 tables
3-12 and 7-14: integer MULT/DIV/DMULT/DDIV costs `5/37/8/69`, FP multiply
single/double `5/8`, and FP divide single/double `29/58`.  The generic entries
are relative costs because actual implementations vary.  This table is not yet
used to alter code generation.

## Optimizer And Allocator Statistics

PCC has an opt-in pass2 statistics stream for optimizer and allocator work:

```sh
pcc -O2 -fopt-stats -S -o output.s input.c 2>optstats.log
```

The internal PCC spelling is `-Zp`, available through the driver as
`-Wc,-Zp`.  Statistics are disabled by default and are written only to
standard error.  Each function produces one `PCC_OPTSTATS kind=function`
key/value record, followed at compiler exit by a `kind=summary` record.  The
field order and values are deterministic for the same compiler, source, and
options, so reports can be compared directly or parsed without debug text.

The report includes function and summary counts, basic blocks, CFG edges,
natural-loop nesting, TEMP count and maximum live pressure, GPR/FPR pressure,
interference edges, coalescing attempts/successes/rejections, spill candidates,
actual selected TEMP spills, generated reloads/stores, rematerialized values,
spill-area and complete target frame bytes, caller/callee-saved registers used,
and call count.  Interference/coalescing counters include allocator retries.
The spill counters exclude normal callee-save prologue/epilogue traffic.

The host toolchain smoke verifies that normal mode emits no report, that all
keys are present, that two reports are byte-identical, and that assembly is
byte-identical with statistics disabled and enabled.  `optstats.c` is also
part of the native PCC bootstrap, so the same instrumentation is available to
the compiler running under ReBSD.

## Register Allocator Spill Costs

The graph allocator uses measured, target-independent spill selection rather
than choosing the first eligible long-lived TEMP.  The score includes
loop-weighted uses and definitions, live-range length, interference degree,
call crossing, move relation, rematerialization, and GPR/FPR class.  Calls
already make live values interfere with caller-saved colors, so callee-saved
colors remain available when profitable.  Named weights and deterministic TEMP
number tie breaking keep this policy tunable and reproducible.

Only integer and symbol constants, their simple conversions, and simple frame
addresses can be rematerialized.  Memory loads, volatile accesses, calls,
side effects, and potentially trapping expressions are excluded.  Spill
metadata uses a temporary side array and does not enlarge the permanent graph
node representation.  Focused regression coverage includes GPR/FPR pressure,
nested loops, call-crossing and move-related values, struct copies, varargs,
and both FP ABIs.  Full measurements and commands are in
`docs/PCC_PHASE4_REPORT.md`.

## Pass2 CFG Verification

Phase 5A adds machine-independent verification in `mip/cfgverify.c` without
enabling a new optimization.  Every built CFG is checked for complete and
non-overlapping basic-block coverage, a consistent label map, the exact
successors implied by terminators and fallthrough, and reciprocal parent/child
edges.  CFG rebuild sites call the verifier immediately, providing the common
rebuild-and-verify path required by future branch folding.

When the existing internal SSA path is selected, the verifier independently
solves dominator sets and checks DFS parents, immediate dominators, dominator
tree edges, phi predecessor counts, duplicate phi destinations, and TEMP ranges
before and after rename.  The imported driver already passes `-xssa` for
normal optimized compilation; Phase 5A did not newly enable it.  The verifier
is compiled into all pass2 frontends and the target-hosted native PCC.  Host
smoke also uses explicit `-Wc,-xssa` on a nested-loop probe.  The Phase 5A
hard-float a.out debug ROM passed real N64 hardware with
`N64_PCC_DEBUG_END 0`, `N64_PCC_DEBUG_RUNNER_RC 0`, and
`N64_PCC_DEBUG_RC_END`.

Phase 5B replaces sequential phi removal with machine-independent
critical-edge splitting and typed parallel-copy lowering.  Fallthrough and
taken-edge pads preserve physical fallthrough layout, and a CFG verifier
rejects any remaining critical edge before phi placement.  Cycles use one
temporary only when no destination can be written safely.  A critical
computed-goto edge disables SSA for that function because pass2 cannot retarget
its destination table; the next function starts with the driver's normal SSA
setting.  Register allocation and matching use this per-function state.

The Phase 5B cross and native regressions pass 287/287 runtime cases, and all
six PCC-kernel/PCC-rootfs Malta64/Malta/MaltaEL hard/soft QEMU profiles report
`PCC_SMOKE_ALL_RC:0`.  The build-only N64 image is
`sys/mips/n64/builds/20260710-phase5b-ssa-lowering/pcc-debug.z64`; it uses the
default `-mfix4300` path but has not yet been run on real hardware.  Full
commands, measurements, hashes, and the kernel-gate finding are in
`docs/PCC_PHASE5_REPORT.md`.

Phase 5C performs only SSA-name cleanup before parallel-copy lowering.  A
type-identical `TEMP = TEMP` with one destination definition is propagated
through tree and phi uses; then phis whose defined non-self inputs all agree
are collapsed.  Undefined phi inputs prevent the transform.  Removed copy
nodes carry a private sentinel until edge insertion has finished, so cleanup
cannot consume user empty inline-asm or memory barriers.  The transform is
machine-independent and never substitutes a memory access, volatile object,
call, asm, or trapping expression.

The Phase 5C cross/native gates pass 288/288 runtime cases, and all six final
PCC-kernel/PCC-rootfs hard/soft QEMU profiles report `PCC_SMOKE_ALL_RC:0`.
The build-only default-`-mfix4300` N64 image is
`sys/mips/n64/builds/20260710-phase5c-ssa-copyprop/pcc-debug.z64`; real hardware
validation is still pending.  Full counters, logs, and hashes are in
`docs/PCC_PHASE5_REPORT.md`.

Phase 5D1 adds a deliberately incomplete integer SCCP lattice.  It propagates
only unnamed integral constants with exact TEMP types and equal-value phis.
Any undefined phi input, mixed value/type, mismatched TEMP use, or XASM
reference blocks substitution.  Constant phis are removed only when no other
phi consumes their result.  Expression evaluation and branch folding are not
part of D1.

The type guard is required by libc IR where pass1 conversions reuse an SSA
TEMP with a different `n_type`.  The XASM guard is required for CP0 and other
register-constrained operands: substituting a literal into `mtc0 %0,$6`
produced invalid assembly during the first Malta64 kernel gate.  Final
cross/native regressions pass 289/289 runtime cases and all six PCC-kernel/
PCC-rootfs profiles report `PCC_SMOKE_ALL_RC:0`.  The build-only default-
`-mfix4300` N64 image is
`sys/mips/n64/builds/20260710-phase5d1-sccp-lite/pcc-debug.z64`; hardware
validation remains pending.  Full details are in `docs/PCC_PHASE5_REPORT.md`.

Phase 5D2 folds only integral `CBRANCH` comparisons whose operands are already
unnamed `ICON` nodes.  Constant true becomes a normal direct `GOTO`; constant
false removes the branch and retains fallthrough.  Folding runs after the
existing post-SSA jump cleanup and immediately rebuilds/verifies the CFG.
There is no expression evaluator, FP fold, XASM substitution, target-specific
branch rewrite, or unreachable-block deletion in D2.

The D2 cross/native gates pass 290/290 runtime cases and all six PCC-kernel/
PCC-rootfs profiles report `PCC_SMOKE_ALL_RC:0`.  The build-only default-
`-mfix4300` N64 image is
`sys/mips/n64/builds/20260710-phase5d2-branch-fold/pcc-debug.z64`; hardware
validation remains pending.  Full commands, counters, and hashes are in
`docs/PCC_PHASE5_REPORT.md`.

Phase 5D3 computes a new reachability closure after D2.  The function entry,
epilogue, `IP_DEFNAM` blocks, and labels listed as computed-goto destinations
are roots.  This preserves alternate entries even when their computed `GOTO`
is itself unreachable.  Other disconnected basic blocks are removed as whole
interpass extents, then the CFG is rebuilt and verified immediately.  D3 does
not reuse stale dominator DFS numbers and is not a general DCE or LVN pass.

The D3 cross/native gates pass 291/291 runtime cases and all six PCC-kernel/
PCC-rootfs profiles report `PCC_SMOKE_ALL_RC:0`.  The build-only default-
`-mfix4300` N64 image is
`sys/mips/n64/builds/20260710-phase5d3-unreachable/pcc-debug.z64`.  A real N64
run reported `N64_PCC_DEBUG_END 0`, `N64_PCC_DEBUG_RUNNER_RC 0`, and the final
`N64_PCC_DEBUG_RC_END` marker.  The physical multiply erratum still requires
the default `-mfix4300` because QEMU cannot reproduce it.  Full commands,
counters, and hashes are in `docs/PCC_PHASE5_REPORT.md`.

Phase 5D4 performs local value numbering only within one SSA basic block.  It
matches structurally identical, exact-type integer arithmetic, bitwise, and
shift trees made only from SSA TEMPs and unnamed constants.  Calls, XASM,
memory references, stores, structure operations, and uncertain assignments
clear the local table.  FP, pointers, conversions, division, comparisons,
symbols, and commutative canonicalization are deliberately excluded.

A duplicate expression becomes a TEMP copy and the existing Phase 5C pass
removes that alias.  `misc/ssalvn001` confirms that one repeated XOR is removed
while a volatile-memory barrier keeps both evaluations.  Cross and native
runtime gates pass 292/292, and all six PCC-kernel/PCC-rootfs profiles report
`PCC_SMOKE_ALL_RC:0`.  The build-only default-`-mfix4300` D4 image is
`sys/mips/n64/builds/20260710-phase5d4-lvn/pcc-debug.z64`; real N64 validation
of this new image remains pending.  Full details are in
`docs/PCC_PHASE5_REPORT.md`.

## Active PCC Work Queue

The current PCC milestone is a selectable MIPS CPU userland compiler plus
compiler-level MIPS soft-float support.  Endianness is fixed by the PCC target
triple: `mips-rebsd` is big-endian, and `mipsel-rebsd` is little-endian.

Completed for this milestone:

- ReBSD/MIPS PCC CPU switches for `vr4300` and `mips32r2`.  Accept both
  GCC-style aliases (`-march=vr4300`, `-mips3`, `-march=mips32r2`,
  `-mips32r2`) and pass the selected CPU to both `ccom` and `as`.
- The selected CPU is part of the C ABI layout.  `vr4300` keeps the current
  8-byte alignment policy; `mips32r2` uses the 4-byte Malta o32 layout.
- MIPS32r2 data and integer-pair arguments retain that 4-byte layout.  FP64
  arguments use even o32 slots independently, including varargs, and formal
  parameter offsets apply the same rule.  This preserves the existing ReBSD
  `long long` ABI while keeping PCC FP calls compatible with GCC-built libc.
- Keep big-endian/little-endian selection as target identity for PCC.  Do not
  rely on a runtime `-EL`/`-EB` switch to mutate a compiler target into the
  opposite endian ABI.
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
  ReBSD tools plus cross PCC without target runtime libraries, then use
  that cross PCC to build `crt0.o`, libc, libm, and `libpcc.a`, then build
  native PCC and the rootfs against the PCC-built runtime.
- The ReBSD assembler and linker support ELF mode for MIPS.  Kernel PCC gates
  invoke `as --elf` and `ld --elf`; userland links with the board-generated
  ELF linker script.
- Rootfs and SDK installs use `/usr/lib/ldscripts/elf32-bigmips.ld` or
  `/usr/lib/ldscripts/elf32-littlemips.ld`.  The old flat
  `/usr/lib/elf32-mips.ld` path is intentionally not installed.
- Cross SDK and target rootfs layouts keep hard-float and soft-float compiler
  runtime libraries separate.  The normal runtime lives in `lib/libpcc.a`; the
  soft-float override lives in `lib/softfloat/libpcc.a`.
- Soft-float `libpcc.a` includes the compiler-private compiler-rt helpers that
  PCC-generated code and other `libpcc` helpers can reference directly.
- MIPS `-fomit-frame-pointer` is implemented in PCC for kernel builds.  The
  backend decides whether `$fp` is required before register allocation, then
  performs the final `$fp` to `$sp` rewrite only after `ngenregs()` knows the
  final spill frame size.  It keeps `$fp` for functions that need a stable
  frame base, including non-leaf functions while MIPS call templates adjust
  `$sp`, raw frame-address users, `alloca` users, and code that explicitly
  assigns `$fp`.
- PCC kernel builds now use the standalone cross PCC path without GCC wrapper
  scripts.  The verified Malta and Malta64 QEMU hard-float gates compile the
  kernel with PCC, build the PCC rootfs, boot, and run `/root/pcc-smoke-all.sh`.
  The N64 PCC hard-float full zswap ROM build also completes for hardware
  smoke, and the UART-only real-hardware boot isolation matrix passed with PCC
  and GCC kernels, raw swap, and zswap.
- N64 PCC kernel builds assemble PCC-generated kernel `.s` files with the
  ReBSD assembler in ELF mode and link the final kernel with the ReBSD linker
  in ELF mode: `mips-rebsd-as --elf -EB -march=vr4300` and
  `mips-rebsd-ld --elf -EB`.  The N64 stage0 path remains on the external N64
  GCC toolchain.
- The 2026-07-07 N64 build-only gate completed both full hard-float
  PCC-userland ROM variants:
  `N64_KERNEL_COMPILER=gcc N64_USERLAND_COMPILER=pcc N64_ROOTFS_KBYTES=32768`
  and
  `N64_KERNEL_COMPILER=pcc N64_USERLAND_COMPILER=pcc N64_ROOTFS_KBYTES=32768`.
  Both generated root images passed `fsutil --check`; the resulting kernel
  ELFs are big-endian MIPS-III ELF executables.  This was not a real-hardware
  smoke pass.
- Kernel version strings now include a detailed build banner:
  builder user/host, selected compiler and compiler version, CPU, float ABI,
  and endian ABI.

Remaining work:

- Run the updated PCC userland/rootfs gate on real N64 hardware in both
  hard-float and soft-float modes after the Malta64/R4000 and Malta QEMU
  matrix stays stable.  Default N64 hardware builds use `N64_ZSWAP=1` so the
  old whole-process swapper gets a larger logical swap map without reducing the
  4 MiB Expansion Pak user window.
- Decide the long-term PCC kernel FPU policy for soft-float kernel builds.  The
  current validated kernel gates are hard-float unless a test explicitly states
  otherwise.

## Standalone Cross SDK

The standalone SDK is built by `sys/mips/tools/build-cross-pcc-sdk.sh` or
`sys/mips/sdk.mk`:

```sh
sys/mips/tools/build-cross-pcc-sdk.sh --cpu vr4300 --float soft --endian big --prefix /path/cross-pcc
sys/mips/tools/build-cross-pcc-sdk.sh --cpu mips32r2 --float hard --endian little --prefix /path/cross-pcc
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
cross-pcc/mips-rebsd/lib/ldscripts/elf32-bigmips.ld
```

For `--endian little`, the installed target prefix is `mipsel-rebsd` and the
installed script is `mipsel-rebsd/lib/ldscripts/elf32-littlemips.ld`.

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

## PCC Rebuild Dependencies

Cross-provider builds use
`$(MIPS_PCC_HOST_PREFIX)/.rebsd-pcc-toolchain.stamp` as the normal prerequisite
for PCC-generated kernel, rootfs, runtime, benchmark, and regression outputs.
The stamp depends on the imported PCC frontend, middle-end, and backend sources
and records checksums for the installed PCC driver and `ccom`.  GCC-only kernel
and rootfs configurations do not depend on this stamp.

The dependency gate can be repeated without cleaning as follows:

```sh
make -C sys/mips/malta64 \
    MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc \
    MIPS_ROOTFS_FLOAT=soft \
    pcc-regress-compile linpack-smoke-build unix.elf
touch src/dev/pcc/pcc/arch/mips/local2.c
make -C sys/mips/malta64 \
    MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc \
    MIPS_ROOTFS_FLOAT=soft \
    pcc-regress-compile linpack-smoke-build unix.elf
```

Record timestamps before and after for the installed `ccom`, a PCC kernel
object, a PCC rootfs object, Linpack, and a regression binary.  On Malta64,
`malta64-stage0.o` is a useful GCC-generated control and must remain unchanged.
When using `touch` only as a dependency probe, restore the backend source's
original timestamp after the test.

## PCC Kernel Build Status

Kernel PCC builds are explicit opt-in gates.  They are not the default build
policy, but the previous PCC frontend/kernel blocker is fixed.  Representative
commands are:

```sh
make -C sys/mips/malta64 malta64.elf \
    MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc \
    MIPS_ROOTFS_CPU=vr4300 MIPS_ROOTFS_FLOAT=hard MIPS_ROOTFS_ENDIAN=big \
    MIPS_PCC_PROVIDER=cross

make -C sys/mips/malta unix.elf \
    MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc \
    MIPS_ROOTFS_CPU=mips32r2 MIPS_ROOTFS_FLOAT=hard MIPS_ROOTFS_ENDIAN=big \
    MIPS_PCC_PROVIDER=cross

make -C sys/mips/n64 kernel.z64 preflight.z64 \
    N64_KERNEL_COMPILER=pcc N64_USERLAND_COMPILER=pcc \
    N64_USERLAND_CPU=vr4300 N64_USERLAND_FLOAT=hard \
    N64_USERLAND_ENDIAN=big N64_ZSWAP=1

make -C sys/mips/n64 kernel.z64 \
    N64_KERNEL_COMPILER=pcc N64_USERLAND_COMPILER=pcc \
    N64_USERLAND_CPU=vr4300 N64_USERLAND_FLOAT=hard \
    N64_USERLAND_ENDIAN=big N64_ROOTFS_KBYTES=32768
```

The fixed 2026-07-06 PCC kernel issues were:

- PCC frontend handling of kernel enum prototypes and null pointer constants.
- MIPS inline-assembler constraint handling needed by kernel helpers.
- Kernel image linking with PCC output without accidentally carrying temporary
  `.bss` sections into the Malta rootfs blob.
- MIPS C integer arithmetic now uses non-trapping `addu`, `subu`, and `addiu`
  forms where the language requires wraparound or non-trapping behavior.
- MIPS `-fomit-frame-pointer` now removes `$fp` from PCC-generated kernel
  frames while preserving a frame pointer for functions that cannot be safely
  rewritten.  The 2026-07-06 follow-up fixed the pass2 ordering bug where an
  early optimizer call rewrote frame references before the backend knew that a
  later call/argument path required `$fp`, which produced mixed `$fp` prologues
  with `$sp`-relative frame references.

The Malta64 PCC hard-float kernel/rootfs QEMU gate booted with:

```text
ReBSD for Malta64: built on sash@sashz-mbp with pcc Portable C Compiler 1.2.0.DEVEL 20231021 for mips-unknown-rebsd, cpu=vr4300, float=hard, endian=big
PCC_SMOKE_ALL_FAILURES 0
PCC_SMOKE_ALL_RC:0
linpack-pcc: 12095.997 and 11946.666 KFLOPS
```

The Malta PCC hard-float kernel/rootfs QEMU gate booted with:

```text
ReBSD for Malta: built on sash@sashz-mbp with pcc Portable C Compiler 1.2.0.DEVEL 20231021 for mips-unknown-rebsd, cpu=mips32r2, float=hard, endian=big
PCC_SMOKE_ALL_FAILURES 0
PCC_SMOKE_ALL_RC:0
linpack-pcc: 10996.365 and 10692.598 KFLOPS
```

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
  MIPS target contract.
- Locale-aware multibyte, UTF-8, C++ startup, TLS, and shared-library behavior
  are intentionally not hidden behind placeholder functions.

Soft-float mode uses an ABI-specific compiler runtime archive staged as
`/usr/lib/softfloat/libpcc.a`.  The normal hard-float archive remains
`/usr/lib/libpcc.a`.  Keeping both archives in the rootfs and SDK lets target
PCC and future kernel builds select the correct compiler helpers without
wrapper scripts or rebuilding the SDK.

`src/dev/pcc/pcc-libs/csu` provides upstream PCC `crtbegin.o` and `crtend.o`,
but ReBSD does not enable them in the default C link path.  The current static
ELF toolchain and libc startup support flat linker-defined ctor/dtor ranges,
which is enough for C constructor/destructor attributes in the C gate.

## Toolchain Requirements

The ReBSD MIPS toolchain is part of the supported PCC target:

- `ld` accepts `--elf`, `-EB`, `-EL`, `-T`, `-L`, `-l`, and
  `--sysroot[=DIR]`, defaults sysroot to `/`, and applies sysroot only to
  standard library directories.
- `ld` reads installed ELF scripts from `/usr/lib/ldscripts` in rootfs builds
  and from `$prefix/mips*-rebsd/lib/ldscripts` in cross SDK builds.
- `ld` preserves 8-byte text/data/BSS alignment needed by MIPS FPU literals and
  `double` storage.
- `as` accepts `--elf`, `-EB`, `-EL`, and the GCC/PCC MIPS syntax used by the
  imported backend, including ctor/dtor sections, `.init_array`/`.fini_array`,
  `.eh_frame`, `neg`/`negu`, absolute `la`, `symbol+-offset` expressions,
  UTF-8 symbol bytes, and the relocation forms needed by PCC-built shell/login
  paths.
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
enabled for both `vr4300` and `mips32r2` userlands.  Caller-clobbered FPRs are
now represented as temporaries, variable 64-bit shifts are lowered inline,
unsigned 32-bit FP conversions use the correct helpers, and common symbols
carry their required assembler alignment.

## Validation Gates

The current C gate is green in these environments:

- Host cross smoke with ReBSD `as`/`ld`.
- Malta QEMU PCC userland, including `mips32r2` hard-float and soft-float
  `/root/pcc-smoke-all.sh` plus LINPACK smoke.
- Malta64/R4000 QEMU PCC userland, including hard-float and soft-float LINPACK
  smoke for the `vr4300` ABI, using
  `qemu-system-mips64 -M malta -cpu R4000 -m 64M -nographic`.
- Real N64 hardware normal hard-float PCC rootfs smoke from the earlier gate.
  The Phase 4 spill-cost hard-float a.out debug ROM also passed on real hardware
  with `N64_PCC_DEBUG_RUNNER_RC 0` and `N64_PCC_DEBUG_RC_END`.  A fresh
  soft-float hardware pass is still pending.
- N64 UART-only boot isolation on real hardware passed on 2026-07-06 for
  PCC/raw swap, PCC/zswap, GCC/raw swap, and GCC/zswap.  The minimal debug
  rootfs includes `/bin/login`; without it `getty` respawns after username
  entry because `/bin/login` cannot be executed.
- N64 hard-float PCC kernel/userland full rootfs ROM build with `N64_ZSWAP=1`.
  The ROM boots on real hardware to root login and basic shell use; `ls -l /`
  and `uptime` work.  Full runtime validation is still pending because
  `uname -a` currently triggers a kernel `TLB load/fetch` panic after login.

The 2026-07-05 QEMU smoke matrix finished with:

```text
malta64 vr4300  hard  PCC_SMOKE_ALL_FAILURES 0  PCC_SMOKE_ALL_RC:0
malta64 vr4300  soft  PCC_SMOKE_ALL_FAILURES 0  PCC_SMOKE_ALL_RC:0
malta   mips32r2 hard  PCC_SMOKE_ALL_FAILURES 0  PCC_SMOKE_ALL_RC:0
malta   mips32r2 soft  PCC_SMOKE_ALL_FAILURES 0  PCC_SMOKE_ALL_RC:0
```

The clean 2026-07-10 matrix additionally covered MaltaEL and repeated every
profile with both the kernel and rootfs selected as PCC:

```text
malta64 vr4300   hard  PCC_SMOKE_ALL_FAILURES 0  PCC_SMOKE_ALL_RC:0
malta64 vr4300   soft  PCC_SMOKE_ALL_FAILURES 0  PCC_SMOKE_ALL_RC:0
malta   mips32r2 hard  PCC_SMOKE_ALL_FAILURES 0  PCC_SMOKE_ALL_RC:0
malta   mips32r2 soft  PCC_SMOKE_ALL_FAILURES 0  PCC_SMOKE_ALL_RC:0
maltael mips32r2 hard  PCC_SMOKE_ALL_FAILURES 0  PCC_SMOKE_ALL_RC:0
maltael mips32r2 soft  PCC_SMOKE_ALL_FAILURES 0  PCC_SMOKE_ALL_RC:0
```

The same source state passed `321` host compile checks with only the three
documented expected failures, `281/281` cross runtime checks, and the native
PCC gate with `291` successful compilations, `30` expected compile failures,
and `281/281` runtime passes.

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

The current hard-float PCC full zswap ROM has reached root login on real N64
hardware and can run basic shell commands.  It is slow on hardware, and the
`uname -a` panic suggests an unresolved timing/race or interrupt-path issue
rather than a rootfs packaging failure.

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
supported by the current static ELF target.  Keep the corresponding upstream
PCC tests expected-fail unless ReBSD grows a concrete ABI and runtime policy for
them.

Locale-aware multibyte and UTF-8 wide-character semantics are not part of the
current PCC milestone.  The existing wide-character libc surface documents the
current single-byte execution character model.

## Build Artifacts

Generated rootfs stages, native PCC build trees, port object files, ROM images,
catman outputs, and temporary manifests are build artifacts.  They must stay
ignored unless a generated file is already a tracked project source input.
