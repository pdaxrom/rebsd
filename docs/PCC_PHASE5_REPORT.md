# PCC Phase 5 Report

## Phase 5A: CFG And SSA Verification

Phase 5A prepares the existing pass2 representation for incremental scalar
optimization.  It does not newly enable SSA, add a new IR, or change target
code generation.  The imported PCC driver already passes `-xssa` for normal
optimized compilation.  The implementation is machine-independent and does
not enlarge the MIPS backend.

The new `mip/cfgverify.c` checks:

- complete, contiguous, and non-overlapping basic-block coverage;
- block numbering and the label-to-block map;
- exact direct, conditional, computed-goto, and fallthrough successors;
- reciprocal predecessor/successor edge multiplicity;
- DFS parents and independently recomputed immediate dominators;
- dominator-tree child bitsets;
- phi predecessor count, duplicate destination TEMP, and Aphi membership;
- phi source/destination TEMP ranges before and after SSA rename.

`optim2.c` now uses one build-and-verify helper at the initial and post-SSA CFG
construction points.  Normal `xtemps` compilation runs the allocation-free CFG
checks.  Independent dominator bitsets and phi checks are allocated when the
existing internal `-xssa` path is active, including the normal `-O` path.

The common verifier object is included in C, C++, F77, split C pass, host-cross,
and native PCC builds.  The permanent host smoke also compiles a nested-loop
and call probe with explicit `-Wc,-xssa`.

## Assembly Identity

A MIPS32r2 hard-float compiler built from pre-change commit `ce61008c` was
compared against verifier commit `3f6f628e` with identical flags.  These files
were byte-identical:

| Source | Assembly SHA-256 |
| --- | --- |
| `optim003.c` | `655c3497f6171e827f9de78eb3e1cc46d2520eef8287c0aea1801af7d3f40c43` |
| `spillcost001.c` | `549bcb6b68553e28f38ad2bc3c347dc7200e7966fc5dfffddfe26be0f3bcc123` |
| `spillfp001.c` | `0b5c4769719c037d8357946fb1d204dca01e22cafa6381e64d123c20f2032ef1` |
| `spillabi001.c` | `a6ddc4f3bf4170193da291a1f5c42c2b56e58a854dfc403c70221a0dbee2a30f` |
| `extendsfdf2.c` | `1595bf4b68aba40512f2e731e25b857af5b06d254a1eff4cf70871fc0be3759a` |
| `linpack.c` | `61f071066b8b537dd764d313030a655ba92f82f1943be750dcce08f7727e9098` |

VR4300 hard-float Linpack was also identical: `79974` assembly bytes and
SHA-256 `ab7f5876e2d2fb599cc1674d8a1298c44972afc2d61b37b6fb3f21935a869b86`.

The stripped cross-host `ccom` size changed from `526104` to `542712` bytes,
an increase of `16608` bytes or about 3.2 percent.

## Validation

Commands used:

```sh
make -C sys/mips/malta smoke-host-portablecc
make -C sys/mips pcc-regress-compile
make -C sys/mips pcc-regress-runtime
make -C sys/mips/malta native-pcc-regress-runtime
make -C sys/mips BOARD=malta64 MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc pcc-smoke-all-runtime
make -C sys/mips BOARD=malta64 MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc MIPS_ROOTFS_FLOAT=soft pcc-smoke-all-runtime
make -C sys/mips BOARD=malta MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc pcc-smoke-all-runtime
make -C sys/mips BOARD=malta MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc MIPS_ROOTFS_FLOAT=soft pcc-smoke-all-runtime
make -C sys/mips BOARD=maltael MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc pcc-smoke-all-runtime
make -C sys/mips BOARD=maltael MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc MIPS_ROOTFS_FLOAT=soft pcc-smoke-all-runtime
make -C sys/mips/n64 pcc-debug-image
```

Cross regression compiled 322 of 325 tests with only the existing x86 inline
assembly, shared-library/PIC, and TLS expected failures.  All 285 runtime tests
passed.  Native PCC compiled 295 tests, observed the 30 existing expected
failures, and passed all 285 runtime tests.  Unexpected compile and runtime
failures were both zero, with `NATIVE_PCC_REGRESS_RC:0`.

| Board | CPU | Endian | Float | PCC Linpack KFLOPS | Result |
| --- | --- | --- | --- | --- | --- |
| Malta64 | VR4300 | big | hard | 11571 / 11473 | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | 924 / 908 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32r2 | big | hard | 13413 / 13568 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32r2 | big | soft | 883 / 895 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32r2 | little | hard | 13378 / 13679 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32r2 | little | soft | 911 / 908 | `PCC_SMOKE_ALL_RC:0` |

Every profile also reported `PCC_SMOKE_ALL_FAILURES 0`.  Three isolated reruns
of the identical Malta64 hard image ranged from 12880 to 13230 KFLOPS.  The
lower result after the full compiler stress workload is runtime noise rather
than a code-generation difference.

## N64 Artifact

The hard-float a.out hardware image is:

```text
sys/mips/n64/builds/20260710-phase5a-cfg-verifier/pcc-debug.z64
verifier source commit: 3f6f628e
size: 6619136 bytes
sha256: 3ce8f732949fb0d885b6e1bc55c95663aafeff45cfd55f338dd240a4b17ce806
cross pcc sha256: 9c490a935ead5fa5819733227a6b62ce31f1e322e5155cce1b7a1fa1dcbc7065
cross ccom sha256: 8363b14fbcece51d54fb02373a29e34b5b0134ecbe319fc8b769fd705bc2ca8b
native ccom sha256: d6d5e37a8bb880669931ea0f22e5bd582ceffcab0b10ede136a5ee2d24e61fbc
```

The earlier N64 build directories remain byte-for-byte unchanged.  Real N64
hardware completed the image with:

```text
N64_PCC_DEBUG_END 0
N64_PCC_DEBUG_RUNNER_RC 0
N64_PCC_DEBUG_RC_END
```

This validates the Phase 5A native compiler and default `-mfix4300` hard-float
a.out path.  It does not cover a soft-float image or a distinct
`-mno-fix4300` run, and QEMU still cannot reproduce the physical multiply
erratum.

## Phase 5B: Critical Edges And Parallel Copies

Implementation commit `e1f21c62` replaces the old sequential `removephi()`
code with machine-independent critical-edge splitting and parallel-copy
lowering in `mip/ssalower.c`.

The splitter uses two layouts:

- a critical fallthrough edge gets a source-adjacent label and jump pad;
- a critical taken edge gets a destination-adjacent pad, with an explicit jump
  added only when the preceding physical block could otherwise enter the pad.

The CFG is rebuilt after splitting and a dedicated verifier rejects every
remaining edge whose source has multiple successors and destination has
multiple predecessors.  Each phi predecessor is therefore a single-successor
block and provides an unambiguous insertion point.

All copies for one phi edge are lowered together.  A copy is emitted only when
its destination is not a source of another pending copy.  If no such copy
exists, one typed TEMP saves an old destination value and breaks the cycle.
This fixes the correctness hole in sequential copy emission without adding
target-specific templates.

Computed-goto destinations cannot currently be retargeted in pass2 IR.  The
splitter detects a critical computed-goto edge before mutating the function,
then disables SSA for that function only.  Register allocation and matching
consult the per-function `ssa_active` state, so following functions still use
SSA.  Optimized `gcccompat/extension004` covers this fallback and debug output
confirms `SSA fallback for critical computed-goto edge`.

### Kernel Gate Finding

The first implementation put both pad kinds after the source block.  Although
small regression tests passed, the full PCC-built Malta64 kernel trapped in
`hardclock()` with a bad store through address `0x14`.  The generated sequence
kept `p1` in `$a0`, loaded `p1->p_stats->p_ru.ru_stime` through the same
register, and then used the overwritten `$a0` as the store base.

A focused `hardclock()` reproducer isolated the layout dependency.  Moving
taken-edge pads next to the destination while preserving the source
fallthrough restored byte-identical assembly for the reproducer.  The full
six-profile kernel matrix below then passed.  This is why kernel compilation
and boot remain mandatory gates for pass2 CFG changes.

### Regression Coverage

`misc/ssaphi001` exercises cyclic integer and double copy sets in an optimized
loop.  It is run with explicit `-Wc,-xssa` in addition to the driver's normal
optimized SSA selection.  `gcccompat/extension004` is compiled and run a
second time at `-O2` to cover per-function computed-goto fallback.

Cross regression compiled 324 of 327 tests.  The only failures were the
documented x86 inline assembly, unsupported shared-library/PIC ABI, and
unsupported TLS ABI cases.  All 287 runtime candidates passed.  Native PCC
compiled 297 cases, observed the 30 expected compile failures, and passed all
287 runtime cases.  Unexpected compile/runtime failures were zero and the
native gate ended with `NATIVE_PCC_REGRESS_RC:0`.

The final `ssalower.o` also compiles under the C++, F77, split-pass,
host-cross, and target-native build layouts.  Full C++ and F77 frontend links
remain blocked by their existing unrelated `zbits` type mismatch and
`TYIREG` declaration errors, respectively.

### Assembly And Compiler Size

Unoptimized `optim003.c` is byte-identical before and after Phase 5B:

```text
sha256 bff0d1f27f8ab0b61e07de14db8313979e788d03be521282055fb327f6fb08ef
```

Optimized output changes intentionally because `-O` already enables SSA and
now uses safe parallel-copy semantics.  Linpack assembly counters are:

| Target | Phase 5A instructions | Phase 5B instructions | Phase 5A loads/stores | Phase 5B loads/stores | Phase 5B nops |
| --- | ---: | ---: | ---: | ---: | ---: |
| VR4300 hard | 2477 | 2477 | 643 / 244 | 643 / 244 | 158 |
| MIPS32r2 hard | 2533 | 2551 | 765 / 286 | 772 / 293 | 113 |

The MIPS32r2 increase is the cost of preserving cyclic/live copy sources and
is the first cleanup target for Phase 5C copy propagation and DCE.  VR4300 code
counts are unchanged.  The stripped host `ccom` grows only 80 bytes, from
542712 to 542792, because the new lowering replaces the old implementation
rather than adding a second path.

### QEMU Matrix

All kernels and root filesystems were built with PCC.  Kernel compilation
continues to cover `-fomit-frame-pointer`; soft profiles cover
`-msoft-float`.

| Board | CPU | Endian | Float | PCC Linpack KFLOPS | Result |
| --- | --- | --- | --- | --- | --- |
| Malta64 | VR4300 | big | hard | 11520 / 10754 | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | 872 / 872 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32r2 | big | hard | 13149 / 13189 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32r2 | big | soft | 999 / 1000 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32r2 | little | hard | 13004 / 12964 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32r2 | little | soft | 999 / 1004 | `PCC_SMOKE_ALL_RC:0` |

Every profile also reported `PCC_SMOKE_ALL_FAILURES 0`.  Logs are
`/private/tmp/pcc-phase5b-{malta64,malta,maltael}-{hard,soft}.log`.

### N64 Artifact

The build-only hard-float a.out image is:

```text
sys/mips/n64/builds/20260710-phase5b-ssa-lowering/pcc-debug.z64
implementation commit: e1f21c62
size: 6619136 bytes
sha256: c4368d550fece2b21a9ba0481e4d088e2d6bfa058d1e1b9bbf5db6a4446dd7c5
cross pcc sha256: 57b2ccf70c8bf56bc9e4437bf7d38394378754e4b4469ca7b26431c9ca1d9f72
cross ccom sha256: 7ff430d870dca24f35f7b0c3660d6f2eb694f6303956140fc1660b58cf6bdcf6
native ccom sha256: 9b3aa47067760805e2cbad32db08c06f9c937c3a7ef974b2c8e87f108b30bf74
```

The Phase 4 and Phase 5A ROM hashes remain unchanged.  This image uses the
default `-mfix4300` path.  It has not yet run on real N64 hardware, and a
distinct `-mno-fix4300` image was not built.  QEMU cannot reproduce the
physical VR4300 multiply erratum.

## Next Step

Phase 5C will add narrow TEMP copy propagation and dead-copy elimination on
the verified SSA-lowered IR.  Its first measurable requirement is to recover
the MIPS32r2 parallel-copy overhead above without changing volatile accesses,
control flow, FP ABI behavior, or the `-mfix4300` post-pass repair.  SCCP-lite,
branch folding, unreachable-block cleanup, and LVN remain separate Phase 5D
work after the same cross/native and six-profile gates.
