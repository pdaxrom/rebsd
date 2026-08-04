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

## Late MIPS Scheduling

Milestone C begins with the existing four-instruction load-delay reorder in
the compiler driver.  Commit `11f66467` enables it for MIPS32R2 as well as
VR4300.  It recognizes a parsed independent shift, move, signed-imm16 `li`, or
simple GPR ALU operation before a load and moves it between that load and the
first dependent use.  Register conflicts, control transfers, memory
candidates, unknown syntax, and instructions already occupying a delay slot
are rejected.

VR4300 uses this window to replace an explicit load-delay `nop`; MIPS32R2 uses
the same dependency proof to hide interlocked load latency.  The later branch
peephole can consume an exposed independent operation as a branch delay-slot
instruction.  On Linpack this changes 47 MIPS32R2 regions and fills two branch
delay slots, while VR4300 assembly remains byte-identical.  The host smoke
contains a permanent MIPS32R2 ordering probe.

Commit `668a7d88` adds the second short window.  A pure parsed GPR operation
may move across one independent load into the delay slot of a following
integer branch.  The branch still executes the candidate on both paths.
Candidates touching the load, branch condition, `$sp`, or `$ra` are rejected,
as are labels, unknown instructions, memory candidates, and CPU profiles
other than VR4300 or MIPS32R2.  This fills two additional Linpack branch slots
on each CPU without changing load/store counts.  A permanent VR4300 host probe
checks the final `load; branch; addiu` order.

Commit `0331d3cf` adds the third bounded window.  It parses exact binary FPU
read/write sets and the VR4300 add/sub/multiply/divide cycle counts, then moves
one following `lw` between an immediately dependent FPU producer/consumer
pair.  The consumer replaces the load-delay `nop`; no two memory operations
change order.  This separates 11 Linpack pairs on both VR4300 and MIPS32R2.
Generic MIPS3/R4000 remains unchanged, and permanent host probes cover all
three target cases.

These remain bounded scheduler substeps.  The pass still has no general memory
alias model, general longer-window scheduler, unary/conversion FP latency
model, or cross-block scheduling.  The VR4300 multiply repair remains the
final pass, is active by default under `-mfix4300`, and is disabled only by
`-mno-fix4300`.  C3 passed cross/native regressions and all six full
PCC-kernel/PCC-rootfs QEMU profiles.  Its N64 hard-float image is
`sys/mips/n64/builds/20260711-milestone-c3-fpu-latency-schedule/pcc-debug.z64`;
real hardware validation passed on 2026-07-11 with
`N64_PCC_DEBUG_END 0`, `N64_PCC_DEBUG_RUNNER_RC 0`, and the final
`N64_PCC_DEBUG_RC_END` marker.  Full details and hashes are in
`docs/PCC_MIPS_SCHEDULER_REPORT.md`.

The 2026-07-11 clean-build audit later invalidated the C2 through D1 hardware
runs as PCC-kernel evidence.  `N64_KERNEL_COMPILER` was absent from the N64
build-mode stamp, allowing stale GCC kernel objects to survive a compiler
switch while `vers.o` reported `with pcc`.  The images still validate PCC
userland/debug execution.  Commit `776e41af` adds the compiler to the stamp;
subsequent PCC-kernel hardware claims require a clean build or a
`.build-mode.pcc.*` stamp.

Commit `3c2092b7` closes the current conservative scheduler scope with a
five-instruction HI/LO window.  One exactly parsed pure GPR operation after
`mflo`/`mfhi` may move into a two-nop multiply gap when it does not touch the
HI/LO result, `$sp`, or `$ra`.  VR4300 Linpack fills two such gaps and moves
from `2473/155` to `2471/153` instructions/nops; MIPS32R2 is byte-identical
because it already uses one-instruction `mul`.  Cross/native regressions and
all six PCC-kernel/PCC-rootfs QEMU profiles pass.  The N64 hard-float image is
`sys/mips/n64/builds/20260711-milestone-c4-hilo-gap-schedule/pcc-debug.z64`;
real hardware validation passed on 2026-07-11 with `N64_PCC_DEBUG_END 0`,
`N64_PCC_DEBUG_RUNNER_RC 0`, and the final `N64_PCC_DEBUG_RC_END` marker.
The supplied final marker had no numeric value, so none is inferred.

Commit `34c0b9d8` begins Milestone D.  Under explicit
`-fomit-frame-pointer`, a non-leaf function with register-only calls may use a
fixed 16-byte o32 home area below all private frame slots.  This removes the
per-call `$sp` restoration and permits the existing omit-FP rewrite in cases
that previously forced `$fp`.  Stack arguments, aggregates, `alloca`, varargs,
and raw frame addresses remain on the old path.  Normal Linpack is
byte-identical; omit-FP Linpack removes 28 VR4300 and 34 MIPS32R2 instructions.
Cross/native regressions and all six PCC-kernel/PCC-rootfs QEMU profiles pass.
The N64 artifact and full invariants are documented in
`docs/PCC_MIPS_FRAME_REPORT.md`.  Its PCC userland/debug hardware validation
passed on 2026-07-11, but the clean-build audit found stale GCC kernel objects,
so that run is not PCC-kernel validation.  A corrected clean D2 image is the
next real-hardware gate.  That corrected clean-PCC D2 image subsequently
passed on real N64 with `N64_PCC_DEBUG_END 0`, `N64_PCC_DEBUG_RUNNER_RC 0`,
and the final `N64_PCC_DEBUG_RC_END` marker.

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
  The N64 PCC hard-float full ROM build also completes for hardware smoke, and
  the UART-only real-hardware boot isolation matrix passed with PCC and GCC
  kernels and both raw and compressed RAM-device profiles.
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
  matrix stays stable.  Default N64 startup creates compressed `/dev/ram1`
  from userland so its logical capacity can exceed the dynamic backing store without
  reducing the 4 MiB Expansion Pak user window.
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
    N64_USERLAND_ENDIAN=big

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
  PCC and GCC kernels with raw and compressed RAM-device profiles.  The minimal debug
  rootfs includes `/bin/login`; without it `getty` respawns after username
  entry because `/bin/login` cannot be executed.
- N64 hard-float PCC kernel/userland full rootfs ROM build with runtime
  compressed `/dev/ram1`.
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

The Malta64 low-memory swap check on 2026-07-06 used the former fixed-pool
N64-sized profile with the rootfs linked at physical `0x00800000`.  That
historical image reported `phys mem = 8192 kbytes`, `user mem = 4096 kbytes`,
`root size = 16384 kbytes`, and `swap size = 4608 kbytes`;
`/root/pcc-smoke-all.sh` finished with
`PCC_SMOKE_ALL_FAILURES 0` and `PCC_SMOKE_ALL_RC:0`.  The `linpack-pcc` result
inside that run was `12096.004` and `12172.076` KFLOPS.

The 32-bit Malta low-memory swap check on 2026-07-06 used the same historical profile with
`MIPS_ROOTFS_CPU=mips32r2` and hard-float PCC userland.  Boot again reported
`phys mem = 8192 kbytes`, `user mem = 4096 kbytes`, `root size = 16384 kbytes`,
and `swap size = 4608 kbytes`; `/root/pcc-smoke-all.sh` finished with
`PCC_SMOKE_ALL_FAILURES 0` and `PCC_SMOKE_ALL_RC:0`.  The `linpack-pcc` result
inside that run was `10751.999`, `10294.467`, and `10132.773` KFLOPS.

On real 8 MiB N64 hardware, the native utility workload needs more writable
temporary space than the original 512 KiB `/var` RAM disk can provide.  N64
startup therefore creates `/dev/ram0` with 1 MiB of dynamically allocated VM
backing.  The 4 MiB user window is
unchanged; with compressed `/dev/ram1` the expected 8 MiB boot report becomes
`user mem = 4096 kbytes` and `swap size = 3584 kbytes`.
The workload serializes its persistent outputs within that device: only
the first compiler's `basename` binary is retained for the cross-driver
comparison, while copied sources, `sum`, `size`, and their output files are
not kept across later compiler invocations.  This leaves the remaining space
for the active `cpp`, `ccom`, assembler, and linker temporary files.

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
Default hardware ROMs configure `/dev/ram1` at runtime; raw-device regression
uses `ramctl` without the `compression` option.  The smoke covers native PCC compile/link/run,
shell/login-sensitive paths, repeated `ccom`, libc/math/wide-character policy
smoke tests, DHCP receive support, real small utility rebuilds, and Linpack
binaries built with both GCC and PCC.

The 2026-07-11 SSA multiply-reuse pass extends local value numbering only for
exact integer `MUL` subtrees composed of SSA TEMPs and unnamed constants.
Repeated address-scale expressions may be reused across memory accesses because
the scalar SSA operands are immutable.  Calls and asm split the region, and a
candidate is rejected when one of its operands is defined in the containing
tree.  The first occurrence is materialized only after a duplicate has been
proved, so unique multiplies gain no temporary or instruction.

On Linpack this removes 11 integer multiplies.  VR4300 falls from 2371 to 2349
instructions and from 61 to 50 HI/LO multiply/divide sequences; MIPS32R2 falls
from 2473 to 2462 instructions and from 59 to 48 direct `mul` instructions.
Loads, stores, branches, jumps, and nops are unchanged.  The positive assembly
gate requires one multiply for two identical scaled addresses, while a changed
SSA index must retain two.

Cross regression compiled 329 of 332 cases with only the three established
expected failures.  Native PCC compiled 302 cases, observed 30 expected
compile failures, and passed 292/292 runtime cases.  All six PCC-kernel and
PCC-rootfs Malta64/Malta/MaltaEL hard/soft profiles reported
`PCC_SMOKE_ALL_FAILURES 0` and `PCC_SMOKE_ALL_RC:0`.  Kernel coverage includes
`-msoft-float -fomit-frame-pointer`; the pass is independent of the VR4300
`-mfix4300` erratum repair.

Real N64 validation passed with all final markers at zero.  The stable 16-rep
row measured GCC at 4411.954 KFLOPS and PCC at 3094.330 KFLOPS, or 70.14% of
GCC.  PCC was 0.20% below its E3 result, within run-to-run timing noise.  This
shows that removing the static duplicates reduces code size but does not
address Linpack's dominant dynamic cost; loop-carried address strength
reduction remains necessary.

The 2026-07-12 induction-strength pass implements that next step for expensive
integer multiplication.  It recognizes only a canonical SSA loop with one
external predecessor, one dominated latch, a `+1` or `-1` phi update, and an
invariant integer stride defined before the preheader.  It creates one scaled
phi, computes the initial scale on the entry edge, advances it with an add or
subtract on the back edge, and replaces exact `index * stride` uses dominated
by the loop header.  A zero initial index does not emit a preheader multiply.
Only one stride is reduced per induction phi to bound register pressure.

The pass is disabled unless the target opts in.  MIPS enables it when
`MIPS_CAP_MUL3` is absent, covering VR4300/MIPS III HI/LO multiplication.
MIPS32R2 remains byte-identical to the previous milestone because its direct
`mul` is cheap and an unrestricted prototype reduced QEMU Linpack by about
3.8% through added live ranges and spills.  The target-specific policy remains
a two-line cost hook instead of being duplicated in the MIPS optimizer.

The 2026-07-12 F1 frontend step adds bounded constant-argument specialization
without enabling ReBSD's disabled general automatic `-xinline` path.  The
driver enables it only for hosted `-O2` and above; `-O`, `-Os`, and
`-ffreestanding` retain their previous code path.  This keeps the PCC kernel,
which is compiled with `-msoft-float -fomit-frame-pointer -O2
-ffreestanding`, outside the transform.

A direct call to a not-yet-defined file-local function may select one clone.
The call must have no more than six arguments and useful integer constants in
the range `-1..1`; calls with five or six arguments require at least two such
constants.  The frontend saves the later function body with PCC's existing
inline representation, substitutes constants into the copied formal TEMP
initializers, and lets normal SSA folding remove dead paths.  Calls after a
definition are not cloned.  A generic call or function-address use causes the
original body to be emitted as well, while an all-specialized function emits
only the clone.

Linpack specializes `idamax`, both `dscal` variants, both `daxpy` variants, and
both `ddot` variants.  It does not clone `dgefa` or `dgesl`.  VR4300 assembly
falls from E5's 2375 instructions, 127 nops, and 71622 bytes to 2289
instructions, 117 nops, and 70371 bytes.  MIPS32R2 hard-float assembly is 2412
instructions, 103 nops, and 69823 bytes, identical for big and little endian.

The permanent `ssaspecialize001` regression covers a specialized direct call,
a variable generic call, and a function pointer.  Cross regression passes
331/334 compilations with the three established expected failures.  Native
VR4300 regression passes 294/294 runtime cases with zero unexpected failures.
All six PCC-kernel/PCC-rootfs Malta profiles pass full `pcc-smoke-all.sh`,
including hard/soft float and both MIPS32R2 endian modes.  The clean hardware
image is
`sys/mips/n64/builds/20260712-milestone-f1-static-specialization-gcc-kernel/pcc-debug.z64`,
SHA-256
`30be99a022c0c10e3e2eb64b914071ba09ee252b49f095e00fdfc06eefcfb685`.
It uses a GCC kernel, PCC VR4300 hard-float a.out userland, default
`-mfix4300`, and isolated GCC/PCC Linpack runtimes.  Physical N64 validation
passes: the stable 16-rep row is 4416.677 GCC and 3178.200 PCC KFLOPS, putting
PCC at 71.96% of GCC and 0.48% above E5.  Both Linpack statuses and the two
numeric debug statuses are zero; the final `N64_PCC_DEBUG_RC_END` marker is
present without a numeric value.  This closes the scoped Milestone F.

VR4300 Linpack falls from 48 to 23 integer multiplies and from 2349 to 2337
instructions.  It adds eight loads and three stores from register pressure,
so the hardware benchmark remains the deciding performance gate.  All six
host profiles and all six full PCC-kernel/PCC-rootfs QEMU profiles pass.
Native VR4300 regression passes 293/293 runtime tests, including canonical
ascending/descending loops and a variable-step negative case.

Real N64 validation passed with every final marker at zero.  The stable 16-rep
row measured GCC at 4412.343 KFLOPS and PCC at 3163.024 KFLOPS, or 71.69% of
GCC.  GCC changed by only 0.009% from E4 while PCC improved by 2.22%, making
the induction transform the first post-E3 optimization with a measurable N64
Linpack gain.  It still leaves the compiler below the 75-85% medium-term goal,
so the next work must target call/inlining overhead rather than generalizing
the loop pass without evidence.

The current hard-float PCC full compressed-RAM ROM has reached root login on real N64
hardware and can run basic shell commands.  It is slow on hardware, and the
`uname -a` panic suggests an unresolved timing/race or interrupt-path issue
rather than a rootfs packaging failure.

The 2026-07-12 G2 hard-float step keeps o32 `double` and `long double`
parameters received through GPR pairs in compiler temporaries for optimized,
non-variadic VR4300 and MIPS32R2 functions.  ABI materialization still occurs
before promotion.  Generic MIPS3/R4000, soft-float, varargs, and leading
F12/F14 arguments retain their previous behavior.

The same step adds one exact late-scheduler rule for an independent
`mov.s`/`mov.d` in an FPU load-use gap.  It proves disjoint register sets and
both consumer dependencies, does not move memory operations across each
other, and runs in two bounded passes to compose with the existing binary-FPU
window.  VR4300 Linpack removes 15 repeated loads with no nop or store growth;
alternating Malta64 A/B runs improve by 0.96%.  Cross and native regression
pass 294/294 runtime candidates, and all six PCC-kernel/PCC-rootfs full QEMU
profiles pass.  Soft-float Linpack assembly remains byte-identical to F1 for
both endian modes.  The default `-mfix4300` repair remains unchanged.  Its clean
GCC-kernel/PCC-hard-float-a.out-userland image is
`sys/mips/n64/builds/20260712-milestone-g2-fpu-param-temp-gcc-kernel/pcc-debug.z64`,
SHA-256
`9106cbd1b3cac395d35d4c243815a42426186199c19f73a58bc89d417abeb854`.
Physical N64 validation passes with zero GCC/PCC Linpack status and 15-digit
precision.  The stable 16-rep row is 3188.274 PCC versus 4411.854 GCC KFLOPS,
or 72.27%; PCC improves 0.32% over F1 while the GCC control changes -0.11%.
The two numeric debug status markers are zero, and the terminal
`N64_PCC_DEBUG_RC_END` marker is present without a numeric value.  This closes
G2 while leaving the broader Milestone G performance work open.

The G3 specialization step fixes the partial-parameter case left by F1.  A
five- or six-argument clone can have selected hard-float scalar stack
parameters promoted to compiler temporaries on VR4300 and MIPS32R2 while
unselected parameters keep their original storage.  Clone replay then replaces
those TEMP initializers with the selected constants, allowing SSA folding to
remove generic increment paths that previously remained in the clone.

Selected literal arguments are marked after prototype conversion.  MIPS omits
their argument-register assignments and stack stores but still advances every
o32 stack slot, so no private calling convention is introduced.  Soft-float,
generic MIPS3/R4000, varargs, `-Os`, and freestanding compilations retain the
G2 behavior.  MIPS32R2 BE/LE and VR4300 soft-float Linpack assembly are
byte-identical to G2.

VR4300 hard-float Linpack drops from 2291 to 2005 instructions, from 416 to
362 loads, from 226 to 202 stores, and from 117 to 95 nops.  Alternating
Malta64 A/B runs improve by 3.44%.  Cross regression produces 294 runtime
candidates, native VR4300 passes 294/294, and all six PCC-kernel/PCC-rootfs
full QEMU profiles pass with `-msoft-float -fomit-frame-pointer` kernels.  The
default `-mfix4300` repair remains unchanged.  Its clean
GCC-kernel/PCC-hard-float-a.out-userland image is
`sys/mips/n64/builds/20260712-milestone-g3-stack-specialization-gcc-kernel/pcc-debug.z64`,
SHA-256
`87ca5dce22afd995da205704126bf573d389224503f29f9b1e5a439c3c4fe748`.
The PCC Linpack is 40224 section bytes, 1136 fewer than G2; GCC Linpack remains
24600 section bytes and uses its separate GCC-built runtime.

Physical N64 validation passes with zero GCC/PCC Linpack status and 15-digit
precision.  The stable 16-rep row is 3186.275 PCC versus 4411.809 GCC KFLOPS,
or 72.22%.  PCC is 0.06% below G2 while the GCC control changes -0.001%, so
the Malta64 QEMU gain does not transfer to hardware.  The two numeric debug
markers are zero; `N64_PCC_DEBUG_RC_END` is present without a numeric value.
G3 remains as a specialization correctness and code-size improvement, not as
a measured VR4300 performance gain.

The G4 step reuses the existing local SSA scale CSE to materialize repeated
exact `TEMP << constant` expressions once per safe basic-block region.  A
target hook limits this to hard-float VR4300 and MIPS32R2; generic MIPS3 and
soft-float retain G3 generation.  VR4300 Linpack falls from 2005 to 1947
instructions and MIPS32R2 hard-float from 2124 to 2087, with no load, store,
nop, branch, or jump growth.  MIPS32R2 BE/LE hard counters match, and both
soft-float outputs are byte-identical to G3.

Three alternating Malta64 pairs improve the 256-repetition average from
13392.683 to 13461.499 KFLOPS, or 0.51%.  Cross regression produces 294
runtime candidates, native VR4300 passes 294/294, and all six
PCC-kernel/PCC-rootfs full QEMU profiles report zero failures.  Kernels retain
`-msoft-float -fomit-frame-pointer`; VR4300 erratum option behavior is
unchanged.  The small QEMU gain is not treated as a hardware result; physical
N64 comparison remains open.  The clean GCC-kernel/PCC-hard-float-a.out image
is
`sys/mips/n64/builds/20260712-milestone-g4-constant-shift-cse-gcc-kernel/pcc-debug.z64`,
SHA-256
`23a82ec406f916b4f040cb3c14281368901afe21784cfa74fca8af542123fcba`.
Its PCC Linpack is 39968 section bytes, 256 fewer than G3; GCC Linpack remains
24600 section bytes and uses its separate GCC-built runtime.

Physical G4 validation passes with zero GCC/PCC Linpack status and 15-digit
precision.  The stable 16-rep row is 3316.659 PCC versus 4411.821 GCC KFLOPS,
or 75.18%.  PCC improves 4.09% over G3 while the GCC control changes only
0.0003%, confirming that repeated constant-shift CSE transfers to real
VR4300 hardware.  Both numeric debug markers are zero; the final
`N64_PCC_DEBUG_RC_END` marker is present without a numeric value.  This closes
the medium-term 75-85% gate while leaving the 90-100% stretch target open.

G5 aligns optimized PCC userland with the existing ReBSD MIPS GCC target
policy: `-O2+` defaults to frame-pointer omission on VR4300 and MIPS32R2 after
the driver has resolved `-march`/`-mtune`.  Explicit
`-fno-omit-frame-pointer` restores the prior output; `-O0`, `-O1`, `-Os`, and
generic MIPS3/R4000 remain unchanged.  This uses PCC's existing pass2 support
and does not add a Linpack-specific flag.

VR4300 hard-float Linpack drops from 1947 to 1866 instructions and removes ten
loads and ten stores.  MIPS32R2 hard-float drops from 2087 to 1983
instructions; BE/LE counters match.  Soft-float also passes in both endian
modes.  Five alternating Malta64 pairs improve by 1.49%.  Cross/native
regression passes 294 runtime candidates and all six PCC-kernel/PCC-rootfs
QEMU profiles report zero failures.  Erratum option behavior is unchanged;
physical N64 validation passed on 2026-07-12.  The published
GCC-kernel/PCC-hard-float a.out image is
`sys/mips/n64/builds/20260712-milestone-g5-o2-frame-omit-gcc-kernel/pcc-debug.z64`,
SHA-256
`a71270c6bea582df7cfe3c104fd1f29e17dadfdda17eb3eaa4d92ef2ec0b4f19`.
Its PCC Linpack is 39632 section bytes, 336 fewer than G4; GCC Linpack remains
24600 section bytes and is byte-identical to G4.

The hardware run used a fresh out-of-tree rebuild with SHA-256
`6624001a706a305045b2d4c0e25e577249eed0e645fe151f7bf435c1e430a305`;
its kernel ELF is byte-identical to the published G5 kernel.  The 16-repetition
row measured 4367.264 GCC and 3278.433 PCC KFLOPS, putting PCC at 75.07% of
GCC.  Both Linpack statuses, `N64_PCC_DEBUG_END`, and
`N64_PCC_DEBUG_RUNNER_RC` were zero, followed by `N64_PCC_DEBUG_RC_END`.

G6 extends the existing bounded specialization policy without adding an
inliner or a MIPS-only IPA pass.  When a non-varargs static function has
already been selected for hard-float VR4300/MIPS32R2 specialization, its
stack-passed scalar and pointer parameters are promoted to PCC TEMPs at entry.
The o32 parameter slots and external calling convention do not change.  This
turns repeated loop-body loads of unselected pointer parameters into one entry
load, while selected constant parameters continue to use the G3 path.

VR4300 and MIPS32R2 hard-float Linpack each remove 26 loads and 722 assembly
bytes.  Three clean alternating Malta64 512-repetition A/B pairs improve from
13550.962 to 13878.128 KFLOPS on average, or 2.41%.  Cross and native PCC
regressions pass all 294 runtime cases, and all six PCC-kernel/PCC-rootfs QEMU
profiles report `PCC_SMOKE_ALL_RC:0`.  Soft-float and generic MIPS3 are not
eligible.  The VR4300 multiplication workaround and the
`-mfix4300` / `-mno-fix4300` controls are unchanged.

The clean G6 hardware image uses a GCC kernel and PCC hard-float a.out
userland.  It is stored outside the source tree at
`/Users/sash/Work/N64/retrobsd-build/n64-g6-specialized-stack-params-kgcc-upcc-hard-aout/pcc-debug.z64`,
SHA-256
`2792c760271601826bc55aceac5e3d21c9744e6cb00489acf12e9e1bced1c3f7`.
It passes `fsutil --check`, has build stamp `.build-mode.gcc.1.0.0.1`, and
contains independently linked GCC and PCC Linpack binaries.  Physical N64
validation passes: the stable 16-repetition row measures 3404.961 PCC versus
4367.120 GCC KFLOPS, or 77.97%.  PCC improves 3.86% over G5 while the GCC
control changes by -0.003%.  Both Linpack statuses and the two numeric debug
statuses are zero; the terminal `N64_PCC_DEBUG_RC_END` marker is present.

G7 adds loop-carried pointer induction for repeated VR4300 hard-float affine
addresses.  The SSA pass converts `base + ((i+c) << scale)` families into a
pointer phi with short constant offsets and one latch increment, but only when
the base dominates the preheader and every byte offset fits a signed 16-bit
immediate.  VR4300 Linpack removes 97 instructions and three isolated Malta64
pairs improve 0.35%.  A measured MIPS32R2 version regressed 0.303%, so it is
disabled; soft-float, MIPS32R2, and generic MIPS3 output remain byte-identical
to G6.  The VR4300 erratum repair and `-mfix4300` / `-mno-fix4300` options are
unchanged.

Cross and native regression pass 294/294 runtime cases, and all six clean
PCC-kernel/PCC-rootfs QEMU profiles pass `pcc-smoke-all.sh`.  The G7 hardware
image is
`/Users/sash/Work/N64/retrobsd-build/n64-g7-pointer-induction-kgcc-upcc-hard-aout/pcc-debug.z64`,
SHA-256
`9ad25d3a132a85257ffcdb4408c464b4eadb3d17379923e983f96b75052f653e`.
It uses a GCC kernel and PCC hard-float a.out userland and contains separately
linked GCC and PCC Linpack binaries.  Physical validation passes: the stable
16-repetition row measures 3631.062 PCC versus 4453.809 GCC KFLOPS, or 81.53%.
PCC improves 6.64% over G6; both Linpack statuses and both numeric debug
statuses are zero, and the terminal `N64_PCC_DEBUG_RC_END` marker is present.

H0 adds a per-kernel Linpack profiler before the next optimization is chosen.
It builds independent GCC/PCC binaries for rolled and unrolled `daxpy`,
`ddot`, and `dscal`, plus `idamax`; value self-tests run before every timed
set.  Typed volatile calls prevent GCC from inlining the benchmark bodies
while PCC leaves them out of line.  A same-image Malta64/R4000 QEMU run puts
the PCC generic kernels between 96.8% and 101.4% of GCC.  QEMU does not model
the physical VR4300 timing gap closely enough to select the next scheduler
change, so the H0 N64 image is the decision gate.  Before that hardware gate,
the complete `pcc-smoke-all-runtime` suite passes with PCC kernels and PCC
root filesystems on Malta64, Malta, and Maltael, each in hard-float and
soft-float mode.  All six runs report `PCC_SMOKE_ALL_FAILURES 0` and
`PCC_SMOKE_ALL_RC:0`.

The corrected physical H0 run completes with all self-tests, Linpack statuses,
and final N64 debug markers zero.  The 16-repetition ordinary Linpack result is
3642.421 PCC versus 4367.881 GCC KFLOPS, or 83.39%.  The PCC/GCC per-kernel
ratios are 64.59% for `daxpy_r`, 85.35% for `daxpy_ur`, 52.35% for `ddot_r`,
77.63% for `ddot_ur`, 67.65% for `dscal_r`, 75.79% for `dscal_ur`, and 44.20%
for `idamax`.  Unlike QEMU, real VR4300 hardware therefore shows a substantial
gap in the generic loops themselves.  The next milestone starts with shared
loop code generation and scheduling visible in `idamax` and the rolled
kernels, not with a Linpack-specific `dgefa` or `dgesl` transformation.

Two correctness defects were found while bringing up H0.  A K&R-style
hard-float definition with a visible compatible prototype was receiving its
leading `double` through `$a0/$a1`, while callers correctly used `$f12`; PCC
now distinguishes that case from a genuinely unprototyped old-style
definition.  This fixes the ReBSD `fabs` definition and has direct ABI probes
for VR4300 and MIPS32R2.  Also, the Malta64 `rootfs-repack-kernel` path now
relinks `malta64-stage0-rootfs.o`; without it, the generated stage0 image
contained the kernel but omitted the rebuilt root filesystem.

The first H0 hardware image exposed a third correctness defect before the GCC
kernel self-test: GCC emitted `ldc1` for an 8-byte constant, but the final
a.out address was `0x00405ec4`.  The assembler honored `.align 3` within its
internal `.rodata` segment, then lost that alignment when it packed `.data`
and `.rodata` into the single a.out data segment.  The a.out assembler now
pads that boundary to the alignment requested by `.rodata`; a direct host
probe covers the case.  In the rebuilt GCC profiler the same constant is at
`0x00405ec8`.  The complete a.out assembler/linker/archive smoke and corrected
H0 hardware rerun both pass.

H1 starts from the physical per-kernel profile rather than QEMU timing.  The
initial PCC/GCC assembly comparison found that `idamax`, the worst measured
kernel at 44.20% of GCC, called libc `fabs` for each candidate and called it a
second time when updating the maximum.  PCC now uses a small MIPS target
builtin for plain `fabs` and the three `__builtin_fabs*` variants in hard-float
code.  Float clears the sign bit after `mfc1`; double and the o32 long-double
alias copy the pair with `mov.d`, clear the high-word sign bit, and write that
word back with `mtc1`.  Soft-float continues to call the corresponding libc
function, so its ABI and helper policy do not change.

The lowering intentionally avoids `abs.s` and `abs.d`.  The VR4300 manual
classifies ABS as an arithmetic operation and documents Invalid Operation for
signalling NaNs, while the data-transfer sequence preserves the NaN payload
and only clears its sign.  A MIPS extended-asm `%Hn` formatter names the odd
register containing the high word of an o32 FPR pair for both endian modes.
The regression checks exact bit patterns for negative zero, a finite value,
infinity, and sNaN.  In the Linpack profiler this removes six static `jal fabs`
sites from `idamax`, changes it into a leaf, reduces the stack frame from 64 to
24 bytes, and reduces its text from 412 to 380 bytes.

Before the H1 hardware gate, PCC kernels and PCC root filesystems pass the full
`pcc-smoke-all.sh` suite on Malta64, Malta, and Maltael in both hard-float and
soft-float mode.  The three hard-float runtime regression profiles pass
295/295 cases, including `misc__fabs001` on VR4300 and big- and little-endian
MIPS32R2.

The H1 hardware image uses a GCC debug-UART kernel and PCC VR4300 hard-float
a.out userland.  It is stored at
`/Users/sash/Work/N64/retrobsd-build/n64-h1-inline-fabs-kgcc-upcc-hard-aout/obj/sys/mips/n64/pcc-debug.z64`,
SHA-256
`3b1a2ea00c9c09c3f7f26439fad4b18672379989e7435ef1b268e97f4dc165f3`.
The image passes `fsutil --check`, has build stamp
`.build-mode.gcc.1.0.0.1`, and contains independent GCC/PCC ordinary and
per-kernel Linpack binaries.  The direct `pcc-debug.z64` Make goal now enables
debug UART mode before its parse-time parameter and ioconf conditionals are
evaluated; the former target-specific assignment was too late to affect those
conditionals.

Physical H1 validation passes with every Linpack status, kernel self-test, and
final debug marker zero.  PCC `idamax` improves from 2.898 to 4.815 Melem/s,
or 66.15%, and reaches 73.44% of GCC instead of H0's 44.20%.  The stable
ordinary Linpack row is 3673.524 PCC versus 4415.818 GCC KFLOPS, or 83.19%.
PCC is 0.85% faster than H0, but the GCC control in this run is also 1.10%
faster, so this does not establish an overall ratio improvement.  The other
kernel ratios remain 64.63% `daxpy_r`, 84.78% `daxpy_ur`, 52.35% `ddot_r`,
77.58% `ddot_ur`, 67.65% `dscal_r`, and 75.85% `dscal_ur`.  The next isolated
milestone therefore remains generic rolled-loop FP accumulator, reload, and
induction code generation rather than another `idamax` special case.

H2 addresses the loop-carried accumulator without adding a loop-specific
transform.  In hard-float mode, the MIPS `cisreg()` hook now permits
`float`, `double`, and the o32 `long double` alias to become optimizer TEMPs.
The normal address-taken analysis still keeps escaped locals in memory, and
soft-float returns through the old integer/stack path.  For rolled Linpack
`ddot`, the local accumulator remains in an FPR across the loop: static code
falls from 74 to 64 instructions and all seven FP accesses through the stack
disappear.  Across the complete per-kernel benchmark, instruction count falls
from 3238 to 3175 and FP stack accesses from 189 to 110.

The longer FPR lifetimes exposed a pre-existing correctness gap in MIPS table
patterns that emit runtime helper calls from conversion templates.  They are
not CALL nodes, so the generic call-clobber path does not see them.  Their
`NEEDS` sets now name every o32 caller-saved GPR and FPR; patterns returning an
FP result use a separate set that reserves `F0` as the result while clobbering
`F2` through `F18`.  This forces a live local to be saved and restored around
helpers such as `__floatunsdidf`, while leaf accumulators remain register-only.
A focused regression covers the register accumulator, an address-taken local,
and a local live across the helper.  The host assembly smoke checks VR4300 and
MIPS32R2 hard-float output plus the unchanged VR4300 soft-float path.

The three hard-float native profiles pass 296/296 runtime cases on Malta64,
Malta, and Maltael.  Full `pcc-smoke-all.sh` passes with PCC kernels and PCC
root filesystems for VR4300 big-endian hard/soft, MIPS32R2 big-endian hard/soft,
and MIPS32R2 little-endian hard/soft; every run reports
`PCC_SMOKE_ALL_FAILURES 0` and `PCC_SMOKE_ALL_RC:0`.  QEMU timing remains
correctness evidence only.  H2 does not alter multiply scheduling or the
default `-mfix4300` policy; `-mno-fix4300` remains the explicit opt-out.

The H2 hardware image uses a GCC debug-UART kernel and PCC VR4300
hard-float a.out userland, with independent GCC/PCC ordinary and per-kernel
Linpack binaries.  It is stored at
`/Users/sash/Work/N64/retrobsd-build/n64-h2-fp-local-temp-kgcc-upcc-hard-aout/obj/sys/mips/n64/pcc-debug.z64`,
has size 6619136 bytes and SHA-256
`b8111a4e59d135e57e01869a11d691bde26d2e1282f8d04343e7ca1084e01165`.
The image passes `fsutil --check` and has build stamp
`.build-mode.gcc.1.0.0.1`.  Physical validation passes both kernel self-tests,
all numeric Linpack and debug return codes are zero, and terminal
`N64_PCC_DEBUG_RC_END` is present.  Stable ordinary Linpack is 3706.069 PCC
versus 4413.155 GCC KFLOPS, or 83.98%; this is 0.89% faster than H1 PCC and
improves the PCC/GCC ratio by 0.79 percentage points.  PCC `ddot_r` improves
from 3.161 to 3.521 Melem/s and reaches 58.80% of GCC instead of 52.35%;
`ddot_ur` reaches 80.45%.  Register promotion also keeps the `idamax` maximum
in an FPR, improving that kernel from 4.815 to 5.333 Melem/s and from 73.44%
to 81.36% of GCC.  This closes the H2 physical and commit gates; the 90%
overall stretch target remains open.

H3 extends the existing target-independent G7 pointer-induction pass for the
remaining single-use indexed FP address in rolled loops.  The normal pass
still requires two uses of an affine address.  A small target policy hook lets
VR4300 hard-float accept one use when the base points to `float`, `double`, or
the o32 `long double` alias.  In rolled `daxpy`, PCC now carries both `dx` and
`dy` pointers instead of rebuilding `dx + (i << 3)` on every iteration.

The restriction is deliberate.  An unrestricted one-use prototype added four
loads and four stores by extending integer-pointer lifetimes in `dgefa`.
The FP-only form reduces complete VR4300 Linpack from 3136 to 3117 static
instructions and by 394 assembly bytes with unchanged load, store, `nop`,
branch, and jump counts.  VR4300 soft-float, generic MIPS3, integer pointers,
and MIPS32R2 keep the old two-use threshold.  The measured G7 MIPS32R2 form
regressed 0.303%, so it remains disabled pending positive runtime evidence.

All six PCC-kernel/PCC-rootfs hard/soft profiles pass the complete
`pcc-smoke-all.sh` suite with zero failures and final RC zero.  Native
hard-float regression passes 296/296 runtime cases on Malta64, Malta, and
Maltael, including the new single-use FP and integer controls.  QEMU timing is
correctness evidence only; the physical N64 commit-gate result is recorded
below.  The change does not affect multiply scheduling, default `-mfix4300`,
or the `-mno-fix4300` opt-out.

The H3 physical artifact uses a GCC debug-UART kernel and PCC VR4300
hard-float a.out userland with independent GCC/PCC ordinary and per-kernel
Linpack binaries.  It is stored at
`/Users/sash/Work/N64/retrobsd-build/n64-h3-fp-single-pointer-kgcc-upcc-hard-aout/obj/sys/mips/n64/pcc-debug.z64`,
has size 6619136 bytes and SHA-256
`07162ebf10802a6a7d41c9ec3fe7853fb96d01e6d32562d53aca2b63d8a5684f`.
The image passes `fsutil --check` and has build stamp
`.build-mode.gcc.1.0.0.1`.  Physical validation passes both self-tests, all
benchmark and numeric debug statuses are zero, and terminal
`N64_PCC_DEBUG_RC_END` is present.  The stable ordinary row is 3693.268 PCC
versus 4410.514 GCC KFLOPS, or 83.74%; PCC is 0.35% below H2, so H3 does not
establish an overall Linpack gain.  The targeted rolled paths do improve:
`daxpy_r` rises 7.50%, from 3.333 to 3.583 Melem/s, and reaches 69.06% of GCC;
`ddot_r` rises 17.84%, from 3.521 to 4.149 Melem/s, and reaches 69.29% of GCC.
This closes H3's narrow physical and commit gates while the 90% overall target
remains open.

H4 removes redundant hard-float copies with a bounded assembly postpass rather
than enlarging the MIPS table or adding global scheduler state.  A simple
destructive `MUL` table prototype did not remove the copies, and an `RLEFT`
prototype incorrectly recolored a result without a move, so both were
discarded.  The accepted pass recognizes only `mov.s`/`mov.d` followed within
12 straight-line instructions by a supported three-operand FP binary
operation.  It forwards one use and removes the copy only when a later complete
write proves the copied temporary dead.

The proof is conservative around labels, directives, control transfers,
unknown instructions, overlapping FPR masks, live uses, partial writes to a
double register pair, and branch delay slots.  It is enabled for VR4300 and
MIPS32R2; soft-float output has no matching instructions and is unchanged.
VR4300 multiply-errata repair remains the final postpass, so default
`-mfix4300` checks the transformed stream and `-mno-fix4300` still disables
only that workaround.

Across per-kernel Linpack, H4 removes 23 of 33 `mov.s`/`mov.d` instructions,
reducing total static instructions from 3117 to 3094 and assembly size from
96359 to 95255 bytes with unchanged nops, loads, stores, branches, and jumps.
All six PCC-kernel/PCC-rootfs hard/soft smoke profiles pass on Malta64, Malta,
and MaltaEL; a second clean MaltaEL hard build also passes.  Native hard-float
regression passes 296/296 runtime cases on all three boards.

The physical artifact is
`/Users/sash/Work/N64/retrobsd-build/n64-h4-fpu-copy-forward-kgcc-upcc-hard-aout/obj/sys/mips/n64/pcc-debug.z64`,
size 6619136 bytes, SHA-256
`bc937face6146d435a1e063f9582d7f0e0cd209b145f7dcb9eddffdc2f5dc6ea`.
It uses a GCC kernel and PCC VR4300 hard-float a.out userland, passes all
`fsutil --check` phases, and contains independent GCC/PCC ordinary and
per-kernel Linpack binaries.  Physical validation passes both kernel self-tests
and all benchmark/debug return codes.  Stable ordinary Linpack is 3783.781 PCC
versus 4412.843 GCC KFLOPS, or 85.74%; PCC improves 2.45% over H3 and the ratio
gains 2.01 percentage points.  `daxpy_r`, `daxpy_ur`, and `dscal_ur` improve
4.07%, 4.73%, and 1.40%, respectively.  This closes the H4 physical and commit
gates while the 90% overall target remains open.

H5 adds a general MIPS compiler corpus to prevent later work from being
selected only by Linpack.  Matching GCC/PCC executables cover integer
arithmetic and constant division, branches and switches, direct and libc
memory operations, calls, 64-bit pairs, single/double precision, and
FP/integer conversion.  Every kernel checks a deterministic result before
reporting adaptive timing.  The N64 debug runner executes both compiler
variants with separately built GCC and PCC libc/compiler runtimes.

The corpus exposed two backend correctness errors.  Long-long simple
operations could partially overlap their result and source pairs; the table
now requires a distinct result pair and fixes the `$t6`/`$t7` overlap map.
Hard-float signed FP-to-integer conversion used rounding-mode-dependent
`cvt.w.s/d`; it now emits `trunc.w.s/d`, while unsigned conversion uses the
existing full-range helper calls.  Optimized regressions lock down both
failures.

Malta64 hard-float runtime regression passes 298/298.  Full
`pcc-smoke-all.sh` passes for Malta64 VR4300, Malta MIPS32R2, and MaltaEL
MIPS32R2 in hard- and soft-float mode; all six runs report general benchmark
self-test zero, zero smoke failures, and final RC zero.  The H5 physical
candidate is
`/Users/sash/Work/N64/retrobsd-build/n64-h5-general-corpus-kgcc-upcc-hard-aout/obj/sys/mips/n64/pcc-debug.z64`,
size 8716288 bytes, SHA-256
`6370f6402bc86ed43495fd559a3166fa2f193345ac2827c03a16fc9c508386ae`.
It uses a GCC kernel and PCC VR4300 hard-float a.out userland and contains
independent GCC/PCC ordinary Linpack, kernel Linpack, and general compiler
benchmark binaries.

Physical N64 validation passes both general self-tests, both Linpack kernel
self-tests, every benchmark return code, all final debug/runner statuses, and
the terminal marker.  Stable ordinary Linpack is 3786.472 PCC versus 4412.654
GCC KFLOPS, or 85.81%, effectively unchanged from H4.  The general corpus is
substantially less favorable: PCC/GCC ranges from 15.79% for conversion and
26.13% for constant division through 39.94-59.70% for the integer, memory,
call, 64-bit, and single-precision cases; double precision reaches 67.22%.
This closes H5 and directs H6 toward shared conversion, constant arithmetic,
and integer code generation rather than another Linpack-specific transform.

H6 lowers hard-float 32-bit unsigned-to-FP conversion without a runtime call.
An unsigned constant mask no greater than `INT_MAX`, or a logical right shift,
uses the existing signed hardware conversion because its high bit is proven
clear.  The general full-range path converts the signed bit pattern to double
and conditionally adds exact `2^32`; float then rounds once with `cvt.s.d`.
Soft-float keeps its helper ABI.  This avoids adding a general SSA range
analysis while still handling unsigned values carried through local TEMPs.

The new `misc__ufprange001` passes in 299/299 hard-float runtime regressions on
Malta64, Malta, and MaltaEL.  All six hard/soft full smoke profiles pass with
zero general self-test, smoke-failure, and final RC markers.  The general PCC
benchmark loses five helper calls and shrinks by 896 section bytes.  The H6
physical candidate is
`/Users/sash/Work/N64/retrobsd-build/n64-h6-u32-fp-range-kgcc-upcc-hard-aout/obj/sys/mips/n64/pcc-debug.z64`,
size 8716288 bytes, SHA-256
`e59c80500ae0b8510b992f88130ffe5952638f408edf5de456ff074a7481e494`.
Physical N64 validation passes every self-test, benchmark return code, final
runner/debug status, and terminal marker.  `convert` improves from 0.403 to
1.655 Mwork/s, or 310.67%, and reaches 64.85% of GCC instead of 15.79%.
`float` and `double` improve 5.64% and 2.02%, reaching 48.77% and 68.58% of
GCC.  Stable ordinary Linpack improves 0.81% to 3817.158 KFLOPS and reaches
86.56% of GCC.  H6 therefore closes its physical gate; constant division is
the next largest assembly-confirmed general target.

H7 strength-reduces signed and unsigned 32-bit division and remainder by a
non-power-of-two constant in the MIPS table/emitter.  Unsigned division uses
multiply-high plus an optional correction and shift; signed division uses the
corresponding signed magic sequence with truncation-toward-zero correction.
Remainder emits `n - q * d`, using an existing shift/add product plan for
cheap `2^k +/- 1` magnitudes and a low-half multiply otherwise.  Powers of
two, zero, `+/-1`, and variable divisors keep their previous rules.

`misc__divconst001` compares 32 optimized constant operations with four
volatile hardware-divide references over boundary vectors and 16,384
deterministic pseudorandom values.  VR4300 and MIPS32R2 assembly gates require
no divide in the constant functions and preserve all four variable divides.
The general `bench_const_div` body replaces six divides with six multiplies;
the full PCC benchmark grows by only 176 section bytes.

All six Malta64/Malta/MaltaEL hard/soft full-smoke profiles pass with zero
general self-test, smoke-failure, and final RC markers.  Final hard-float
runtime regression passes 300/300 on Malta64, Malta, and MaltaEL, including
the expanded constant-division test.  The H7 physical image is
`/Users/sash/Work/N64/retrobsd-build/n64-h7-divconst-kgcc-upcc-hard-aout/obj/sys/mips/n64/pcc-debug.z64`,
size 6619136 bytes, SHA-256
`2641efd945fd1510a09ae97ef18b033967e377a4437506b94f4449bf1864f4a2`.
It uses a GCC kernel and PCC VR4300 hard-float a.out userland, passes
`fsutil --check`, and contains independent GCC/PCC runtimes for all six
benchmark binaries.  The physical result below is the final H7 commit gate.

The new integer multiplies remain covered by the final emitted-stream
workaround when default VR4300 `-mfix4300` is active.  Thus a dangerous
floating-point multiply cannot become adjacent to an H7 integer multiply;
`-mno-fix4300` continues to disable the workaround explicitly.

Physical N64 validation passes every general and Linpack self-test, every
benchmark return code, both final zero-valued debug statuses, and the terminal
marker.  `const_div` improves from 0.354 to 0.926 Mwork/s, or 161.58%, and
reaches 67.84% of GCC instead of 26.13%.  The other ten general PCC kernels
remain within 0.64% of H6.  PCC's two-row ordinary Linpack mean is 3788.831
KFLOPS versus 3785.421 for H6, a +0.09% change, and the isolated kernels show
no regression.  This closes the H7 physical and commit gates.

H8 allows eligible local signed and unsigned 64-bit integers to use the
existing MIPS `TEMP` register pairs.  It also corrects the `GCLASS` upper
boundary so the final `$s6/$s7` pair is `CLASSB`, not the FPU class.  The
change adds no pass or ABI mechanism and does not make saved-register pairs
permanent; existing caller-saved pairs are sufficient for the targeted leaf
loops.

The VR4300 and MIPS32R2 host gate compiles `misc__llpack001` and requires its
`mix_words` function to have no local frame loads or stores.  The general
`bench_u64` body drops from 96 to 72 instructions and from 34 memory
operations to the two required input loads.  Complete PCC benchmark assembly
shrinks by 24 lines and its a.out image by 160 section bytes, while GCC
controls remain byte-identical.

The first H8 physical candidate confirmed the intended speedup but failed the
correctness gate.  Its PCC `u64` result was 1.652 Mwork/s versus 1.153 for H7,
a 43.28% gain, and it reached 57.18% of the 2.889 Mwork/s GCC control.  All
benchmark self-tests and return codes were zero, but native `ccom` faulted
while compiling `jira/PCC-85.c`, so the final debug and runner status was one.
This was not an OOM: the hardware log reports 8192 KiB physical memory,
4096 KiB user memory, and 4096 KiB swap, and every later benchmark completed.

The fault exposed a general hidden-call contract bug.  MIPS `zzzcode` emits
`__udivdi3` and `__umoddi3` calls for 64-bit division and remainder, but those
operations are not CALL nodes and therefore do not receive the allocator's
normal call-clobber handling.  H8 made an old defect reproducible by keeping a
live pointer in `$t1` across the helper.  `NDIVB` now declares the existing
`MIPS_CALLER_SAVED_NEVER` set, matching the actual helper ABI.  The new
`misc__llcall001` regression keeps pointer and quotient state live across
consecutive helpers; it is also part of the physical N64 compile-and-run gate.
The necessary saves add 80 section bytes to the general benchmark, leaving
the corrected 45664-byte executable 80 bytes smaller than H7.

All six Malta64/Malta/MaltaEL hard/soft full-smoke profiles pass with zero
self-test, smoke-failure, and final RC markers.  Hard-float runtime regression
passes 301/301 on all three boards, including `jira__PCC-85`,
`misc__llpack001`, and `misc__llcall001`.  Native Malta64 regression also
passes all 301 runnable tests after 311 compile passes and 30 expected compile
failures.  The corrected physical candidate is
`/Users/sash/Work/N64/retrobsd-build/n64-h8-callclobber-kgcc-upcc-hard-aout/obj/sys/mips/n64/pcc-debug.z64`,
size 6619136 bytes, SHA-256
`6dbbffb1b32a0ca1b8fbe49b9d237082a6b351aba8115f9bc8963da7fc2f1b92`.
It uses a GCC kernel and PCC VR4300 hard-float a.out userland, passes
`fsutil --check`, and contains all six independently linked benchmark
binaries with zero undefined symbols.  Its debug rootfs includes both focused
native tests.

Physical N64 validation of the corrected image passes the complete native
runner, both general-corpus self-tests, both Linpack kernel self-tests, and
every benchmark return code.  `N64_PCC_DEBUG_END` and
`N64_PCC_DEBUG_RUNNER_RC` are zero and terminal `N64_PCC_DEBUG_RC_END` is
present.  PCC `u64` is 1.653 Mwork/s versus 1.153 for H7, a 43.37% gain, and
reaches 57.64% of the 2.868 Mwork/s GCC control instead of H7's 39.92%.  The
GCC control differs from H7 by only -0.69%.  PCC ordinary Linpack rows are
3756.395 and 3789.017 KFLOPS; their 3772.706 mean is 85.12% of the current
GCC mean and only 0.43% below H7.  The correctness fix therefore preserves
the provisional H8 performance while closing the physical and commit gates.

H9A extends the existing SSA address-induction hook to integer memory loops on
VR4300 and MIPS32R2.  Global symbol addresses may be initialized in a loop
preheader, and unsigned induction accepts PCC's integer-typed pointer-scaling
shift.  Single-use register bases remain disabled for integer data because an
unrestricted prototype increased pressure and stack traffic in Linpack
`dgefa`; a single-use global base is allowed, while the earlier hard-float
VR4300 FP exception is unchanged.  Final H8 and H9A Linpack assembly is
byte-identical on both target CPUs, but the general `bench_memory` hot loop
uses loop-carried pointers instead of rebuilding three global addresses per
iteration.  Hard-float runtime regression passes 301/301 on all three Malta
variants, native Malta64 passes all 301 runnable cases, and all six hard/soft
full-smoke profiles pass.  The retained clean GCC-kernel/PCC-hard-float a.out
candidate is
`/Users/sash/Work/N64/retrobsd-build/n64-h9a-address-induction-kgcc-upcc-hard-aout/obj/sys/mips/n64/pcc-debug.z64`,
SHA-256
`a0c5b5c7cf15f1c471bd8a39dad9b25864a39e3b9ac52c60c6736f203a99b5ad`.
Its boot-time C runner compiles and executes `misc__ssastrength001` as well as
the previously staged `misc__llcall001`.  Physical N64 validation passes with
both self-tests, all benchmark return codes, and both numeric debug markers
at zero.  PCC `memory` improves from 4.262 to 5.352 Mwork/s (+25.57%), all ten
other general PCC kernels improve, and isolated Linpack kernels remain within
-1.02% to +0.93% of H8.  Ordinary PCC Linpack averages 3792.478 KFLOPS,
0.52% above H8 and 85.25% of the current GCC mean.  This closes the H9A
physical and commit gates.

H10 enables PCC's existing automatic-variable `TEMP` path for non-volatile
MIPS pointers.  Address-taken pointers still receive stack storage, so this is
a target eligibility correction rather than a new optimizer or register
allocator mechanism.  `misc__pointertemp001` covers copy, fill, early-return
compare, address-taken fallback, an indirect function pointer, and a global
table pointer.  Host assembly checks require no frame references in the three
pointer walks and retain the address-taken stack slot.

The first self-hosted build exposed a hidden dependency in the driver assembly
scheduler: GAS expands a bare-symbol load or store through `$at`, but the
scheduler did not model that implicit use and could move a live `$at` consumer
past it.  The driver now treats bare-symbol integer and FPU memory operations
as `$at` users.  A host ordering gate and the full native compiler regression
cover the fix.

Cross runtime regression passes 302/302 cases.  Native Malta64 reports 312
compile passes, 30 expected compile failures, and 302/302 runtime passes.  All
six Malta64/Malta/MaltaEL hard/soft full-smoke profiles pass with zero general
self-test, smoke-failure, and final RC markers.  The retained physical
candidate is
`/Users/sash/Work/N64/retrobsd-build/n64-h10-pointer-temp-kgcc-upcc-hard-aout/obj/sys/mips/n64/pcc-debug.z64`,
size 6619136 bytes, SHA-256
`7731ff096837bbb25f434ecbfa1d2808bfb5ad7a273b9a3b5ed7898fa23b582b`.
It uses a GCC kernel and PCC VR4300 hard-float a.out userland, passes all five
rootfs checks, and has zero undefined symbols in all benchmark binaries.  The
GCC controls are byte-identical to H9A, while PCC general, Linpack, and Linpack
kernel section sizes fall by 1680, 1632, and 1632 bytes.  Physical N64
validation passes every native, self-test, benchmark return-code, and final
status gate.  PCC `libc_memory` improves from 22.252 to 38.912 Mwork/s, a
74.87% gain, and reaches 74.65% of GCC instead of 42.99%.  PCC Linpack
averages 3774.106 KFLOPS, 0.48% below H9A and 84.79% of GCC; isolated kernels
range from -0.78% to +5.08%.  H9A and H10 timed general benchmark assembly is
byte-identical outside the changed libc callees, so the raw `convert` timing
outlier is not a code-generation regression.  This closes the H10 physical
and commit gates.

## Reverse Address Induction

The SSA address-strength-reduction pass recognizes scaled array indexes of the
forms `i`, `i+C`, and `C-i`.  A reverse candidate carries direction `-1`, so
its pointer phi is initialized from `base - i*scale` and its latch step has the
opposite sign.  The constant remains a normal immediate displacement.  Keep
the form narrow: the base must be loop invariant, the scale must be a power of
two represented by `LS`, and the index must be the canonical induction TEMP
with an optional constant.

Regression coverage includes constant and dynamic initial indexes, paired
register-base accesses, and a single global FP access.  Generic MIPS3 retains
the old code unless its target profitability hook enables the transform;
VR4300 and MIPS32R2 use the established hooks.  The transform adds no ABI,
scheduler, or register-class state.  It also does not affect VR4300 multiply
errata handling: `-mfix4300` remains the default final-stream repair and
`-mno-fix4300` remains the explicit opt-out.

On physical VR4300, this transform raises the general PCC `float` kernel from
2.553 to 2.956 Mwork/s (+15.79%) and `double` from 2.113 to 2.382 (+12.73%).
Ordinary PCC Linpack remains neutral at 3773.312 KFLOPS, 0.02% below the prior
milestone and 85.59% of the same-run GCC mean.  The complete native regression
and all N64 debug markers pass.

## Masked Constant Induction

The target-independent SSA induction reducer can carry an unsigned constant
product through a loop when it appears under a complete low-bit mask:
`(i*C) & (2^N-1)`.  The preheader computes `start*C` once when the initial
induction value is dynamic, and the latch advances a second phi by the signed
immediate `C*delta`.  This removes a multiply or shift/add sequence from each
iteration without changing unsigned wraparound semantics.

The form is intentionally narrow.  The induction value and multiplication
must have the same unsigned integer type, the mask must be a positive
`2^N-1` constant, `C` is limited to a positive signed-16-bit constant greater
than one, and `C*delta` must fit a signed immediate.  Partial masks and
unmasked constant products do not enable the transform.  A target hook limits
the current profitability decision to VR4300 and MIPS32R2; generic MIPS3
retains its old lowering.

`misc__ssastrength001` covers zero and dynamic starting values, a partial-mask
negative control, and an unmasked negative control.  Host assembly gates
check VR4300, MIPS32R2, and generic MIPS3 behavior.  The transform does not
alter final instruction scheduling or multiply errata handling:
`-mfix4300` remains the default final-stream repair and `-mno-fix4300` remains
the explicit opt-out.

H12 host gates pass for VR4300 and MIPS32R2.  Cross compilation passes 339
tests with three expected failures, followed by 302/302 runtime passes.
Native Malta64 reports 312 compile passes, 30 expected compile failures, and
302/302 runtime passes.  All six Malta64/Malta/MaltaEL hard/soft full-smoke
profiles report compiler-benchmark self-test zero, `CCOM_STRESS_DONE:100`,
`PCC_SMOKE_ALL_FAILURES 0`, and final RC zero.  Sequential one-second Linpack
repeats also pass on every profile; the H11 and H12 PCC Linpack source
assembly is byte-identical.

The retained physical candidate is
`/Users/sash/Work/N64/retrobsd-build/n64-h12-masked-induction-kgcc-upcc-hard-aout/obj/sys/mips/n64/pcc-debug.z64`,
size 6619136 bytes, SHA-256
`28bd9078d306add2494c7af852879013c73204999deef5b7e4fe78debfda873a`.
It uses a GCC kernel and PCC VR4300 hard-float a.out userland.  The kernel,
debug runner, and all three GCC benchmark controls are byte-identical to H11;
all six benchmark executables and the runner have zero undefined symbols.
The 6144 KiB debug rootfs passes all five `fsutil` phases with 358 files, 3872
used blocks, and 2247 free blocks.  Physical N64 validation remains mandatory
before commit.

Physical N64 validation passes the expanded native regression, both general
self-tests, both Linpack-kernel self-tests, every benchmark return code, and
the complete debug runner.  `N64_PCC_DEBUG_END` and
`N64_PCC_DEBUG_RUNNER_RC` are zero and the terminal marker is present.  The
targeted PCC `memory` kernel improves from H11's 5.349 to 5.720 Mwork/s, a
6.94% gain, and reaches 55.64% of the same-run GCC result instead of 52.38%.
All unrelated general kernels remain within 0.96% of H11.  PCC Linpack
averages 3758.096 KFLOPS, 0.40% below H11 and 84.89% of the current GCC mean;
the isolated PCC kernels are unchanged within timing noise.  This closes the
H12 physical and commit gates.

## Canonical Counted-Loop Latches

After SSA phi copies are inserted, a target may lower a narrow canonical
counted loop so its latch branches directly to the body.  Keep the original
header as the zero-trip entry guard.  The current implementation requires a
single preheader and body, adjacent `header/body/exit` layout, an integer
non-pointer induction phi updated by exactly one, and a matching signed or
unsigned exit comparison against a numeric or pre-loop TEMP limit.  This
restriction makes `updated_i != limit` equivalent to the original top test.

The generic hook defaults off.  MIPS enables it for VR4300 and MIPS32R2;
generic MIPS3, step-two loops, and multiblock loops retain their old control
flow.  `misc__ssacounted001` provides runtime coverage and host assembly
checks for both transformed and rejected forms.  It is also staged in the
extended N64 physical debug runner.

H13 passes host smoke, 303/303 cross runtime tests, 303/303 native Malta64
hard-float tests, and all six Malta64/Malta/MaltaEL hard/soft full-smoke
profiles.  The additional native soft-float run exposes the existing
loop-free `c99__arith003` NaN comparison failure; H12 and H13 produce
byte-identical assembly and objects for that test, and the new counted-loop
test passes in the same run.

The retained GCC-kernel/PCC-hard-float-a.out candidate is
`/Users/sash/Work/N64/retrobsd-build/n64-h13-counted-loop-kgcc-upcc-hard-aout/obj/sys/mips/n64/pcc-debug.z64`,
size 6619136 bytes, SHA-256
`9bdcae43c38d6c2d06c3760b01740f67873ee8be246f97ace5cbd9d5fe7e1a78`.
Its kernel and all GCC benchmark controls are byte-identical to H12, all
benchmark binaries and the runner have zero undefined symbols, and the rootfs
passes all five `fsutil` phases.  Physical validation is required before
commit.  This lowering does not alter multiply scheduling: `-mfix4300`
remains the default erratum repair and `-mno-fix4300` remains available.

Physical N64 validation passes the complete extended runner, including the
staged native `misc__ssacounted001`, both general and Linpack-kernel
self-tests, every benchmark return code, zero-valued `N64_PCC_DEBUG_END` and
`N64_PCC_DEBUG_RUNNER_RC`, and the terminal `N64_PCC_DEBUG_RC_END` marker.
Ordinary PCC Linpack averages 3786.128 KFLOPS, 0.75% above H12 and 85.53% of
the current 4426.766 KFLOPS GCC mean.  The targeted PCC `ddot_r` kernel rises
from 4.280 to 4.967 Melem/s, a 16.05% gain, and reaches 82.26% of GCC.  In the
general corpus, `int_mix`, `memory`, `calls`, `u64`, `float`, and `double`
improve by 4.06% to 13.90%; `branch`, `switch`, `libc_memory`, and `convert`
remain within 0.81% of H12.  `dscal_r` is 3.32% below H12 and the other
isolated kernels remain within 0.74%.  This closes the H13 physical and commit
gates while the 90% overall target remains open.

## Conservative Control-Delay Lookback

H14 extends the existing MIPS late peephole for VR4300 and MIPS32R2.  A safe
instruction may move from immediately after a label into a following control
delay slot.  The peephole may also look back across one simple GPR move/ALU
instruction when that middle instruction cannot fill the slot and all GPR
RAW/WAR/WAW dependencies are disjoint.  Candidates remain limited to the
existing non-trapping GPR move/ALU set plus `mov.s`/`mov.d`; the new lookback
does not move memory operations.  Generic MIPS3 output is unchanged.

The focused general corpus removes 22 branch/jump delay-slot `nop`s and the
Linpack-kernel translation unit removes 14, with useful instruction and
memory/control counts unchanged.  Host positive/negative assembly gates,
303/303 VR4300 runtime tests, and all six Malta64/Malta/MaltaEL hard/soft full
smoke profiles pass.  The retained GCC-kernel/PCC-hard-float-a.out N64 image is
`/Users/sash/Work/N64/retrobsd-build/n64-h14-control-delay-kgcc-upcc-hard-aout/obj/sys/mips/n64/pcc-debug.z64`,
size 6619136 bytes, SHA-256
`72d3497e0e0cd3fc97b70b21b9782fcaeeef3d9dd0bcca7fdcce0ddd0ccdbdae`.
Its rootfs passes `fsutil --check`; physical N64 validation is required before
commit.  `-mfix4300` remains the VR4300 default, multiply candidates remain
blocked by the scheduler, and the erratum repair still runs afterward.

Physical N64 validation passes the complete runner, both benchmark self-tests,
every GCC/PCC return code, both zero-valued numeric debug markers, and the
terminal marker.  Ten of eleven general PCC kernels improve over H13 by 0.88%
to 7.63%; `libc_memory` is the measured exception at -2.47%.  PCC Linpack
averages 3807.266 KFLOPS, 0.56% above H13 and 85.53% of the current GCC mean.
The isolated kernels remain stable apart from the earlier low `dscal_r`
measurement returning to its H12 level.  This closes the H14 physical and
commit gates while preserving the 90% overall target.

## Dense Switch Tables

H15 replaces the generic linear lowering only for profitable 32-bit dense
switches on VR4300 and MIPS32R2.  A table requires at least five cases, a
range of at most 256 entries, and at least 50% density.  Smaller, sparse,
wide, generic MIPS3, and C++ switches retain the old lowering.  The switch
value is normalized once and checked as unsigned, so signed negative ranges
and unsigned ranges near `UINT_MAX` use the same 32-bit sequence.  Table
entries are readonly `.word` case addresses loaded with `lw` and dispatched
with `jr`; no MIPS64 instruction is introduced.

The computed targets are registered through the frontend's existing
address-taken-label list.  This gives pass2 conservative CFG edges to every
case and prevents dead-block elimination from removing a case reached through
the table.  It adds no new pass2 IR or scheduler state.  The focused
`bench_switch` function falls from 91 to 83 instructions and from 13 to eight
`nop`s.  More importantly, the hot dispatch no longer executes a chain of up
to nine comparisons; it uses one range check, one table load, and one
indirect jump.  GCC remains smaller at 67 instructions, leaving block layout
and case-to-latch control flow as later general optimization work.

`misc__switchtable001` covers holes/defaults, signed negative cases, unsigned
wraparound, a switch without a default, and two tables in one function.  Host
assembly gates require tables for VR4300 and MIPS32R2 and reject them for
generic MIPS3.  Cross regression reports 341 compile passes plus the same
three expected failures and 304/304 runtime passes.  All six PCC-kernel and
PCC-rootfs Malta64/Malta/MaltaEL hard/soft full-smoke profiles pass with
`CCOM_STRESS_DONE:100`, compiler self-test zero, and
`PCC_SMOKE_ALL_FAILURES 0`.

The `-j4` matrix exposed a pre-existing `awk` parser-generation race: a
target without a recipe allowed GNU make's built-in `.y.c` rule to run a
second `byacc` while `awk.h` was copied.  The side-effect targets now have an
explicit portable recipe that verifies the files and blocks the implicit
rule.  Clean parallel N64 and Malta builds confirm one parser generation.

Before H15, saved H13 and H14 `libc_memory` assembly and the linked
`memcmp`/`memcpy`/`memset` objects were compared and found byte-identical.
H14's reported -2.47% `libc_memory` result is therefore physical timing
variation, not a control-delay code-generation regression.

The first H15 physical candidate was
`/Users/sash/Work/N64/retrobsd-build/n64-h15-switch-table-kgcc-upcc-hard-aout/obj/sys/mips/n64/pcc-debug.z64`,
size 6619136 bytes, SHA-256
`22b608a0ee40a066b95e88e4f79808d1dc61d0c9ce1b3c3c6248ba75f0de4496`.
It has build stamp `.build-mode.gcc.1.0.0.1`, a GCC kernel, and PCC VR4300
hard-float a.out userland.  The kernel SHA-256 remains
`a6a22e43ca2cbc5fc792eeaa4a8596070034f63c4749da6e9164af8496bb717d`.
The rootfs passes all five `fsutil` phases with 360 files, 3853 used blocks,
and 2266 free blocks.  The runner and all six GCC/PCC benchmark binaries have
zero undefined symbols.  The rootfs stages `misc__switchtable001` and its
fallback all-tests script executes it.  A later pre-release audit found that
the primary C runner still omitted the new test, however.

The first H15 image passes its physical N64 correctness gate.  The supplied
excerpt starts at Linpack, but all visible self-tests and command return codes
are zero, as are `N64_PCC_DEBUG_END` and `N64_PCC_DEBUG_RUNNER_RC`; the terminal
marker is present.  GCC Linpack averages 4428.735 KFLOPS and PCC averages
3822.356 KFLOPS, or 86.31% of GCC.  This is within physical timing noise of
H14.  The general `switch` kernel regresses from 3.113 to 3.020 Miter/s,
however, a 2.99% loss, so this version is rejected rather than committed.
Because of the C-runner omission, this log does not close the new switch
test's physical correctness gate.

The regression exposed an interaction with pass2 SSA rather than a cost in
the table dispatch itself.  PCC formerly abandoned SSA for an entire function
when critical-edge splitting encountered a computed goto.  Adding the table
therefore removed the existing `input_a` and `input_b` address inductions from
the benchmark loop and recomputed both addresses on every iteration.

H15b retains SSA in this case.  Ordinary critical edges are still split.  An
unretargetable computed-goto edge instead emits destination phi copies before
the dispatch; SSA result temporaries are distinct, so copies for untaken
targets are dead.  `misc__switchtable001` now also has a dense switch in a loop
with an accumulator phi and a symbol-address induction.  Its host assembly
gate requires the four-byte pointer increment, preventing this regression
from returning.

H15b restores both benchmark pointer inductions and reduces the focused
function to 82 instructions, 74 non-NOP instructions, eight NOPs, three
branches, and 12 jumps.  Host hard-float a.out smoke passes.  Malta64 reports
341/344 cross-compilation passes with the same three expected failures and
304/304 runtime passes.  All six full Malta64/Malta/MaltaEL hard/soft profiles
pass with PCC kernels and root filesystems.

`misc__switchtable001` is now also an explicit primary C-runner entry.  The
linked runner is audited for the literal test name so staging the source
without executing it cannot satisfy the release gate.

The corrected physical candidate is
`/Users/sash/Work/N64/retrobsd-build/n64-h15b-switch-ssa-runner-kgcc-upcc-hard-aout/obj/sys/mips/n64/pcc-debug.z64`,
size 6619136 bytes, SHA-256
`bd7cc9e47e720b896748b3231a7617173a047190ee2e87162f7dc08aaad60db7`.
Its GCC kernel is byte-identical to H14 and the first H15.  The rootfs passes
all five `fsutil` phases with 360 files, 3855 used blocks, and 2264 free
blocks.  The runner SHA-256 is
`922b9b9b305a32664cd4e32849d5cb1e0bec4bfe9d26b993ce5e9a2e69a40c3c`;
it contains the switch-test name.  The runner and all six benchmark binaries
have zero undefined symbols.

Physical N64 H15b validation passes.  The supplied excerpt begins at Linpack,
but the audited C runner executes its extended test group, including
`misc__switchtable001`, before the benchmarks and reports aggregate
`N64_PCC_DEBUG_END 0`.  Both general and Linpack-kernel self-tests, every
visible GCC/PCC benchmark return code, `N64_PCC_DEBUG_RUNNER_RC 0`, and the
terminal marker also pass.  The target `switch` kernel reaches 3.190 Miter/s,
2.47% above H14 and 5.63% above the rejected first H15, or 64.46% of GCC.
Other general kernels remain within 0.89% of H14.  PCC Linpack averages
3805.275 KFLOPS, 0.05% below H14 and 86.37% of the current GCC mean.  This
closes the corrected H15b physical and commit gates.

H15 does not change floating-point multiply scheduling or final-stream
erratum repair.  `-mfix4300` remains enabled by default for VR4300 and
`-mno-fix4300` remains the explicit opt-out.

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
