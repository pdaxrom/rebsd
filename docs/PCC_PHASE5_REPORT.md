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

## Phase 5C: SSA Copy Propagation

Implementation commit `1fd10752` adds a narrow machine-independent cleanup
between SSA rename and parallel-copy lowering.

For a tree assignment `TEMP destination = TEMP source`, the pass requires:

- identical source and destination types;
- a destination in the current function's TEMP range;
- exactly one tree definition of that destination.

It then renames uses in every tree and phi input before removing the dead copy.
Because the operation aliases immutable SSA names, it does not inspect or
rewrite memory, volatile objects, calls, inline asm, or potentially trapping
expressions.

After copy propagation, a phi is trivial when every non-self input names the
same TEMP.  Its result uses are renamed to that TEMP and the now-empty edge
copies are skipped.  Simplification repeats because one collapsed phi can make
another trivial.  Any undefined (`0`) input blocks simplification: selecting a
source from another edge could otherwise create a non-dominating use.

Propagated copies remain as private sentinel placeholders until phi edge
insertion is complete, because the current CFG still points at the old block
extents.  Only nodes whose `ip_asm` pointer equals that private sentinel are
removed.  This is intentionally stricter than deleting all empty assembly
nodes; user `asm("")` and memory barriers must remain compiler barriers.

### Regression Coverage

New `misc/ssacopy001` covers integer and double copy chains through both sides
of a branch.  `misc/ssaphi001` continues to cover cyclic integer/FP parallel
copies, and optimized `gcccompat/extension004` continues to cover per-function
computed-goto fallback.

Cross regression compiled 325 of 328 tests with only the three documented
expected failures and passed all 288 runtime candidates.  Native PCC compiled
298 cases, observed 30 expected compile failures, and passed 288/288 runtime
cases.  Unexpected counts were zero and the native gate ended with
`NATIVE_PCC_REGRESS_RC:0`.

Final `ssalower.o` and `optim2.o` objects also compile in C++, F77, split-pass,
host-cross, and target-native layouts.  The existing unrelated full C++/F77
frontend failures are unchanged.

### Assembly And Compiler Size

Unoptimized `optim003.c` remains byte-identical to Phase 5A and Phase 5B:

```text
sha256 bff0d1f27f8ab0b61e07de14db8313979e788d03be521282055fb327f6fb08ef
```

| Target | Phase 5B instructions/nops | Phase 5C instructions/nops | Phase 5B bytes | Phase 5C bytes | Phase 5C loads/stores |
| --- | ---: | ---: | ---: | ---: | ---: |
| VR4300 hard | 2477 / 158 | 2475 / 157 | 79967 | 79931 | 643 / 244 |
| MIPS32r2 hard | 2551 / 113 | 2549 / 112 | 79329 | 79293 | 772 / 293 |

MIPS32r2 big- and little-endian counters match.  The improvement is small but
strictly reduces code on both targets.  The remaining difference from Phase
5A comes from nontrivial parallel-copy sets and is not removed without an
interference-aware coalescing decision.  The stripped host `ccom` grows by only
80 bytes, from 542792 to 542872.

### QEMU Matrix

All kernels and root filesystems were built with PCC.  Kernel compilation
covers `-fomit-frame-pointer`; soft profiles cover `-msoft-float`.

| Board | CPU | Endian | Float | PCC Linpack KFLOPS | Result |
| --- | --- | --- | --- | --- | --- |
| Malta64 | VR4300 | big | hard | 11753 / 11175 | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | 945 / 944 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32r2 | big | hard | 12766 / 12991 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32r2 | big | soft | 1044 / 1034 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32r2 | little | hard | 13162 / 13035 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32r2 | little | soft | 1060 / 1064 | `PCC_SMOKE_ALL_RC:0` |

Every profile also reported `PCC_SMOKE_ALL_FAILURES 0`.  Final logs are
`/private/tmp/pcc-phase5c-sentinel-{malta64,malta,maltael}-{hard,soft}.log`.

### N64 Artifact

The build-only hard-float a.out image is:

```text
sys/mips/n64/builds/20260710-phase5c-ssa-copyprop/pcc-debug.z64
implementation commit: 1fd10752
size: 6619136 bytes
sha256: ceff865c1a04b08afea35384ae2fe41abfb59036959a8017d300170eb910e955
cross pcc sha256: f8df8a87d7633c17297f4891341d773147af7aac3bb177505e2eea1725a256df
cross ccom sha256: 3b21bd3f85a90d9a77eb5561ad2a33a45e22cdc538853e7f41949b47bb523e45
native ccom sha256: f572ac10799bc546cbb2fdfabc06e4611af1bfdea6f0343eb43225ea5091a716
```

Phase 4, Phase 5A, and Phase 5B ROM hashes remain unchanged.  The Phase 5C
image uses the default `-mfix4300` path.  It has not yet run on real N64
hardware, and a distinct `-mno-fix4300` image was not built.  QEMU cannot
reproduce the physical VR4300 multiply erratum.

## Phase 5D1: Integer SCCP Lattice

Implementation commit `922085b9` adds the non-conditional first part of SCCP.
The lattice recognizes only direct assignments of unnamed integral `ICON`
nodes to an identically typed SSA TEMP.  It iterates through phis, accepting a
phi only when every defined non-self input has the same known value and exact
type.  Undefined inputs, mixed values, symbols/pointers, floating constants,
expressions, and control-flow decisions remain overdefined.

Known TEMP uses are replaced with typed `ICON` nodes.  A constant phi can skip
edge copies only when no other phi consumes its result.  This terminal-only
rule deliberately leaves some redundant copies: deleting a constant phi in a
chain could remove the definition still needed by a downstream phi that was
retained for another reason.

### Guard Rails Found By Full Builds

The first libc rebuild reached `stdio/flsbuf.c` and found an SSA TEMP whose use
had a different `n_type` from its constant definition.  PCC pass1 conversions
can create this representation.  Phase 5D1 now validates every use first; any
type mismatch makes that TEMP non-replaceable everywhere, preserving the
existing conversion path.

The first Malta64 kernel build then found a more important extended-asm case.
A constant TEMP used by `mtc0 %0,$6` was replaced by a literal, producing the
invalid instruction `mtc0 0,$6`.  Any known TEMP appearing anywhere below an
`XASM` node is now excluded from substitution regardless of its constraint or
type.  A focused `machdep.o` rebuild restored register operands, and the full
PCC kernel boot/smoke passed.  Native `pcclist/asm002` remains an additional
permanent XASM regression.

### Regression Coverage

New `misc/ssaconst001` covers equal constants reaching a branch phi and a
self-referential loop phi.  The emitted functions return typed immediates;
branch folding is intentionally deferred to Phase 5D2.

Cross regression compiled 326 of 329 tests with only the three documented
expected failures and passed all 289 runtime candidates.  Native PCC compiled
299 cases, observed 30 expected compile failures, and passed 289/289 runtime
cases.  Unexpected counts were zero and the native gate ended with
`NATIVE_PCC_REGRESS_RC:0`.  One initial QEMU retry timed out at the `login:`
prompt before starting tests; the unchanged image reran successfully and only
the complete 289/289 run is counted.

Final `ssalower.o` and `optim2.o` objects compile in C++, F77, split-pass,
host-cross, and target-native layouts.  Unoptimized `optim003.c` remains
byte-identical:

```text
sha256 bff0d1f27f8ab0b61e07de14db8313979e788d03be521282055fb327f6fb08ef
```

### Assembly And Compiler Size

Linpack counters are unchanged from Phase 5C:

| Target | Instructions/nops | Loads/stores | Assembly bytes |
| --- | ---: | ---: | ---: |
| VR4300 hard | 2475 / 157 | 643 / 244 | 79931 |
| MIPS32r2 hard | 2549 / 112 | 772 / 293 | 79293 |

The MIPS32r2 assembly hash changes because one `idamax` constant is loaded into
`$v0` after a call instead of being kept live in callee-saved `$s0` across the
call.  Counts remain identical.  The stripped host `ccom` grows by 48 bytes,
from 542872 to 542920.

### QEMU Matrix

| Board | CPU | Endian | Float | PCC Linpack KFLOPS | Result |
| --- | --- | --- | --- | --- | --- |
| Malta64 | VR4300 | big | hard | 10877 / 11403 | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | 941 / 952 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32r2 | big | hard | 12949 / 12291 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32r2 | big | soft | 1021 / 1021 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32r2 | little | hard | 13280 / 13360 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32r2 | little | soft | 1058 / 1047 | `PCC_SMOKE_ALL_RC:0` |

All kernels and root filesystems were built with PCC.  Every profile reported
`PCC_SMOKE_ALL_FAILURES 0`; kernel compilation covers
`-fomit-frame-pointer`, and soft profiles cover `-msoft-float`.  Logs are
`/private/tmp/pcc-phase5d1-xasm-{malta64,malta,maltael}-{hard,soft}.log`.

### N64 Artifact

```text
sys/mips/n64/builds/20260710-phase5d1-sccp-lite/pcc-debug.z64
implementation commit: 922085b9
size: 6619136 bytes
sha256: 9e3624f40ab31018465ce354d59221c102be71f13806d4fe6d65b038ae7d7f86
cross pcc sha256: 60d918c5f73083771bbc5bb04696bc20d9f353c38286ce3537250de05861c794
cross ccom sha256: 9390a7eb781a665869c95b42d12dfafb75b3b1ece86c38e73aecdcf05a30b2aa
native ccom sha256: 4c0ed62d0f18301c78fce9514e46c61b95461585de1072b6b3f042b6894ac423
```

Earlier preserved ROM hashes remain unchanged.  This build-only image uses
the default `-mfix4300` path.  Real N64 hardware and a distinct
`-mno-fix4300` image have not been run; QEMU cannot reproduce the physical
VR4300 multiply erratum.

## Phase 5D2: Constant Branch Folding

Implementation commit `07a126ba` folds only `CBRANCH` comparisons whose two
operands are unnamed integral `ICON` nodes after D1 substitution.  Supported
operators are signed and unsigned equality and relational comparisons.  The
pass evaluates signed and unsigned relations separately; it does not evaluate
arithmetic expressions, FP comparisons, symbols, pointers, or XASM operands.

A true condition replaces the complete branch tree with a direct `GOTO` to the
existing label.  A false condition removes the `CBRANCH` node and preserves
physical fallthrough.  The pass runs after the existing post-SSA `deljumps`,
then immediately invokes `cfg_rebuild()` and the Phase 5A verifier with stage
`post-constant-branch-fold`.  There is deliberately no second dead-code pass:
unreachable-block removal is Phase 5D3.

### Regression Coverage

New `misc/ssabranch001` constructs equal constants on both sides of a selector
branch, then consumes each phi through a constant-true or constant-false
branch.  Generated code retains the selector branch but removes the second
branch and returns the expected 17/43 values.

Cross regression compiled 327 of 330 tests with only the three documented
expected failures and passed all 290 runtime candidates.  Native PCC compiled
300 cases, observed 30 expected compile failures, and passed 290/290 runtime
cases.  Unexpected counts were zero and `NATIVE_PCC_REGRESS_RC:0`.  Final
`ssalower.o` and `optim2.o` objects compile in C++, F77, split-pass,
host-cross, and target-native layouts.

Unoptimized `optim003.c` remains byte-identical:

```text
sha256 bff0d1f27f8ab0b61e07de14db8313979e788d03be521282055fb327f6fb08ef
```

Linpack does not expose a foldable D1 branch and is byte-identical to D1:

```text
sha256 4ded49d82f017ccb2243652f808d36cd767a7248e111ccda142a9248b00c429d
VR4300: 2475 instructions, 157 nops, 643 loads, 244 stores
MIPS32r2: 2549 instructions, 112 nops, 772 loads, 293 stores
```

The stripped host `ccom` grows by 32 bytes, from 542920 to 542952.

### QEMU Matrix

| Board | CPU | Endian | Float | PCC Linpack KFLOPS | Result |
| --- | --- | --- | --- | --- | --- |
| Malta64 | VR4300 | big | hard | 11225 / 11063 | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | 938 / 957 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32r2 | big | hard | 12649 / 12720 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32r2 | big | soft | 1026 / 1009 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32r2 | little | hard | 12474 / 13084 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32r2 | little | soft | 1057 / 1034 | `PCC_SMOKE_ALL_RC:0` |

All kernels and root filesystems were built with PCC.  Every profile reported
`PCC_SMOKE_ALL_FAILURES 0`; kernel compilation covers
`-fomit-frame-pointer`, and soft profiles cover `-msoft-float`.  Logs are
`/private/tmp/pcc-phase5d2-{malta64,malta,maltael}-{hard,soft}.log`.

### N64 Artifact

```text
sys/mips/n64/builds/20260710-phase5d2-branch-fold/pcc-debug.z64
implementation commit: 07a126ba
size: 6619136 bytes
sha256: 73487bfee4a26fc1dee5c8ad819c994915e469bd4d4478f783a0c90162c5837f
cross pcc sha256: 86cc9e7c768aa4f632447a9fc5fa2e1b1e811cc5ded0b7fd663893f94a691c2b
cross ccom sha256: c69ade427fbe84d23d9a0423d89e6de0804487f57c2627da3fd4fa5f90a468da
native ccom sha256: ab2efd0d4f685229d41e10c7785172521c5da3d3cdcc12d908e0683075d0b9b5
```

Earlier preserved ROM hashes remain unchanged.  This build-only image uses
the default `-mfix4300` path.  Real N64 hardware and a distinct
`-mno-fix4300` image have not been run; QEMU cannot reproduce the physical
VR4300 multiply erratum.

## Phase 5D3: Unreachable Block Removal

Implementation commit `c19a5ceb` removes basic blocks disconnected by D2
branch folding.  It computes reachability from the current CFG rather than
using dominator DFS numbers left by an earlier graph.  The function entry,
epilogue, every `IP_DEFNAM` block, and every label listed in the function's
computed-goto table are roots; all successors of those roots are retained.

An unreachable block is removed as one complete interpass extent and every
contained tree is released.  `optimize()` then immediately calls
`cfg_rebuild()` and the Phase 5A verifier with stage
`post-unreachable-removal`.  The pass does not remove individual live-block
statements, fold expressions, perform LVN, alter delay slots, or contain MIPS
target logic.

### Regression Coverage

New `misc/ssaunreach001` exposes constant-true and constant-false branches
through equal-value phis.  Each dead side contains a volatile store.  Runtime
checks verify both selector paths and the unchanged volatile object; generated
assembly contains no dead store in either helper.

Cross regression compiled 328 of 331 tests with only the three documented
expected failures and passed all 291 runtime candidates.  Native PCC compiled
301 cases, observed 30 expected compile failures, and passed 291/291 runtime
cases.  Unexpected counts were zero and `NATIVE_PCC_REGRESS_RC:0`.  Final
`ssalower.o` and `optim2.o` objects compile in C++, F77, split-pass,
host-cross, and target-native layouts.

Unoptimized `optim003.c` remains byte-identical:

```text
sha256 bff0d1f27f8ab0b61e07de14db8313979e788d03be521282055fb327f6fb08ef
```

Linpack has no block made unreachable by D2.  Static counts remain unchanged:

```text
VR4300: 2475 instructions, 157 nops, 643 loads, 244 stores
MIPS32r2: 2549 instructions, 112 nops, 772 loads, 293 stores
MIPS32r2 sha256: 4ded49d82f017ccb2243652f808d36cd767a7248e111ccda142a9248b00c429d
```

The stripped host `ccom` grows by 48 bytes, from 542952 to 543000.

### QEMU Matrix

| Board | CPU | Endian | Float | PCC Linpack KFLOPS | Result |
| --- | --- | --- | --- | --- | --- |
| Malta64 | VR4300 | big | hard | 11670 / 11229 | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | 946 / 950 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32r2 | big | hard | 12897 / 12806 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32r2 | big | soft | 1018 / 1011 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32r2 | little | hard | 13075 / 13216 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32r2 | little | soft | 1033 / 1041 | `PCC_SMOKE_ALL_RC:0` |

All kernels and root filesystems were built with PCC.  Every profile reported
`PCC_SMOKE_ALL_FAILURES 0`; kernel compilation covers
`-fomit-frame-pointer`, and soft profiles cover `-msoft-float`.  Logs are
`/private/tmp/pcc-phase5d3-{malta64,malta,maltael}-{hard,soft}.log`.

### N64 Artifact

```text
sys/mips/n64/builds/20260710-phase5d3-unreachable/pcc-debug.z64
implementation commit: c19a5ceb
size: 6619136 bytes
sha256: 0c831754963a61f611b2cd30eb2264174ace57e54fca905d43190912f2f6b2fd
cross pcc sha256: de6cede84c63a5ac2147b4c3399c993791945b0c095ee6ff7b4ab8cc966bf763
cross ccom sha256: e7a41795e9d7e11e4f23e1f6ff63ba967290836b1838b99b876d3dcfae431d21
native ccom sha256: 146db5c4a0a9b6df5a11844667ac0f16f3216e2dc2a4cd2714ea0af8caf07c78
```

Earlier preserved ROM hashes remain unchanged.  This build-only image uses
the default `-mfix4300` path.  A real N64 run reported
`N64_PCC_DEBUG_END 0`, `N64_PCC_DEBUG_RUNNER_RC 0`, and the final
`N64_PCC_DEBUG_RC_END` marker.  A distinct `-mno-fix4300` image has not been
run; QEMU cannot reproduce the physical VR4300 multiply erratum.

## Phase 5D4: Local Value Numbering

Implementation commit `ed66b00a` adds exact local value numbering after SSA
rename and before Phase 5C TEMP-copy propagation.  Candidate expressions are
integer `PLUS`, `MINUS`, `MUL`, `AND`, `OR`, `ER`, `LS`, `RS`, `UMINUS`, and
`COMPL` trees composed only of current SSA TEMPs and unnamed integer constants.
Every node must preserve the exact PCC type and qualifiers.

Expressions are compared structurally.  The pass does not canonicalize
commutative operands and does not accept memory, pointers, floating point,
conversions, divide/remainder, comparisons, named constants, or non-SSA
registers.  Calls, XASM, memory references, stores, structure operations, and
uncertain assignments clear the value table.  This makes the pass local,
machine-independent, and independent of MIPS scheduling and VR4300 erratum
handling.

When a duplicate is found, its RHS becomes a copy from the first expression's
destination TEMP.  The existing copy-propagation pass then removes the alias,
so LVN does not need a second use-rewrite implementation.

### Regression Coverage

New `misc/ssalvn001` contains an exact repeated XOR and the same pattern across
a volatile-memory update.  Optimized output reduces the first helper from two
XOR instructions to one and retains both XORs in the barrier helper.

Cross regression compiled 329 of 332 tests with only the three documented
expected failures and passed all 292 runtime candidates.  Native PCC compiled
302 cases, observed 30 expected compile failures, and passed 292/292 runtime
cases.  Unexpected counts were zero and `NATIVE_PCC_REGRESS_RC:0`.  Final
`ssalower.o` and `optim2.o` objects compile in C++, F77, split-pass,
host-cross, and target-native layouts.

Unoptimized `optim003.c` remains byte-identical:

```text
sha256 bff0d1f27f8ab0b61e07de14db8313979e788d03be521282055fb327f6fb08ef
```

Linpack has no expression matched by D4.  Static counts remain unchanged:

```text
VR4300: 2475 instructions, 157 nops, 643 loads, 244 stores
MIPS32r2: 2549 instructions, 112 nops, 772 loads, 293 stores
```

The stripped host `ccom` grows from 543000 to 559568 bytes.

### QEMU Matrix

| Board | CPU | Endian | Float | PCC Linpack KFLOPS | Result |
| --- | --- | --- | --- | --- | --- |
| Malta64 | VR4300 | big | hard | 11560 / 11156 | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | 940 / 960 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32r2 | big | hard | 12904 / 12964 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32r2 | big | soft | 1018 / 1025 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32r2 | little | hard | 13248 / 13176 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32r2 | little | soft | 1060 / 1051 | `PCC_SMOKE_ALL_RC:0` |

All kernels and root filesystems were built with PCC.  Every profile reported
`PCC_SMOKE_ALL_FAILURES 0`; kernel compilation covers
`-fomit-frame-pointer`, and soft profiles cover `-msoft-float`.  Logs are
`/private/tmp/pcc-phase5d4-{malta64,malta,maltael}-{hard,soft}.log`.

### N64 Artifact

```text
sys/mips/n64/builds/20260710-phase5d4-lvn/pcc-debug.z64
implementation commit: ed66b00a
size: 6619136 bytes
sha256: 795878b66c824beaf3c58bb8af52f3e68a331991b827e45006a92ec01ead2956
cross pcc sha256: 778e8157e657a5bf6d5ebc741849e986d7bd3b31d3a480b46851a7830d2a3c09
cross ccom sha256: 8439c25288b37399f5a19cf8507d0eb3daf546b85bcd222261ed7719a4923081
native ccom sha256: 7e5274ca79d64647d4edfeb9d03ef1b486f6b3217d127c45013bd82dbdc938ad
```

Earlier preserved ROM hashes remain unchanged.  This build-only image uses
the default `-mfix4300` path.  Real N64 validation of D4 and a distinct
`-mno-fix4300` image remain pending; QEMU cannot reproduce the physical
VR4300 multiply erratum.

## Next Step

The next milestone is a compact late MIPS scheduler.  Start with a 3-5
instruction window and an explicit dependency model for GPR/FPR, HI/LO,
memory ordering, calls, branches, labels, and inline assembly.  First targets
are safe branch-delay filling and separating VR4300 load/FPU-use hazards when
an independent instruction already exists.  Keep the VR4300 multiply erratum
barrier mandatory under `-mfix4300` and removable only with
`-mno-fix4300`.
