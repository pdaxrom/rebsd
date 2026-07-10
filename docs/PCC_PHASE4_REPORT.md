# PCC Phase 4 Spill-Cost Report

## Scope

Phase 4 replaces the machine-independent graph allocator's coarse first-fit
spill choice with measured cost selection.  It does not change the default GCC
build policy, the ReBSD assembler/linker/archive tools, the static o32 ABI, the
VR4300 erratum policy, or target instruction selection.

Changed source files:

- `src/dev/pcc/pcc/mip/regs.c`
- `src/dev/pcc/pcc-tests/regress/misc/Makefile`
- `src/dev/pcc/pcc-tests/regress/misc/spillcost001.c`
- `src/dev/pcc/pcc-tests/regress/misc/spillfp001.c`
- `src/dev/pcc/pcc-tests/regress/misc/spillabi001.c`
- `src/dev/pcc/pcc-tests/regress/misc/spillremat001.c`

## Allocator Design

The allocator records uses, definitions, loop-weighted reference cost, live
range endpoints, call crossing, register class, and a conservative
rematerialization expression for each long-lived TEMP.  Loop weights are 1,
4, 16, and 64 for depths zero through three or more.  Spill selection minimizes
the named weighted cost divided by the current interference degree and uses the
TEMP number as a deterministic tie breaker.

The existing interference model makes every value live across a call conflict
with caller-saved colors.  Callee-saved colors therefore remain available when
profitable, while the new cost adds a call-crossing penalty before choosing a
spill.  FPR references receive a higher rewrite cost to avoid repeated numeric
loop traffic.  Metadata lives in a temporary side array rather than enlarging
every allocator node.

Rematerialization accepts only integer constants, symbol constants, their
simple conversions, and `$fp` plus/minus an integer constant.  It rejects
memory loads, volatile operations, calls, trapping expressions, and side
effects.  Rewriting requires exactly one top-level definition and copies the
saved expression before deleting that definition.

## Deterministic Metrics

The MIPS32r2 hard-float `spillcost001` result is:

| Metric | Before | After |
| --- | ---: | ---: |
| selected spills | 11 | 11 |
| reloads | 54 | 33 |
| spill stores | 22 | 22 |
| spill bytes | 44 | 44 |
| instructions | 468 | 448 |
| loads | 84 | 63 |
| stores | 45 | 45 |
| nops | 9 | 9 |
| assembly bytes | 12565 | 11985 |

`spillfp001` remained at 360 instructions, 104 loads, and 40 stores.
`spillabi001` remained at 167 instructions, 52 loads, and 36 stores.  This
provides a measured GPR improvement without a material FPR or ABI regression.
The runtime helper `extendsfdf2` reports two actually rematerialized values.

## Correctness Gates

Exact commands used:

```sh
make -C sys/mips pcc-regress-compile
make -C sys/mips pcc-regress-runtime
make -C sys/mips/malta native-pcc-regress-runtime
make -C sys/mips linpack-smoke-runtime
make -C sys/mips BOARD=malta64 MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc pcc-smoke-all-runtime
make -C sys/mips BOARD=malta64 MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc MIPS_ROOTFS_FLOAT=soft pcc-smoke-all-runtime
make -C sys/mips BOARD=malta MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc pcc-smoke-all-runtime
make -C sys/mips BOARD=malta MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc MIPS_ROOTFS_FLOAT=soft pcc-smoke-all-runtime
make -C sys/mips BOARD=maltael MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc pcc-smoke-all-runtime
make -C sys/mips BOARD=maltael MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc MIPS_ROOTFS_FLOAT=soft pcc-smoke-all-runtime
make -C sys/mips/n64 pcc-debug-image
```

Cross regression compiled 322 of 325 tests; the remaining three are the
existing x86 inline-assembly, shared-library/PIC, and TLS expected failures.
All 285 runtime candidates passed.  Native PCC compiled 295 tests, observed 30
existing expected failures, and passed all 285 runtime tests.  Unexpected
compile and runtime failures were both zero.

| Board | CPU | Endian | Float | PCC Linpack KFLOPS | Result |
| --- | --- | --- | --- | --- | --- |
| Malta64 | VR4300 | big | hard | 13614 / 13666 | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | 975 / 975 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32r2 | big | hard | 13565 / 13580 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32r2 | big | soft | 973 / 969 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32r2 | little | hard | 13631 / 13800 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32r2 | little | soft | 929 / 949 | `PCC_SMOKE_ALL_RC:0` |

Every full smoke also reported `PCC_SMOKE_ALL_FAILURES 0`.  PCC kernel commands
retained `-msoft-float -fomit-frame-pointer` in all profiles.  The separate
Malta Linpack run produced GCC 31662/31777 and PCC 13807/13822 KFLOPS.

## N64 Status

The build-only hard-float a.out hardware image is:

```text
sys/mips/n64/builds/20260710-phase4-spill-cost/pcc-debug.z64
allocator source commit: c20bf9e6
size: 6619136 bytes
sha256: bbd782c7f581c1e8af05df886e92c36f497792c5b1105fc8c9efca9c076a6457
cross pcc sha256: 749c0557821f073dbfd13ac94127271cca7464306dffbd85d7de302967723000
cross ccom sha256: 50e90d5612bca07c711a8e571b66ba9873bc49bdcde9f60e9e56f2bf4e6cd54c
native ccom sha256: 20234e3861e3d2df0328e86606af2b77f3869d73ada9a510a3788c47d9aff37d
```

The previously prepared `20260710-hard-float` images were not rebuilt or
modified.  No real N64 boot log is available yet.  QEMU does not reproduce the
VR4300 FP multiply erratum, so this report does not claim hardware readiness.

## Remaining Work

Spill selection is no longer first-fit, but allocator statistics still include
retry work and the simple loop detector uses CFG back-edge intervals.  The
aggregate Malta result remains about 44 percent of GCC on QEMU Linpack, so the
larger gap is not primarily a spill-choice problem.  The next phase should add
CFG and dominator consistency checks, then evaluate compact scalar cleanup and
late MIPS scheduling as separate measured changes.  Real N64 smoke is required
before any VR4300 performance claim.
