# PCC MIPS Scheduler Report

## N64 Clean-Build Audit

The 2026-07-11 C2, C3, and C4 N64 hardware artifacts booted and passed their
PCC userland/debug runners, but they are not valid PCC-kernel evidence.  The
N64 build-mode stamp omitted `N64_KERNEL_COMPILER`, so stale GCC kernel objects
could be reused after selecting PCC while the rebuilt version string reported
`with pcc`.  Commit `776e41af` fixes the stamp.  The QEMU PCC-kernel results and
all assembly measurements in this report remain valid; only the affected N64
kernel-compiler attribution is corrected.

## Milestone C1: MIPS32R2 Load-Use Scheduling

Implementation commit `11f66467` extends the existing late four-instruction
load-delay reorder from VR4300 to MIPS32R2.  The pass runs on compiler-emitted
assembly after final instruction selection and before load-nop trimming and
the existing late peepholes.

The accepted input shape is:

```text
independent candidate
load
nop
first dependent use
```

The output is:

```text
load
independent candidate
first dependent use
```

Candidate instructions remain limited to forms already parsed by the driver:

- immediate GPR shifts;
- register moves;
- signed-imm16 `li`, canonicalized to one `addiu` from `$zero`;
- simple GPR ALU operations before an FPU load.

The load must have a known GPR or FPR destination, the following instruction
must use that destination, and moving the candidate must create no read/write
conflict with the load.  The pass rejects unknown syntax, memory candidates,
control transfers, register conflicts, and code immediately following a
control transfer.  It does not cross labels or reorder a memory operation with
another memory operation.

On VR4300 the candidate fills a compiler-emitted load-delay gap.  On MIPS32R2
the CPU interlocks loads, but the same dependency proof moves useful work
between the load and its first consumer.  The subsequent late peephole can
then place that work in a branch delay slot.  The VR4300 multiply-erratum
repair still runs last and is unchanged: default/explicit `-mfix4300` inserts
the required separator, while `-mno-fix4300` disables it.

## Permanent Probe

`sys/mips/n64/native/smoke-host-portablecc.sh` compiles an integer indexed-load
probe for MIPS32R2.  It requires this final order:

```text
lw   $a0,16($fp)
sll  $v0,$v1,2
addu $a0,$a0,$v0
```

The index remains live after the array load, preventing register allocation
from making the shift dependent on the base load.  This tests the intended
reorder without inline assembly or target-specific source changes.

## Static Results

VR4300 Linpack is byte-identical to Phase 5D4:

```text
sha256 7cb99ed4a516bda6102475792b37401334fb45181dfb4615ce6b5e8f07178c84
2475 instructions, 157 nops, 643 loads, 244 stores
```

MIPS32R2 Linpack has 47 changed scheduling regions.  Two independent moves
become integer branch delay-slot instructions:

```text
before sha256 4ded49d82f017ccb2243652f808d36cd767a7248e111ccda142a9248b00c429d
after  sha256 fa4f45641b2858f0cbb8c09ff67c9ebc1b173b49438b6cc8707a250aaf6749e8
instructions/nops: 2549/112 -> 2547/110
loads/stores: 772/293 -> 772/293
branch/jump delay nops: 112 -> 110
assembly bytes: 79293 -> 79346
```

The assembly-text byte count grows because moved signed-imm16 `li` aliases are
written explicitly as `addiu`; this is still one machine instruction.  The
unoptimized `optim003.c` output remains byte-identical with SHA-256
`bff0d1f27f8ab0b61e07de14db8313979e788d03be521282055fb327f6fb08ef`.

## Regression Gates

Cross regression compiled 329 of 332 tests with only the three documented
expected failures and passed all 292 runtime candidates.  Native PCC compiled
302 cases, observed 30 expected compile failures, and passed 292/292 runtime
cases.  Unexpected compile and runtime counts were zero.

All kernels and root filesystems in the final matrix were built with PCC.
Kernel builds cover `-fomit-frame-pointer`; soft profiles cover
`-msoft-float`.

| Board | CPU | Endian | Float | PCC Linpack KFLOPS | Result |
| --- | --- | --- | --- | --- | --- |
| Malta64 | VR4300 | big | hard | 10960 / 11215 | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | 959 / 950 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | hard | 12872 / 12868 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | soft | 1063 / 1086 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | hard | 13330 / 13265 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | soft | 1135 / 1146 | `PCC_SMOKE_ALL_RC:0` |

Each profile has exactly one `PCC_SMOKE_ALL_OK`,
`PCC_SMOKE_ALL_FAILURES 0`, and `PCC_SMOKE_ALL_RC:0`.  Logs are
`/private/tmp/pcc-sched1-{malta64,malta,maltael}-{hard,soft}.log`.

QEMU timing varies with host load, so the scheduler result is attributed to
the verified instruction ordering and static delay-slot counts rather than a
single runtime sample.

## N64 Artifact

```text
sys/mips/n64/builds/20260710-milestone-c1-mips32-load-schedule/pcc-debug.z64
implementation commit: 11f66467
size: 6619136 bytes
sha256: 75a50623882183eb2736cd5c1355acb266b08bc22897e3dcb1996fa46b227fc6
cross pcc sha256: 335a2ebb75fab145652fc2b98e116122419052f40192d4e19235c45424d091ad
cross ccom sha256: b9a1c51ee8d77b896909a1a2a74e1ec475230ff83d27184f68510db53a1d560f
native ccom sha256: 7e5274ca79d64647d4edfeb9d03ef1b486f6b3217d127c45013bd82dbdc938ad
```

The image is a build-only hard-float a.out control artifact.  MIPS32R2
scheduling is inactive for its VR4300 target, and native `ccom` is
byte-identical to Phase 5D4.  The image uses the default `-mfix4300` path.
Real N64 validation of this image and a distinct `-mno-fix4300` image remain
pending; QEMU cannot reproduce the physical multiply erratum.  All earlier
ROM hashes were rechecked and remain unchanged.

## Milestone C2: Branch Delay Fill Across a Load

Implementation commit `668a7d88` adds a second bounded reorder:

```text
pure GPR candidate
load
integer branch
nop
```

becomes:

```text
load
integer branch
pure GPR candidate
```

The candidate still executes on both branch paths.  It must be a move,
immediate shift, `addiu`, `addu`, or `subu` parsed by the existing simple-GPR
model.  A signed-imm16 `li` is first canonicalized to one `addiu` from
`$zero`.

The rule requires a recognized GPR/FPR load and an integer branch with known
condition registers.  It rejects any candidate/load RAW, WAR, or WAW overlap,
candidate writes to branch inputs, `$sp`, `$ra`, labels, memory candidates,
unknown syntax, and instructions already in another delay slot.  It is gated
to VR4300 and MIPS32R2, so generic MIPS3/R4000 behavior is unchanged.  No
memory operation moves relative to another memory operation, and FP multiply
instructions cannot be candidates.

### Permanent Probe

The host smoke compiles a VR4300 branch with an independent initial result.
It requires this final sequence:

```text
lw    $a0,0($v1)
bltz  $a0,target
addiu $v0,$zero,1
```

This checks the real delay-slot placement without inline assembly.  The C1
MIPS32R2 indexed-load ordering probe remains active in the same smoke.

### Static Results

C2 finds exactly two Linpack windows on each CPU:

```text
VR4300 before: 2475 instructions, 157 nops, 111 branch/jump delay nops
VR4300 after:  2473 instructions, 155 nops, 109 branch/jump delay nops
VR4300 loads/stores: 643/244 -> 643/244
VR4300 sha256: e763739687d31b0883a2d5e020d8999935780cc22357a1bcb98ac89f589b51f2

MIPS32R2 before: 2547 instructions, 110 nops, 110 branch/jump delay nops
MIPS32R2 after:  2545 instructions, 108 nops, 108 branch/jump delay nops
MIPS32R2 loads/stores: 772/293 -> 772/293
MIPS32R2 sha256: 496a1e6cec3f43e6001b3dd0f13729ccf25b1a6031eb83ffc00a684e00e82391
```

Assembly text grows by eight bytes on each target because `li` is emitted as
the explicit one-instruction `addiu` form.  Unoptimized `optim003.c` remains
byte-identical with SHA-256
`bff0d1f27f8ab0b61e07de14db8313979e788d03be521282055fb327f6fb08ef`.

### Regression Gates

Cross regression compiled 329 of 332 tests with only the three documented
expected failures and passed all 292 runtime candidates.  Native PCC compiled
302 cases, observed 30 expected compile failures, and passed 292/292 runtime
cases.  Unexpected counts were zero.

| Board | CPU | Endian | Float | PCC Linpack KFLOPS | Result |
| --- | --- | --- | --- | --- | --- |
| Malta64 | VR4300 | big | hard | 10786 / 11141 | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | 955 / 959 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | hard | 13258 / 12967 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | soft | 1075 / 1092 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | hard | 13301 / 12602 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | soft | 1132 / 1107 | `PCC_SMOKE_ALL_RC:0` |

All kernels and root filesystems were built with PCC.  Every profile has
exactly one `PCC_SMOKE_ALL_OK`, `PCC_SMOKE_ALL_FAILURES 0`, and
`PCC_SMOKE_ALL_RC:0`.  Kernel builds cover `-fomit-frame-pointer`; soft
profiles cover `-msoft-float`.  Logs are
`/private/tmp/pcc-sched2-{malta64,malta,maltael}-{hard,soft}.log`.

QEMU timing remains host-load sensitive.  C2 is attributed to the two proven
delay-slot fills and lower static instruction count, not to one timing sample.

### N64 Artifact

```text
sys/mips/n64/builds/20260710-milestone-c2-branch-load-schedule/pcc-debug.z64
implementation commit: 668a7d88
size: 6619136 bytes
sha256: 3365498bc62c2b59e4731eb881995e447b7b92092d129806cfc73773ce586595
cross pcc sha256: f1a542e9cc9872feb1597f5f2334b296fab3848a37f651ba33921f959933f9ce
cross ccom sha256: df30861569d3b4e6f76e9db15361cb12c9c45b8f22b2158f936af5a424ae6b6d
native ccom sha256: 7e5274ca79d64647d4edfeb9d03ef1b486f6b3217d127c45013bd82dbdc938ad
```

The hard-float a.out image uses the default `-mfix4300` path.  Real N64
hardware validation of C2 passed on 2026-07-11 with:

```text
N64_PCC_DEBUG_END 0
N64_PCC_DEBUG_RUNNER_RC 0
N64_PCC_DEBUG_RC_END
```

The supplied final marker did not include a numeric value, so none is inferred
here.  A distinct `-mno-fix4300` image remains untested on hardware; QEMU
cannot reproduce the physical multiply erratum.  All earlier ROM hashes were
rechecked and remain unchanged.

## Milestone C3: Dependent FPU Scheduling

Implementation commit `0331d3cf` adds a compact FPU read/write and VR4300
latency description for binary `add`, `sub`, `mul`, and `div` in single and
double precision.  The encoded VR4300 execution times come from Table 7-14 of
the local VR4300 manual: add/sub are 3 cycles, multiply is 5/8 cycles, and
divide is 29/58 cycles for single/double precision.  The manual also specifies
one additional interlock cycle when the next FPU instruction consumes the
result because there is no EX-to-EX result bypass.

C3 recognizes this exact pre-trim sequence:

```text
binary FPU producer
dependent binary FPU consumer
lw
nop
following instruction
```

and emits:

```text
binary FPU producer
lw
dependent binary FPU consumer
following instruction
```

The `lw` now supplies the missing FPU separation, and the consumer supplies
the load delay gap, so the old load `nop` is no longer needed.  Producer and
consumer FPR read/write sets must parse exactly, including both halves of a
double register pair.  The load destination must parse exactly and cannot be
`$zero`, `$sp`, or `$ra`.  The rule is enabled only for VR4300 and MIPS32R2.
It moves one load across one register-only FPU operation and never changes the
order of two memory operations.  Generic MIPS3/R4000 remains unchanged.
The implementation reuses the existing late file scan and adds a net 64 lines
to the driver backend; it does not add another pass or a general scheduler.

The final VR4300 multiply-erratum repair still runs after C3.  A moved `lw`
is a valid separator after `mul.s`/`mul.d`; if no safe separator exists,
default/explicit `-mfix4300` still inserts its `nop`.  `-mno-fix4300` still
disables only the erratum repair.

### Permanent Probe

The host smoke feeds an exact multiply/add/load-delay sequence through the
final assembly postprocessor.  Both VR4300 and MIPS32R2 must produce:

```text
mul.d  $f4,$f2,$f0
lw     $a0,20($fp)
add.d  $f6,$f6,$f4
addiu  $v1,$v0,1
```

The same probe requires generic MIPS3/R4000 to keep the original adjacent
multiply/add order.

### Static Results

C3 schedules 11 dependent Linpack windows on each selected CPU.  It changes
ordering rather than machine instruction totals:

```text
VR4300: 2473 instructions, 155 nops, 643 loads, 244 stores
VR4300 sha256: 8180454da87f3adf37ae1412027eea30a3b4dabdc09398677611a5bd1e184408

MIPS32R2: 2545 instructions, 108 nops, 772 loads, 293 stores
MIPS32R2 sha256: b7741b749544cbe2a8601688b581564bc03e2e6dad5f37ecfe7217e120bc9a3f
```

Unoptimized `optim003.c` remains byte-identical with SHA-256
`bff0d1f27f8ab0b61e07de14db8313979e788d03be521282055fb327f6fb08ef`.

### Regression Gates

Cross regression compiled 329 of 332 tests with only the three documented
expected failures and passed 292/292 runtime candidates.  Native PCC compiled
302 cases, observed 30 expected compile failures, and passed 292/292 runtime
cases.  Unexpected counts were zero.

| Board | CPU | Endian | Float | PCC Linpack KFLOPS | Result |
| --- | --- | --- | --- | --- | --- |
| Malta64 | VR4300 | big | hard | 11444 / 11849 | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | 908 / 912 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | hard | 12892 / 12903 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | soft | 1084 / 1088 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | hard | 13251 / 13145 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | soft | 1146 / 1164 | `PCC_SMOKE_ALL_RC:0` |

All kernels and root filesystems were built with PCC.  Every profile has
exactly one `PCC_SMOKE_ALL_OK`, `PCC_SMOKE_ALL_FAILURES 0`, and
`PCC_SMOKE_ALL_RC:0`.  Kernel builds cover `-fomit-frame-pointer` and
`-msoft-float`; soft userland profiles cover `-msoft-float`.  Logs are
`/private/tmp/pcc-c3-{malta64,malta,maltael}-{hard,soft}.log`.

QEMU timing remains host-load sensitive.  The attributable C3 result is the
11 verified dependency separations; real VR4300 timing requires the N64 run.

### N64 Artifact

```text
sys/mips/n64/builds/20260711-milestone-c3-fpu-latency-schedule/pcc-debug.z64
implementation commit: 0331d3cf
size: 6619136 bytes
sha256: be769daa03cc49cab660dd76b69d40eb1e14983a7a32daaf18350a7b0a4cb8cf
cross pcc sha256: bcd708815521840a32f4ad524ee196d80a68125564d138f047cf37dec755bfa1
cross ccom sha256: 9395f148a29013dcbb893de3deff901732d105e0b8e4e33420eca292cc23c79f
native ccom sha256: 7e5274ca79d64647d4edfeb9d03ef1b486f6b3217d127c45013bd82dbdc938ad
```

The hard-float a.out image uses the default `-mfix4300` path.  Real N64
hardware validation passed on 2026-07-11 with:

```text
N64_PCC_DEBUG_END 0
N64_PCC_DEBUG_RUNNER_RC 0
N64_PCC_DEBUG_RC_END
```

The supplied final marker did not include a numeric value, so none is inferred
here.  The C1, C2, and Phase 5D4 artifact hashes were rechecked and remain
unchanged.

## Milestone C4: HI/LO Gap Scheduling

Implementation commit `3c2092b7` reuses the existing five-instruction late
lookahead and the shared simple-GPR read/write parser.  It recognizes:

```text
mult/multu/dmult/dmultu
nop
nop
mflo/mfhi result
pure GPR candidate
```

and emits:

```text
mult/multu/dmult/dmultu
pure GPR candidate
nop
mflo/mfhi result
```

The candidate remains after the multiply has consumed its operands and fills
one required HI/LO gap.  It must be an exactly parsed move, immediate shift,
`addiu`, `addu`, or `subu`.  The rule rejects any candidate that reads or
writes the `mflo`/`mfhi` destination, reads or writes `$sp` or `$ra`, or
writes `$zero`.  Memory operations, control transfers, trapping arithmetic,
unknown syntax, and code in a control delay slot cannot match.  The rule is
enabled only for VR4300 and MIPS32R2; generic MIPS3/R4000 is unchanged.

C4 adds 32 backend lines and no new pass.  The final default/explicit
`-mfix4300` repair still runs last, and `-mno-fix4300` behavior is unchanged.

### Permanent Probe

The host smoke requires VR4300 and MIPS32R2 to transform an exact inline
HI/LO sequence into:

```text
mult  $v1,$v0
move  $v0,$zero
nop
mflo  $v1
```

A second sequence has an `addiu` candidate that reads the `mflo` result and
must remain after `mflo`.  Generic MIPS3/R4000 must retain both original
HI/LO nops in the positive sequence.

### Static Results

VR4300 Linpack contains two safe post-`mflo` candidates.  C4 fills one gap in
each and removes two nops:

```text
VR4300 before: 2473 instructions, 155 nops, 643 loads, 244 stores
VR4300 after:  2471 instructions, 153 nops, 643 loads, 244 stores
VR4300 sha256: 168a5871b79216b204259821397ba7992039e544288d1de7b60a9b3a5b184d2e
```

MIPS32R2 already emits a one-instruction `mul` with equivalent independent
work directly after it.  Its Linpack output remains byte-identical to C3:

```text
MIPS32R2: 2545 instructions, 108 nops, 772 loads, 293 stores
MIPS32R2 sha256: b7741b749544cbe2a8601688b581564bc03e2e6dad5f37ecfe7217e120bc9a3f
```

Unoptimized `optim003.c` remains byte-identical with SHA-256
`bff0d1f27f8ab0b61e07de14db8313979e788d03be521282055fb327f6fb08ef`.

### Regression Gates

Cross regression compiled 329 of 332 tests with only the three documented
expected failures and passed 292/292 runtime candidates.  Native PCC compiled
302 cases, observed 30 expected compile failures, and passed 292/292 runtime
cases.  Unexpected counts were zero.

| Board | CPU | Endian | Float | PCC Linpack KFLOPS | Result |
| --- | --- | --- | --- | --- | --- |
| Malta64 | VR4300 | big | hard | 12343 / 12325 | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | 941 / 981 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | hard | 13698 / 13852 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | soft | 1135 / 1124 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | hard | 13819 / 13744 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | soft | 1155 / 1152 | `PCC_SMOKE_ALL_RC:0` |

All kernels and root filesystems were built with PCC.  Every profile has
exactly one `PCC_SMOKE_ALL_OK`, `PCC_SMOKE_ALL_FAILURES 0`, and
`PCC_SMOKE_ALL_RC:0`.  Kernel builds cover `-fomit-frame-pointer` and
`-msoft-float`; soft userland profiles cover `-msoft-float`.  Logs are
`/private/tmp/pcc-c4-{malta64,malta,maltael}-{hard,soft}.log`.

QEMU timing remains host-load sensitive.  The attributable C4 result is the
two proven VR4300 gap fills and lower static instruction count.  MIPS32R2 is
a correctness control for this substep.

### N64 Artifact

```text
sys/mips/n64/builds/20260711-milestone-c4-hilo-gap-schedule/pcc-debug.z64
implementation commit: 3c2092b7
size: 6619136 bytes
sha256: 19bad4af08a0b21f346294e7dd4418d421a58c09c7e8a8972a4f7abb5e6eb04f
cross pcc sha256: e4b1ee08e24d67dcd6638713b854d49482575116c156a63d15287587536d7d2c
cross ccom sha256: 5144eff0303c2b3eebb7919f5b49d5fcb6ac9eef54ba7dc3d552d321a23e4d0f
native ccom sha256: 3243c4b7a602fbba56c79c606b5f8dde12a48131dfd86bf712489aac102b7a82
```

The hard-float a.out image uses the default `-mfix4300` path.  Real N64
hardware validation passed on 2026-07-11 with:

```text
N64_PCC_DEBUG_END 0
N64_PCC_DEBUG_RUNNER_RC 0
N64_PCC_DEBUG_RC_END
```

The supplied final marker did not include a numeric value, so none is inferred
here.  The C1, C2, and C3 artifact hashes were rechecked and remain unchanged.

## C4 Next Step

C4 closes the current conservative Milestone C scope and has passed real N64
hardware.  A general memory scheduler remains deferred until an explicit alias
model and measured target windows justify its cost.  Proceed to Milestone D
and reduce avoidable frame-pointer, outgoing-argument, spill, and stack
traffic.

## Milestone C5: VR4300 Multiply Interlock

Implementation commit `af5be45d` adds two VR4300-only integer multiply table
rules.  Signed and unsigned 32-bit multiplication now emits `mult`/`multu`
followed directly by `mflo`; the generic MIPS3 rules retain their two software
padding nops, and MIPS32R2 continues to use its real three-operand `mul`.

This is an ISA-specific interlock correction rather than a broader scheduler
pass.  VR4300 User's Manual Table 3-12 states that `MULT` and `MULTU` require
five cycles and stall the entire pipeline.  Section 4.6.4 further states that
the pipeline resumes during the multicycle instruction's last EX-stage clock.
The two generic nops before `mflo` therefore execute after the hardware MCI
wait and add no required separation on VR4300.

C5 changes only compiler-generated 32-bit `MUL`.  It does not alter division,
modulo, 64-bit multiply sequences, post-`mflo` HI/LO hazards, inline assembly,
or floating-point multiplication.  The default `-mfix4300` workaround remains
unchanged and still prevents a FP multiply from being immediately followed by
any integer or FP multiply.

Permanent host probes require immediate `mult; mflo` for VR4300, retain
`mult; nop; nop; mflo` for explicit generic MIPS3/R4000 tuning, and retain the
MIPS32R2 `mul` instruction.  The generic control now uses explicit
`-march=mips3 -mtune=r4000`; legacy `-mips3` selects VR4300 tuning and was not
a valid generic control.

### C5 Static Results

On the current E2+D3 hard-float Linpack baseline:

```text
VR4300 before: 2465 instructions, 2314 non-nops, 151 nops,
                639 loads, 244 stores, 79719 bytes
VR4300 C5:     2443 instructions, 2314 non-nops, 129 nops,
                639 loads, 244 stores, 79609 bytes

MIPS32R2:      2473 instructions, 2367 non-nops, 106 nops,
                558 loads, 276 stores, 71207 bytes (unchanged)
```

C5 removes 22 remaining software nops.  The useful instruction count,
load/store traffic, branch/jump counts, and multiply count do not change.

### C5 Regression Gates

Cross regression compiled 329 of 332 tests with only the three documented
expected failures and passed 292/292 runtime candidates.  Native PCC compiled
302 cases, observed 30 expected compile failures, and passed 292/292 runtime
cases.  Unexpected counts were zero.

| Board | CPU | Endian | Float | PCC Linpack KFLOPS | Result |
| --- | --- | --- | --- | --- | --- |
| Malta64 | VR4300 | big | hard | 12899.308 / 12733.107 | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | 1025.369 / 1036.017 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | hard | 13177.368 / 13202.633 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | soft | 1058.325 / 1059.353 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | hard | 13374.520 / 13481.118 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | soft | 1117.780 / 1117.633 | `PCC_SMOKE_ALL_RC:0` |

All kernels and root filesystems were built with PCC.  Kernel builds use
`-msoft-float -fomit-frame-pointer`.  Every profile has exactly one
`PCC_SMOKE_ALL_OK`, zero failures, `PCC_SMOKE_ALL_RC:0`, and no branch-delay
macro expansion warning.  Logs are
`/private/tmp/pcc-vr4300-interlock-{malta64,malta,maltael}-{hard,soft}.log`.

### C5 N64 Artifact

The hardware gate uses a clean GCC kernel and PCC VR4300 hard-float a.out
userland with the default `-mfix4300` policy:

```text
sys/mips/n64/builds/20260711-milestone-c5-vr4300-mult-interlock-gcc-kernel/pcc-debug.z64
implementation commit: af5be45d
build stamp: .build-mode.gcc.1.0.0.1
size: 6619136 bytes
sha256: ef2577312ea293f24aea77bcce2ea4f1c6d257cdc6076b64187f8d0ddc4b66fb
kernel ELF sha256: a1068b0e6aa93dbc8e14a94141b13b2b889ce641b87a4521d8f58e683d1c2cb1
cross pcc sha256: 751f9fb7d892522e0a8fb4a0a484fb643a3483ee7959ca8a1b3216d08b661ba2
cross ccom sha256: f20f880cfe2bf05f528c4951c436ff56218fe604c6c18bb10fe119038bef42aa
native pcc sha256: d2101d12a530cf67b7a19f885d2625029a8a5ffc0f01d8f8a7e338f9b4d52627
native ccom sha256: 90bdb7ad9409593ba82ff8f799b24872d79a62b23111cff7a698dc8ec7b5b041
debug runner sha256: b60ba962fb441f0af8cadc791598bea9b26ebc956d75cfc59587aa4ca5633020
```

QEMU does not establish the physical VR4300 MCI timing, so C5 required a real
hardware gate.  Validation passed on 2026-07-11 with:

```text
N64_PCC_DEBUG_END 0
N64_PCC_DEBUG_RUNNER_RC 0
N64_PCC_DEBUG_RC_END
```

The C5 physical VR4300 gate is closed.

## Milestone E1: FPU Transfer/Conversion Gap Fill

Implementation commit `1e37ef12` makes the existing exact
`mtc1`/conversion/FPU-load peephole run before the interlocked load-nop trim
on VR4300 and MIPS32R2.  Before E1, the same transformation already worked
for generic non-interlocked MIPS3, but the early VR4300/MIPS32R2 trim removed
the FPU-load `nop` that the late peephole used to recognize.

The accepted input is restricted to this fully parsed sequence:

```text
mtc1 GPR,FPR-A
nop
cvt.s.w/cvt.d.w FPR-A,FPR-A
FPU-load FPR-B,address
nop
binary-FPU FPR-D,...,FPR-B
```

The load destination must not overlap the conversion registers, and the
following binary operation must read the loaded register.  E1 emits:

```text
mtc1 GPR,FPR-A
FPU-load FPR-B,address
cvt.s.w/cvt.d.w FPR-A,FPR-A
binary-FPU FPR-D,...,FPR-B
```

This fills both explicit transfer/load gaps with useful work.  The VR4300
User's Manual, Table 7-14, specifies five execution cycles for integer-to-FP
conversion and an additional interlock cycle for an immediately dependent
consumer.  The load and conversion are independent, so the hardware retains
correct dependency interlocks while executing two fewer explicit `nop`s.

An `mtc1` immediately after `mfhi`/`mflo` is excluded from this early rule.
That preserves the specialized HI/LO gap fill instead of exposing the two
integer multiply nops again.  Permanent VR4300 and MIPS32R2 probes cover the
positive conversion/load sequence and the combined HI/LO sequence.  A generic
MIPS3 probe preserves its pre-E1 late-peephole behavior.

The VR4300 multiplication erratum policy is unchanged.  E1 does not move a
floating-point multiply, and default/explicit `-mfix4300` still repairs
adjacent multiply sequences while `-mno-fix4300` disables only that repair.

### Static Results

VR4300 Linpack has two matching windows.  Loads, stores, non-nop instructions,
branches, and multiply counts are unchanged:

```text
VR4300 before: 2471 instructions, 153 nops, 643 loads, 244 stores, 79929 bytes
VR4300 after:  2469 instructions, 151 nops, 643 loads, 244 stores, 79919 bytes
```

MIPS32R2 Linpack remains at 2545 instructions, 108 nops, 772 loads, and 293
stores.  Its current double constants use split `lwc1` loads and do not match
the exact single-load window, but the permanent target probe exercises E1.

### Regression Gates

Cross regression compiled 329 of 332 tests with only the three documented
expected failures and passed 292/292 runtime candidates.  Native PCC compiled
302 cases, observed 30 expected compile failures, and passed 292/292 runtime
cases.  Unexpected counts were zero.

| Board | CPU | Endian | Float | PCC Linpack KFLOPS | Result |
| --- | --- | --- | --- | --- | --- |
| Malta64 | VR4300 | big | hard | 12329 / 12690 | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | 979 / 968 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | hard | 13053 / 12963 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | soft | 1129 / 1124 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | hard | 13128 / 13240 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | soft | 1145 / 1114 | `PCC_SMOKE_ALL_RC:0` |

All kernels and root filesystems were built with PCC.  Every profile has
exactly one `PCC_SMOKE_ALL_OK`, zero failures, and `PCC_SMOKE_ALL_RC:0`.
Kernel builds cover `-msoft-float -fomit-frame-pointer`; hard and soft userland
profiles were both tested.  Logs are
`/private/tmp/pcc-fpu-{malta64,malta,maltael}-{hard,soft}.log`.

### N64 Artifact

The PCC-kernel/PCC-userland control image is retained as:

```text
sys/mips/n64/builds/20260711-milestone-e1-fpu-conversion-schedule/pcc-debug.z64
implementation commit: 1e37ef12
size: 6717440 bytes
sha256: 879427a3c45df0683538240a9f323139313ca6a0a40966a4b93f062347f4085a
kernel ELF sha256: 0436af61a37f585814cb65ccbe3410f65d264f4ff756955d676b3909654cf789
```

This image has build stamp `.build-mode.pcc.1.0.0.1`.  It is not the N64
performance or hardware gate because a PCC kernel is currently too slow on
real hardware.

The N64 hardware gate uses a GCC kernel and PCC hard-float userland:

```text
sys/mips/n64/builds/20260711-milestone-e1-fpu-conversion-schedule-gcc-kernel/pcc-debug.z64
implementation commit: 1e37ef12
size: 6619136 bytes
sha256: f452245340f0d0f4e4c5698f1103f306052449e756ce81c4c18299838d4b90f9
kernel ELF sha256: a1068b0e6aa93dbc8e14a94141b13b2b889ce641b87a4521d8f58e683d1c2cb1
```

This is a clean GCC-kernel/PCC-userland hard-float a.out image with build
stamp `.build-mode.gcc.1.0.0.1` and default `-mfix4300`.  Use this compiler
combination for future N64 hardware/performance gates unless explicitly
testing PCC kernel correctness.  Real N64 hardware validation passed with:

```text
N64_PCC_DEBUG_END 0
N64_PCC_DEBUG_RUNNER_RC 0
N64_PCC_DEBUG_RC_END
```

The final marker was supplied without a numeric value, so none is inferred.
The E1 hardware gate is closed and the next isolated scheduler substep may
proceed.

## Milestone E2: Hard-Float Expression Reload Materialization

Implementation commit `ba6074a1` extends the existing pass2 stack-reload
folding to nonvolatile hard-float expressions.  Pass2 now materializes the
expression once in a compiler `TEMP`, keeps the original frame store, and
uses that `TEMP` for an immediately adjacent reload.  This removes sequences
such as:

```text
div.d/neg.d FPR-D,...
s.d FPR-D,frame-slot
l.d FPR-R,frame-slot
```

without re-evaluating the expression or deleting the frame store.  The same
rule handles the MIPS32R2 `sdc1`/`ldc1` form.

The call-argument case is deliberately narrow.  The frame store must be
followed immediately by one call tree containing exactly one matching read.
The argument tree may contain only compiler-generated register or frame-slot
moves.  Nested calls, inline assembly, structure operations, compound
assignments, unknown-memory writes, writes to the source slot, and volatile
trees reject the transformation.  Integer and soft-float paths retain their
previous behavior.

Permanent VR4300 and MIPS32R2 host probes cover expression return and call
argument folds.  Negative probes require a volatile reload to remain and
require a reload after a nested mutating call.  The VR4300 multiplication
erratum policy is unchanged: E2 does not schedule multiply operations, and
`-mfix4300`/`-mno-fix4300` behavior remains covered by the existing probes.

### Static Results

Linpack contains four accepted windows on each hard-float target.  E2 removes
four loads and four instructions without changing stores, nops, branches,
jumps, or multiply/divide counts:

```text
VR4300 E1: 2469 instructions, 151 nops, 643 loads, 244 stores, 79919 bytes
VR4300 E2: 2465 instructions, 151 nops, 639 loads, 244 stores, 79719 bytes

MIPS32R2 E1: 2545 instructions, 108 nops, 772 loads, 293 stores, 79354 bytes
MIPS32R2 E2: 2541 instructions, 108 nops, 768 loads, 293 stores, 79194 bytes
```

### Regression Gates

Cross regression compiled 329 of 332 tests with only the same three expected
failures and passed 292/292 runtime candidates.  Native PCC compiled 302
cases, observed 30 expected compile failures, and passed 292/292 runtime
cases.  Unexpected counts were zero.

| Board | CPU | Endian | Float | PCC Linpack KFLOPS | Result |
| --- | --- | --- | --- | --- | --- |
| Malta64 | VR4300 | big | hard | 13403 / 13256 | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | 991 / 1000 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | hard | 12395 / 12878 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | soft | 1093 / 1027 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | hard | 13242 / 12914 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | soft | 1086 / 1130 | `PCC_SMOKE_ALL_RC:0` |

All six profiles used PCC for the kernel and root filesystem.  Kernel builds
used `-msoft-float -fomit-frame-pointer`.  Every log has exactly one
`PCC_SMOKE_ALL_OK`, zero failures, and `PCC_SMOKE_ALL_RC:0`:

```text
/private/tmp/pcc-fpu-e2-malta64-hard.log
/private/tmp/pcc-fpu-e2-malta64-soft.log
/private/tmp/pcc-fpu-e2-malta-hard.log
/private/tmp/pcc-fpu-e2-malta-soft.log
/private/tmp/pcc-fpu-e2-maltael-hard.log
/private/tmp/pcc-fpu-e2-maltael-soft.log
```

### N64 Artifact

The N64 hardware gate uses a GCC kernel and PCC VR4300 hard-float a.out
userland:

```text
sys/mips/n64/builds/20260711-milestone-e2-hardfp-expression-reload-gcc-kernel/pcc-debug.z64
implementation commit: ba6074a1
size: 6619136 bytes
sha256: eb4225ba55fc04e4f3dbcc33263ffc8cd55a42812bbd792f82bc2c0c0758971e
kernel ELF sha256: a1068b0e6aa93dbc8e14a94141b13b2b889ce641b87a4521d8f58e683d1c2cb1
```

The clean build has stamp `.build-mode.gcc.1.0.0.1`, identifies its kernel as
GCC 14.2.0, and uses the default `-mfix4300` policy.  Real N64 hardware
validation passed with:

```text
N64_PCC_DEBUG_END 0
N64_PCC_DEBUG_RUNNER_RC 0
N64_PCC_DEBUG_RC_END
```

The E2 hardware gate is closed and the next isolated optimization may
proceed.

## Milestone E3: VR4300 MTC1 Conversion Interlock

Implementation commit `f17acad2` lets VR4300 use its documented hardware
interlock for an exact `mtc1` followed by `cvt.s.w` or `cvt.d.w` of the same
FPR.  MIPS32R2 already used this rule.  Generic MIPS3/R4000 remains
conservative and keeps the explicit `nop`.

The VR4300 User's Manual, section 7.5.1, specifies that an instruction
immediately following a load or `MTC1` may use the loaded register and that
hardware interlocks maintain correctness.  E3 therefore removes only the
software spacer; it does not move either instruction or alter dependencies.
Permanent probes require the adjacent pair on VR4300 and MIPS32R2 and require
the spacer for explicit R4000 tuning.

The VR4300 multiplication erratum policy is unchanged.  PCC hard-float N64
userland still defaults to `-mfix4300`, and `-mno-fix4300` remains the explicit
opt-out.  The GCC reference Linpack assembly was also scanned: its 23 FP
multiplies and four integer multiplies contain no dangerous adjacent pair.

### E3 Static Results

E3 removes four Linpack spacers and 20 assembly bytes while preserving every
non-nop instruction, load, store, branch, jump, and multiply/divide:

```text
VR4300 before: 2375 instructions, 2248 non-nops, 127 nops, 71622 bytes
VR4300 after:  2371 instructions, 2248 non-nops, 123 nops, 71602 bytes
MIPS32R2:      2473 instructions, 2367 non-nops, 106 nops, 71207 bytes
```

The MIPS32R2 output is byte-identical before and after E3.

### E3 Regression Gates

Cross regression compiled 329 of 332 tests with only the three established
expected failures and passed 292/292 runtime candidates.  Native PCC compiled
302 cases, observed 30 expected compile failures, and passed 292/292 runtime
cases.  Unexpected counts were zero.

| Board | CPU | Endian | Float | PCC Linpack KFLOPS | Result |
| --- | --- | --- | --- | --- | --- |
| Malta64 | VR4300 | big | hard | 13098.799 / 12962.330 | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | 1026.511 / 1036.603 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | hard | 12307.250 / 12434.327 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | soft | 1107.073 / 1106.436 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | hard | 13209.714 / 13401.526 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | soft | 1112.348 / 1117.162 | `PCC_SMOKE_ALL_RC:0` |

All six profiles used PCC for the kernel and root filesystem.  Kernel builds
used `-msoft-float -fomit-frame-pointer`; hard/soft userland and both endian
modes were covered.  Every log has one `PCC_SMOKE_ALL_OK`, zero failures,
`PCC_SMOKE_ALL_RC:0`, and no branch-delay macro warning.  Logs are
`/private/tmp/pcc-fpu-e3-{malta64,malta,maltael}-{hard,soft}.log`.

### E3 N64 Comparison Artifact

Harness commit `cc1bfc4c` adds both GCC and PCC Linpack to the extended N64
debug run.  GCC uses a separately built GCC a.out `crt0.o`, `libc.a`, and
`libm.a`; PCC uses the PCC runtime.  Both binaries use the same source, `-O2`,
hard-float VR4300 ABI, array size 120, and one-second minimum timing window.

```text
sys/mips/n64/builds/20260711-milestone-e3-vr4300-mtc1-interlock-dual-linpack-gcc-kernel/pcc-debug.z64
implementation commit: f17acad2
harness commit: cc1bfc4c
build stamp: .build-mode.gcc.1.0.0.1
size: 6619136 bytes
sha256: 275e34890728711d6722863915331e2934065e3a63baa834298dfd920676952c
kernel ELF sha256: a1068b0e6aa93dbc8e14a94141b13b2b889ce641b87a4521d8f58e683d1c2cb1
linpack-gcc: 24600 section bytes, sha256 f4c17de2f62a9dfc8054281b3b8f584b405bce20cf99acb7dc63e06b2cad1d6c
linpack-pcc: 41696 section bytes, sha256 b6b5b8c5f3a42721cec279e1e9ac9896ff3a1ea08732974474f3214ce2e998b4
GCC libc.a sha256: aeaf1ba199f4dd5f559d69561669ba47aa72d75e974a841c9f0e67045d615b86
debug runner sha256: 930498a9c010eee3c604fe217eaf85527b29d07e2aa40f6b5e0ba278c882f9d9
```

The final clean build passed `fsutil --check`; both Linpack executables have
zero undefined symbols.  Real N64 validation passed on 2026-07-11.  The two
timed rows were:

```text
reps 8:  GCC 4455.842 KFLOPS, PCC 3079.196 KFLOPS (69.10% of GCC)
reps 16: GCC 4412.130 KFLOPS, PCC 3100.650 KFLOPS (70.28% of GCC)
```

Both binaries reported the same 15-digit machine precision and 120 by 120
array.  GCC used 85.83% of its stable run in DGEFA, 4.31% in DGESL, and 9.86%
in overhead.  PCC used 81.70% in DGEFA, 3.98% in DGESL, and 14.32% in
overhead.  This closes the E3 correctness and hardware gate, but the stable
PCC/GCC ratio remains below the 90% performance objective.  All required
status markers were zero:

```text
N64_LINPACK_RC gcc 0
N64_LINPACK_RC pcc 0
N64_PCC_DEBUG_END 0
N64_PCC_DEBUG_RUNNER_RC 0
N64_PCC_DEBUG_RC_END
```

## Milestone E4: SSA Integer Multiply Reuse

Implementation commit `a0803d4b` extends machine-independent SSA local value
numbering to repeated integer `MUL` subtrees nested inside larger expressions.
This targets Linpack's repeated `lda * row` address calculations without adding
a MIPS-only optimizer or a general CSE framework.

Candidates must have exact matching type and structure and contain only current
SSA TEMPs and unnamed integer constants.  Calls and asm delimit a region.  A
memory access does not invalidate the table because it cannot change scalar SSA
names.  A candidate is rejected if an operand is defined in the same containing
tree, preventing the materialized expression from moving before its definition.
Only expressions seen at least twice are materialized, so unique multiplies are
unchanged.

The permanent `ssalvn001` regression now covers an identical scale used by two
array accesses and a changed-index negative case.  The host smoke requires one
multiply in the first helper and two in the second for VR4300, MIPS32R2, hard
and soft float, and both endian modes.

### E4 Static Results

```text
                       instructions  non-nops  nops  loads  stores  multiplies  bytes
VR4300 E3 baseline             2371       2248   123    429     227          61  71602
VR4300 E4                      2349       2226   123    429     227          50  71140
MIPS32R2 E3 baseline           2473       2367   106    558     276          59  71207
MIPS32R2 E4                    2462       2356   106    558     276          48  70723
```

The VR4300 count names HI/LO multiply/divide sequences; the MIPS32R2 count is
the direct `mul` instruction.  `dgefa` alone drops from 52 to 42 integer
multiplies and from 1708 to 1688 instructions.  Whole-file branch and jump
counts remain 109 and 120.  Unoptimized `optim003.c` remains byte-identical at
SHA-256 `bff0d1f27f8ab0b61e07de14db8313979e788d03be521282055fb327f6fb08ef`.

### E4 Regression Gates

Cross regression compiled 329 of 332 tests with only the three established
expected failures.  Native PCC compiled 302 cases, observed 30 expected
compile failures, and passed 292/292 runtime candidates.  Unexpected compile
and runtime failures were zero, with `NATIVE_PCC_REGRESS_RC:0`.

| Board | CPU | Endian | Float | PCC Linpack KFLOPS | Result |
| --- | --- | --- | --- | --- | --- |
| Malta64 | VR4300 | big | hard | 12838.715 / 12820.140 | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | 1040.715 / 1049.102 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | hard | 13805.069 / 13567.242 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | soft | 1172.589 / 1195.857 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | hard | 13737.327 / 13751.758 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | soft | 1170.292 / 1156.240 | `PCC_SMOKE_ALL_RC:0` |

All six profiles used PCC for the kernel and root filesystem.  Every profile
reported `PCC_SMOKE_ALL_FAILURES 0`; kernel commands retained
`-msoft-float -fomit-frame-pointer`.  The transformation does not schedule FP
or integer multiplies, and the final default `-mfix4300` erratum repair remains
unchanged.

### E4 N64 Comparison Artifact

The clean hardware image uses the required GCC kernel and PCC VR4300 hard-float
a.out userland.  It retains the separate GCC-runtime and PCC-runtime Linpack
binaries from E3.

```text
sys/mips/n64/builds/20260711-milestone-e4-ssa-multiply-cse-gcc-kernel/pcc-debug.z64
implementation commit: a0803d4b
build stamp: .build-mode.gcc.1.0.0.1
size: 6619136 bytes
sha256: c6a8fb36252b426c40eaae15a856607b09aec8f418e3bf68be3855fd777a9380
kernel ELF sha256: a1068b0e6aa93dbc8e14a94141b13b2b889ce641b87a4521d8f58e683d1c2cb1
linpack-gcc: 24600 section bytes, sha256 f4c17de2f62a9dfc8054281b3b8f584b405bce20cf99acb7dc63e06b2cad1d6c
linpack-pcc: 41600 section bytes, sha256 4a5257610ac533d1cd0eba26c5a761002282dd6f0f457fe21e2eb287cefdf414
debug runner sha256: 930498a9c010eee3c604fe217eaf85527b29d07e2aa40f6b5e0ba278c882f9d9
```

The build passed `fsutil --check`; both Linpack executables have zero undefined
symbols.  Real N64 validation passed on 2026-07-12.  The timed rows were:

```text
reps 8:  GCC 4367.463 KFLOPS, PCC 3093.962 KFLOPS (70.84% of GCC)
reps 16: GCC 4411.954 KFLOPS, PCC 3094.330 KFLOPS (70.14% of GCC)
```

Both binaries reported 15-digit machine precision and all final status markers
were zero.  The stable GCC result is unchanged from E3, while PCC is 0.20%
lower than E3 and therefore within measurement noise.  E4 reduces static code
but does not improve the hardware performance ratio.  This closes the E4 gate
and makes loop induction-variable strength reduction the next measured target.
QEMU cannot reproduce the physical VR4300 multiply erratum.

## Milestone E5: VR4300 Loop Induction Strength Reduction

Implementation commit `ba59488e` adds a narrow machine-independent SSA
strength-reduction pass with a target cost hook.  It accepts only a loop header
with one preheader and one dominated latch, an integer phi updated by exactly
`+1` or `-1` in the latch, and an invariant SSA stride whose definition
dominates the preheader.  The pass creates a scaled phi and advances it with
one add or subtract on the back edge.  A zero initial index is materialized as
zero instead of multiplying in the preheader.

Only one stride is reduced per induction phi.  This deliberately bounds new
live ranges and avoids turning the pass into a general loop optimizer.  MIPS
opts in only without `MIPS_CAP_MUL3`; VR4300/MIPS III uses the transform while
MIPS32R2 retains its direct `mul` code.  An unrestricted prototype made
MIPS32R2 hard-float QEMU Linpack about 3.8% slower due to spills, so the cost
gate is based on measurement rather than ISA generality.

The permanent `ssastrength001` regression covers ascending and descending
unit-step loops plus a variable-step negative case.  VR4300 assembly must have
zero, one, and two multiplies respectively.  MIPS32R2 must retain two in all
three helpers.

### E5 Static Results

```text
                       instructions  non-nops  nops  loads  stores  branches  jumps  mul/div  bytes
VR4300 E4 baseline             2349       2226   123    429     227       109    120       50  71140
VR4300 E5                      2337       2216   121    437     230       109    120       25  71288
MIPS32R2 E4/E5                 2462       2356   106    558     276       109    120        2  70723
```

The VR4300 integer multiply count itself drops from 48 to 23; the remaining
two `mul/div` entries are integer divides.  MIPS32R2 assembly is byte-identical
to E4 and retains 48 direct `mul` instructions; the table's `mul/div` counter
tracks HI/LO sequences.  Unoptimized `optim003.c` remains byte-identical at
SHA-256 `bff0d1f27f8ab0b61e07de14db8313979e788d03be521282055fb327f6fb08ef`.

### E5 Regression Gates

Cross regression compiled 330 of 333 tests with only the three established
expected failures and produced 293 runtime candidates.  Native VR4300 PCC
compiled 303 cases, observed 30 expected compile failures, and passed 293/293
runtime cases with `NATIVE_PCC_REGRESS_RC:0`.

| Board | CPU | Endian | Float | PCC Linpack KFLOPS | Result |
| --- | --- | --- | --- | --- | --- |
| Malta64 | VR4300 | big | hard | 13433.180 / 13209.286 | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | 1033.647 / 1044.695 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | hard | 13576.847 / 13726.165 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | soft | 1187.897 / 1187.294 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | hard | 13936.018 / 13933.461 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | soft | 1155.081 / 1157.613 | `PCC_SMOKE_ALL_RC:0` |

All six profiles used PCC for the kernel and root filesystem, retained
`-msoft-float -fomit-frame-pointer` for kernels, and reported
`PCC_SMOKE_ALL_FAILURES 0`.  VR4300 hard-float's stable QEMU result is 3.0%
above E4; MIPS32R2 code is unchanged and its timing variation is environmental.
The default `-mfix4300` erratum repair is unchanged.

### E5 N64 Comparison Artifact

The clean image uses a GCC kernel and PCC VR4300 hard-float a.out userland.
The GCC Linpack still uses its separate GCC-built crt0, libc, and libm.

```text
sys/mips/n64/builds/20260712-milestone-e5-vr4300-induction-strength-gcc-kernel/pcc-debug.z64
implementation commit: ba59488e
build stamp: .build-mode.gcc.1.0.0.1
size: 6619136 bytes
sha256: 1d41e8cbd6525995ee28e6888b515a657b3b11596e0cc7a177107fab715490c3
kernel ELF sha256: a1068b0e6aa93dbc8e14a94141b13b2b889ce641b87a4521d8f58e683d1c2cb1
linpack-gcc: 24600 section bytes, sha256 f4c17de2f62a9dfc8054281b3b8f584b405bce20cf99acb7dc63e06b2cad1d6c
linpack-pcc: 41552 section bytes, sha256 72c15bc8cb517b76a3287cfd0fab4353b4fac260db29bd9088a67fff2d31ea21
debug runner sha256: 930498a9c010eee3c604fe217eaf85527b29d07e2aa40f6b5e0ba278c882f9d9
```

The build passed `fsutil --check`; both Linpack binaries have zero undefined
symbols.  Real N64 validation passed on 2026-07-12.  The timed rows were:

```text
reps 8:  GCC 4457.883 KFLOPS, PCC 3118.717 KFLOPS (69.96% of GCC)
reps 16: GCC 4412.343 KFLOPS, PCC 3163.024 KFLOPS (71.69% of GCC)
```

Both binaries reported 15-digit machine precision.  `N64_LINPACK_RC` for GCC
and PCC and all three final debug status markers were zero.  GCC changed by
only 0.009% from E4, while PCC improved by 2.22% from 3094.330 KFLOPS.  This
confirms a real, if modest, hardware gain and closes the scoped Milestone E.
The remaining gap is now assigned to measured call/inlining specialization,
not to broadening the loop transform without a workload-backed case.

## Milestone F1: Bounded Static Constant Specialization

Implementation commit `298c4565` adds a target-independent frontend/SSA
specialization path without enabling PCC's general automatic `-xinline` path.
Hosted `-O2` and `-O3` direct calls to a not-yet-defined file-local function
may select one clone when a bounded argument list contains useful `-1`, `0`,
or `1` integer constants.  The transform rejects calls after the definition,
variadic and indirect calls, `-O`, `-Os`, and `-ffreestanding` compilations.
The PCC kernel therefore remains outside the transform.

Linpack selects exactly seven helpers: `idamax`, the rolled and unrolled
`dscal`, `daxpy`, and `ddot` variants.  `dgefa` and `dgesl` remain generic.
When every use has the selected signature, only the clone is emitted; a
generic call or address use also retains the original body.

### F1 Static Results

```text
                       instructions  non-nops  nops  loads  stores  bytes
VR4300 E5 baseline             2375       2248   127    429     227  71622
VR4300 F1                      2289       2172   117    431     226  70371
MIPS32R2 F1 hard               2412       2309   103    549     272  69823
MIPS32R2 F1 soft               3695       3601    94   1016     554  99839
```

MIPS32R2 big- and little-endian assembly counters are identical for each
float mode.  The linked N64 PCC Linpack decreases from 41552 E5 section bytes
to 41360 F1 section bytes.  The permanent `ssaspecialize001` regression covers
the specialized call, a variable generic fallback, and a function pointer.

### F1 Regression Gates

Cross regression compiled 331 of 334 tests with only the three established
expected failures and produced 294 runtime candidates.  Native VR4300 PCC
compiled 304 cases, observed 30 expected compile failures, and passed 294/294
runtime cases with `NATIVE_PCC_REGRESS_RC:0`.

| Board | CPU | Endian | Float | PCC Linpack KFLOPS | Result |
| --- | --- | --- | --- | --- | --- |
| Malta64 | VR4300 | big | hard | 12970.014 / 13140.267 | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | 1071.303 / 1076.476 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | hard | 13080.427 / 13053.180 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | soft | 1078.573 / 1152.621 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | hard | 13192.531 / 13190.312 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | soft | 1181.722 / 1106.247 | `PCC_SMOKE_ALL_RC:0` |

All six profiles used PCC for both kernel and root filesystem and reported
`PCC_SMOKE_ALL_FAILURES 0`.  Kernels retained
`-msoft-float -fomit-frame-pointer` and contain no specialization clone.  The
default `-mfix4300` erratum repair is unchanged.

### F1 N64 Comparison Artifact

The clean image uses a GCC kernel and PCC VR4300 hard-float a.out userland.
The GCC Linpack uses its separate GCC-built crt0, libc, and libm; archive
comparison confirms the GCC and PCC libc files differ.

```text
sys/mips/n64/builds/20260712-milestone-f1-static-specialization-gcc-kernel/pcc-debug.z64
implementation commit: 298c4565
build stamp: .build-mode.gcc.1.0.0.1
size: 6619136 bytes
sha256: 30be99a022c0c10e3e2eb64b914071ba09ee252b49f095e00fdfc06eefcfb685
kernel ELF sha256: a6a22e43ca2cbc5fc792eeaa4a8596070034f63c4749da6e9164af8496bb717d
linpack-gcc: 24600 section bytes, sha256 f4c17de2f62a9dfc8054281b3b8f584b405bce20cf99acb7dc63e06b2cad1d6c
linpack-pcc: 41360 section bytes, sha256 4a20f3957e8e16917ff567ff57d389afc5585aac58b4f329f7db717a9b6e126f
debug runner sha256: 930498a9c010eee3c604fe217eaf85527b29d07e2aa40f6b5e0ba278c882f9d9
```

The build passed `fsutil --check`; both Linpack binaries have zero undefined
symbols.  The PCC executable contains exactly the seven expected
`__pcc_spec_*` local symbols.  Physical N64 validation passed on 2026-07-12.
The timed rows were:

```text
reps 8:  GCC 4369.091 KFLOPS, PCC 3178.149 KFLOPS (72.74% of GCC)
reps 16: GCC 4416.677 KFLOPS, PCC 3178.200 KFLOPS (71.96% of GCC)
```

Both binaries reported 15-digit machine precision and both
`N64_LINPACK_RC` values were zero.  `N64_PCC_DEBUG_END` and
`N64_PCC_DEBUG_RUNNER_RC` were zero; the terminal `N64_PCC_DEBUG_RC_END`
marker was present without a numeric value.  Against E5's stable 16-rep row,
GCC changes by 0.10% and PCC improves by 0.48%.  The static specialization
therefore produces a small measured hardware gain and closes the scoped
Milestone F at 71.96% of GCC.  The remaining gap is still dominated by work
inside the hot BLAS loops rather than by the removed static-helper branches.

## Milestone G2: Hard-Float Parameter TEMP and Load/Move Scheduling

G2 promotes optimized, non-variadic hard-float `double` and `long double`
parameters that arrive in o32 GPR pairs into compiler `TEMP`s on VR4300 and
MIPS32R2.  PCC first performs the existing ABI-correct register-pair store,
then loads the value once into the temporary.  This removes repeated frame
loads from hot helpers such as Linpack `daxpy`, including the common
`int, double, pointer` signature where the double arrives in `$a2/$a3`.

The optimization deliberately excludes soft-float, variadic functions,
generic MIPS3/R4000, and leading hard-float arguments already passed in
F12/F14.  It therefore does not change their established ABI paths.

The late scheduler also recognizes exactly this register-safe window:

```text
l.d/ldc1 loaded,...
nop
mov.s/mov.d temporary,persistent
binary-fpu consumer(loaded,temporary)
```

After proving disjoint load and move register sets and both consumer
dependencies, it emits the move before the load.  No memory operation crosses
another memory operation.  The VR4300/MIPS32R2 scheduler runs a fixed two
passes so this rule can compose with the existing C3 binary-FPU window without
adding an unbounded fixed-point optimizer.  Final VR4300 erratum repair remains
after scheduling.

### G2 Static And Timing Results

```text
                       instructions  non-nops  nops  loads  stores  bytes
VR4300 F1 baseline             2289       2172   117    431     226  70371
VR4300 G2                      2291       2174   117    416     226  70437
MIPS32R2 F1 hard              2412       2309   103    549     272  69823
MIPS32R2 G2 hard              2414       2311   103    534     272  70039
MIPS32R2 F1/G2 soft           3695       3601    94   1016     554  99839
```

Big- and little-endian MIPS32R2 have identical counters.  Each soft-float
assembly file is byte-identical to its F1 counterpart.  G2 adds two entry
instructions and removes 15 repeated loads in both hard-float targets, with no
nop, store, branch, or jump growth.

Alternating clean Malta64 GCC-kernel/PCC-userland runs measured F1 at
12920.265 and 12718.013 KFLOPS, and G2 at 13009.892 and 12874.900 KFLOPS.
The averages are 12819.139 and 12942.396 respectively, a 0.96% G2 gain; both
alternating pairs favor G2.

### G2 Regression Gates

Cross regression compiled 331 of 334 tests with only the three established
expected failures and produced 294 runtime candidates.  Native VR4300 PCC
compiled 304 cases, observed 30 expected compile failures, and passed 294/294
runtime cases with zero unexpected failures.

| Board | CPU | Endian | Float | PCC Linpack KFLOPS | Result |
| --- | --- | --- | --- | --- | --- |
| Malta64 | VR4300 | big | hard | 12692.158 / 12714.165 | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | 1075.845 / 1069.456 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | hard | 12903.245 / 13042.786 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | soft | 1147.689 / 1139.517 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | hard | 13606.701 / 13563.433 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | soft | 1197.093 / 1164.263 | `PCC_SMOKE_ALL_RC:0` |

All six profiles used PCC for both kernel and root filesystem, retained
`-msoft-float -fomit-frame-pointer` for kernels, and reported
`PCC_SMOKE_ALL_FAILURES 0`.  The default `-mfix4300` behavior and explicit
`-mfix4300` / `-mno-fix4300` selection are unchanged.  QEMU does not model the
physical multiply erratum, so the clean comparison image is also tested on
real N64 hardware.

### G2 N64 Comparison Artifact

The clean image uses a GCC kernel and PCC VR4300 hard-float a.out userland.
The GCC Linpack uses a separate GCC-built crt0, libc, and libm.  The artifact
was built after implementation commit `cf086503`.

```text
sys/mips/n64/builds/20260712-milestone-g2-fpu-param-temp-gcc-kernel/pcc-debug.z64
build stamp: .build-mode.gcc.1.0.0.1
size: 6619136 bytes
sha256: 9106cbd1b3cac395d35d4c243815a42426186199c19f73a58bc89d417abeb854
kernel ELF sha256: a1068b0e6aa93dbc8e14a94141b13b2b889ce641b87a4521d8f58e683d1c2cb1
linpack-gcc: 24600 section bytes, sha256 f4c17de2f62a9dfc8054281b3b8f584b405bce20cf99acb7dc63e06b2cad1d6c
linpack-pcc: 41360 section bytes, sha256 ffe99b95c00723c8e9bdafdca5cb3c316850087f7b44ea5f51df21bff14e6f25
debug runner sha256: 930498a9c010eee3c604fe217eaf85527b29d07e2aa40f6b5e0ba278c882f9d9
```

The build passed `fsutil --check`; both Linpack binaries have zero undefined
symbols.

Physical N64 validation passed on 2026-07-12.  The timed rows were:

```text
reps 8:  GCC 4376.078 KFLOPS, PCC 3188.039 KFLOPS (72.85% of GCC)
reps 16: GCC 4411.854 KFLOPS, PCC 3188.274 KFLOPS (72.27% of GCC)
```

Both binaries reported 15-digit machine precision and both
`N64_LINPACK_RC` values were zero.  `N64_PCC_DEBUG_END` and
`N64_PCC_DEBUG_RUNNER_RC` were zero; the terminal `N64_PCC_DEBUG_RC_END`
marker was present without a numeric value.  Against F1's stable 16-rep row,
PCC improves by 0.32% while the GCC control changes by -0.11%.  This closes
G2 at 72.27% of GCC.  Milestone G remains open because the 75-85% medium-term
target has not yet been reached.

## Milestone G3: Effective Stack-Parameter Specialization

F1 could select constant-argument clones with five or six parameters, but its
saved-inline descriptor was all-or-nothing: one stack-resident parameter made
`inline_args()` discard every parameter mapping.  The clone was emitted under
its specialized name, while stack-passed increment parameters could remain
generic inside the body.

G3 permits a partial descriptor only through a target cost hook.  On optimized
hard-float VR4300 and MIPS32R2, a selected scalar stack parameter is promoted
to a compiler `TEMP`; unselected parameters retain their existing storage.
Clone replay replaces the selected TEMP initializer with its `-1`, `0`, or
`1` constant, allowing normal SSA folding to remove the generic increment
branches and address arithmetic.

After prototype conversion, selected literal call arguments receive a common
interpass marker.  MIPS omits the corresponding o32 argument-register move or
stack store.  A stack `FUNARG` still advances the fixed or dynamic call area,
so every later argument keeps its ABI slot.  This avoids a private calling
convention and keeps the backend addition to two exact table shapes.

Partial specialization and argument elision are disabled for soft-float and
generic MIPS3/R4000.  Variadic, `-Os`, and freestanding compilations already
remain outside F1 specialization.  A permanent six-argument runtime case and
host assembly probes cover register omission, stack-slot preservation, the
generic fallback, and the generic MIPS3 negative case.

### G3 Static And A/B Results

```text
                       instructions  non-nops  nops  loads  stores  branches  jumps  bytes
VR4300 G2                      2291       2174   117    416     226       106    116  70437
VR4300 G3                      2005       1910    95    362     202        82    108  62458
MIPS32R2 G2 hard              2414       2311   103    534     272       106    116  70039
MIPS32R2 G3 hard              2124       2043    81    470     244        82    108  62030
MIPS32R2 G2/G3 soft           3695       3601    94   1016     554       106    230  99839
```

MIPS32R2 BE and LE counters are identical.  Their soft-float assembly is
byte-identical to G2; a compiler rebuilt directly from the G2 commit also
confirms byte-identical VR4300 soft-float assembly.

Four alternating Malta64 GCC-kernel/PCC-hard-float-userland runs measured the
stable 256-repetition rows as follows:

```text
G2: 12984.398, 12862.809 KFLOPS; average 12923.604
G3: 13473.443, 13263.736 KFLOPS; average 13368.590
```

G3 improves the average by 3.44%.  The two paired gains are 3.77% and 3.12%,
so both alternating comparisons favor G3.

### G3 Regression Gates

Cross regression compiled 331 of 334 tests with only the three established
expected failures and produced 294 runtime candidates.  Native VR4300 PCC
compiled 304 cases, observed 30 expected compile failures, and passed 294/294
runtime cases, including the new six-argument specialization case.

| Board | CPU | Endian | Float | PCC Linpack KFLOPS | Result |
| --- | --- | --- | --- | --- | --- |
| Malta64 | VR4300 | big | hard | 13105.422 / 13115.304 | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | byte-identical to G2 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | hard | 13419.638 / 13380.137 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | soft | 1090.437 / 1087.958 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | hard | 13440.236 / 13254.291 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | soft | 1100.336 / 1092.646 | `PCC_SMOKE_ALL_RC:0` |

The MaltaEL hard-float table uses a five-second repeat after the one-second
full-smoke sample produced a timer outlier.  All six full profiles reported
`PCC_SMOKE_ALL_FAILURES 0`; every PCC kernel retained
`-msoft-float -fomit-frame-pointer`, and no assembler macro expanded into a
multi-instruction branch delay slot.  The default `-mfix4300` behavior is
unchanged.  The clean image is also tested on physical N64.

### G3 N64 Comparison Artifact

The clean image uses a GCC kernel and PCC VR4300 hard-float a.out userland.
The GCC Linpack uses its separate GCC-built crt0, libc, and libm; the GCC and
PCC libc archives have different checksums.  The artifact was built after
implementation commit `87134c70`.

```text
sys/mips/n64/builds/20260712-milestone-g3-stack-specialization-gcc-kernel/pcc-debug.z64
build stamp: .build-mode.gcc.1.0.0.1
size: 6619136 bytes
sha256: 87ca5dce22afd995da205704126bf573d389224503f29f9b1e5a439c3c4fe748
kernel ELF sha256: a1068b0e6aa93dbc8e14a94141b13b2b889ce641b87a4521d8f58e683d1c2cb1
linpack-gcc: 24600 section bytes, sha256 f4c17de2f62a9dfc8054281b3b8f584b405bce20cf99acb7dc63e06b2cad1d6c
linpack-pcc: 40224 section bytes, sha256 7b4fee33a80f43016a1731df89ca1c0c9a1a5dccc4eb7b896aa2fb1700946074
debug runner sha256: 930498a9c010eee3c604fe217eaf85527b29d07e2aa40f6b5e0ba278c882f9d9
```

The image passed `fsutil --check`; both Linpack binaries have zero undefined
symbols.  G3 reduces the linked PCC Linpack by 1136 section bytes relative to
G2.

Physical N64 validation passed on 2026-07-12.  The timed rows were:

```text
reps 8:  GCC 4369.178 KFLOPS, PCC 3168.444 KFLOPS (72.52% of GCC)
reps 16: GCC 4411.809 KFLOPS, PCC 3186.275 KFLOPS (72.22% of GCC)
```

Both binaries reported 15-digit machine precision and both
`N64_LINPACK_RC` values were zero.  `N64_PCC_DEBUG_END` and
`N64_PCC_DEBUG_RUNNER_RC` were zero; the terminal `N64_PCC_DEBUG_RC_END`
marker was present without a numeric value.  Against G2's stable 16-rep row,
PCC changes by -0.06% while the GCC control changes by -0.001%.  The 3.44%
Malta64 QEMU gain therefore does not reproduce on physical VR4300 hardware.
G3 is retained because it corrects partial specialization and materially
reduces code size without a measured regression, but it does not advance the
hardware performance ratio.  The scoped G3 gate is closed at 72.22%; broader
Milestone G work remains open.

## Milestone G4: Repeated Constant-Shift CSE

G4 extends the existing local SSA scale materialization pass to exact integer
`TEMP << constant` expressions.  It reuses the same basic-block regions,
call/asm barriers, SSA operand checks, and temporary allocation as repeated
integer multiplication CSE; no new optimizer pass or backend tree shape is
added.  Shift counts are restricted to `1..31`.

A MIPS cost hook enables the extension only for optimized hard-float VR4300
and MIPS32R2.  Soft-float and generic MIPS3/R4000 retain the G3 behavior.  A
host assembly regression uses two independent array bases with one index: the
enabled targets emit one `sll`, while soft-float and generic MIPS3 emit two.
Changing the index between accesses always emits two.

### G4 Static And A/B Results

```text
                       instructions  non-nops  nops  loads  stores  branches  jumps  bytes
VR4300 G3                      2005       1910    95    362     202        82    108  62458
VR4300 G4                      1947       1852    95    362     202        82    108  60328
MIPS32R2 G3 hard              2124       2043    81    470     244        82    108  62030
MIPS32R2 G4 hard              2087       2006    81    470     244        82    108  60404
MIPS32R2 G3/G4 soft           3695       3601    94   1016     554       106    230  99839
```

Big- and little-endian MIPS32R2 counters are identical.  Both soft-float
assembly files are byte-identical to G3; VR4300 soft-float is also
byte-identical to its final G3 baseline.  G4 removes 58 VR4300 and 37
MIPS32R2 instructions without changing loads, stores, nops, branches, or
jumps.

Six alternating Malta64 GCC-kernel/PCC-hard-float-userland runs used the
stable 256-repetition row:

```text
G3: 13552.100, 13170.878, 13455.070 KFLOPS; average 13392.683
G4: 13315.796, 13510.866, 13557.836 KFLOPS; average 13461.499
```

The G4 average is 0.51% higher.  This is a small QEMU result near run-to-run
noise, so the code-size reduction is the firm result and physical N64 remains
the performance gate.

### G4 Regression Gates

Cross regression compiled 331 of 334 tests with the same three expected
failures and produced 294 runtime candidates.  Native VR4300 PCC compiled 304
cases, observed 30 expected failures, and passed 294/294 runtime cases.

| Board | CPU | Endian | Float | PCC Linpack KFLOPS | Result |
| --- | --- | --- | --- | --- | --- |
| Malta64 | VR4300 | big | hard | 11666.021 / 11639.007 | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | 985.588 / 996.068 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | hard | 13150.100 / 13088.184 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | soft | 1072.445 / 1074.174 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | hard | 9019.556 / 8927.989 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | soft | 1172.363 / 1160.734 | `PCC_SMOKE_ALL_RC:0` |

All six profiles used PCC for kernel and root filesystem, retained
`-msoft-float -fomit-frame-pointer` for kernels, and reported
`PCC_SMOKE_ALL_FAILURES 0`.  No assembler macro expanded into a
multi-instruction branch delay slot.  Default `-mfix4300` and explicit
`-mfix4300` / `-mno-fix4300` behavior are unchanged.  Physical N64 validation
is pending.

### G4 N64 Comparison Artifact

The clean image uses a GCC kernel and PCC VR4300 hard-float a.out userland.
Its GCC Linpack again uses a separate GCC-built crt0, libc, and libm.  The
artifact was built after implementation commit `63fac336`.

```text
sys/mips/n64/builds/20260712-milestone-g4-constant-shift-cse-gcc-kernel/pcc-debug.z64
build stamp: .build-mode.gcc.1.0.0.1
size: 6619136 bytes
sha256: 23a82ec406f916b4f040cb3c14281368901afe21784cfa74fca8af542123fcba
kernel ELF sha256: a1068b0e6aa93dbc8e14a94141b13b2b889ce641b87a4521d8f58e683d1c2cb1
linpack-gcc: 24600 section bytes, sha256 f4c17de2f62a9dfc8054281b3b8f584b405bce20cf99acb7dc63e06b2cad1d6c
linpack-pcc: 39968 section bytes, sha256 7ae61b21313b60cf99afbb4586d0ccc70fbe4fe659453327ff94d1b077444f73
debug runner sha256: 069cd338accd3206b291606f6b186747ed5be11efd8219f0c6ed67a2e8058cd1
```

The image passes `fsutil --check`; both Linpack binaries have zero undefined
symbols.  GCC and PCC libc archives have different checksums.  G4 reduces the
linked PCC Linpack by 256 section bytes relative to G3.  Physical N64
correctness and performance remain the deciding gate.

Physical N64 validation passed on 2026-07-12.  The timed rows were:

```text
reps 8:  GCC 4455.849 KFLOPS, PCC 3294.645 KFLOPS (73.94% of GCC)
reps 16: GCC 4411.821 KFLOPS, PCC 3316.659 KFLOPS (75.18% of GCC)
```

Both binaries reported 15-digit machine precision and both `N64_LINPACK_RC`
values were zero.  `N64_PCC_DEBUG_END` and `N64_PCC_DEBUG_RUNNER_RC` were
zero; the terminal `N64_PCC_DEBUG_RC_END` marker was present without a numeric
value.  Against G3's stable row, PCC improves 4.09% while the GCC control
changes 0.0003%.  Unlike G3, the G4 static scale reuse produces a clear
physical VR4300 gain.  It reaches 75.18% of GCC and closes the medium-term
75-85% gate; Milestone G remains open toward the 90-100% stretch target.

## Milestone G5: Targeted O2 Frame Omission

The ReBSD GCC MIPS target flags and N64 a.out wrapper already use
`-fomit-frame-pointer`, while a direct optimized PCC invocation did not.
G5 makes the existing PCC option the `-O2+` target default for VR4300 and
MIPS32R2.  This is selected in the driver after all `-march` and `-mtune`
options have been parsed.  Explicit `-fno-omit-frame-pointer` takes precedence
and reproduces G4 assembly byte for byte.  `-O0`, `-O1`, `-Os`, and generic
MIPS3/R4000 retain their previous policy.

Host assembly probes compare implicit and explicit omission and verify the
opt-out.  Probes for unrelated frame-relative optimizer behavior request
`-fno-omit-frame-pointer` explicitly.  The stack-specialization probe now
checks the o32 slots directly: the live fifth argument is stored at
`16($sp)`, while the omitted sixth constant is not stored at `20($sp)`.

### G5 Static And A/B Results

```text
                       instructions  non-nops  nops  loads  stores  branches  jumps  bytes
VR4300 G4                      1947       1852    95    362     202        82    108  60328
VR4300 G5                      1866       1748   118    352     192        82    108  58057
MIPS32R2 G4 hard              2087       2006    81    470     244        82    108  60404
MIPS32R2 G5 hard              1983       1876   107    459     233        82    108  57342
MIPS32R2 G4 soft              3695       3601    94   1016     554       106    230  99839
MIPS32R2 G5 soft              3667       3557   110   1014     552       106    230  99127
VR4300 G4 soft                3686       3589    97    976     556       106    230  98446
VR4300 G5 soft                3658       3545   113    974     554       106    230  97734
```

MIPS32R2 BE and LE counters are identical.  The additional scheduling nops
are outweighed by removing frame setup, saves/restores, and stack traffic.
Generic MIPS3/R4000 Linpack remains byte-identical to G4.

Five alternating Malta64 GCC-kernel/PCC-hard-float-userland pairs measured
the stable 256-repetition rows:

```text
G4: 12777.396, 12857.279, 13597.253, 13599.033, 13476.837; average 13261.560
G5: 13121.097, 13472.005, 13511.679, 13637.194, 13554.360; average 13459.267
```

Four of five pairs favor G5; the averages improve by 1.49%.

### G5 Regression Gates

Cross regression compiled 331 of 334 tests with the same three expected
failures and produced 294 runtime candidates.  Native VR4300 PCC compiled 304
cases, observed 30 expected failures, and passed 294/294 runtime cases.

| Board | CPU | Endian | Float | PCC Linpack KFLOPS | Result |
| --- | --- | --- | --- | --- | --- |
| Malta64 | VR4300 | big | hard | 11908.699 / 11401.774 | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | 983.953 / 982.233 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | hard | 13259.004 / 13094.049 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | soft | 1087.530 / 1090.075 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | hard | 8777.986 / 9052.190 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | soft | 1184.258 / 1184.055 | `PCC_SMOKE_ALL_RC:0` |

All six PCC-kernel/PCC-rootfs profiles reported
`PCC_SMOKE_ALL_FAILURES 0`.  Kernels retain their explicit
`-msoft-float -fomit-frame-pointer`; no assembler macro expanded into a
multi-instruction branch delay slot.  The VR4300 multiply erratum default and
`-mfix4300` / `-mno-fix4300` controls are unchanged.  Physical N64 validation
passed after the clean out-of-tree rebuild described below.

### G5 N64 Comparison Artifact

The clean image uses a GCC kernel and PCC VR4300 hard-float a.out userland.
The GCC Linpack has its separate GCC-built runtime.  The artifact was built
after implementation commit `0b7c35ee`.

```text
sys/mips/n64/builds/20260712-milestone-g5-o2-frame-omit-gcc-kernel/pcc-debug.z64
build stamp: .build-mode.gcc.1.0.0.1
size: 6619136 bytes
sha256: a71270c6bea582df7cfe3c104fd1f29e17dadfdda17eb3eaa4d92ef2ec0b4f19
kernel ELF sha256: a1068b0e6aa93dbc8e14a94141b13b2b889ce641b87a4521d8f58e683d1c2cb1
linpack-gcc: 24600 section bytes, sha256 f4c17de2f62a9dfc8054281b3b8f584b405bce20cf99acb7dc63e06b2cad1d6c
linpack-pcc: 39632 section bytes, sha256 9a3f7ac8f168b67586781cd4f7b0da749d8fa67219510a379a833d4c8d3e0c11
debug runner sha256: 069cd338accd3206b291606f6b186747ed5be11efd8219f0c6ed67a2e8058cd1
```

The image passes `fsutil --check`; both Linpack binaries have zero undefined
symbols and their libc archives are independently built.  The GCC binary and
kernel ELF match G4.  G5 reduces linked PCC Linpack by another 336 section
bytes.

The 2026-07-12 hardware run used a fresh out-of-tree image with SHA-256
`6624001a706a305045b2d4c0e25e577249eed0e645fe151f7bf435c1e430a305`.
Its kernel ELF SHA-256 remains
`a1068b0e6aa93dbc8e14a94141b13b2b889ce641b87a4521d8f58e683d1c2cb1`.
The measured rows were:

```text
       Reps       GCC KFLOPS       PCC KFLOPS       PCC/GCC
          8          4457.907          3257.149        73.06%
         16          4367.264          3278.433        75.07%
```

Both Linpack return codes, `N64_PCC_DEBUG_END`, and
`N64_PCC_DEBUG_RUNNER_RC` were zero; the terminal
`N64_PCC_DEBUG_RC_END` marker was present.  This closes the G5 physical gate.

## Milestone G6: Specialized Stack-Parameter Promotion

G3 promoted a selected stack-passed constant into a compiler TEMP, but the
other stack parameters of the same bounded clone retained ordinary o32 memory
homes.  The specialized Linpack BLAS clones consequently reloaded loop-invariant
pointer arguments on every iteration.  G6 extends parameter promotion to all
scalar and pointer stack parameters once a function has already passed the F1
bounded-specialization policy.

The transform is deliberately narrow.  It requires compiler TEMPs, a
non-varargs function, hard-float, and either VR4300 tuning or MIPS32R2 ISA.
Every o32 slot is still allocated and the call convention is unchanged.
Soft-float, generic MIPS3/R4000, and functions not selected for specialization
retain the old lowering.  No scheduler rule, branch delay slot, or FP multiply
sequence changes, so the VR4300 erratum default and
`-mfix4300` / `-mno-fix4300` remain unchanged.

The permanent six-argument host probe verifies that the specialized clone
loads its fifth stack pointer exactly once and does not read the dead sixth
constant slot.  The existing runtime specialization regression continues to
cover the generated clone and generic fallback.

### G6 Static And A/B Results

```text
                       instructions  non-nops  nops  loads  stores  branches  jumps  bytes
VR4300 G5                      1866       1748   118    352     192        82    108  58057
VR4300 G6                      1872       1754   118    326     192        82    108  57335
MIPS32R2 G5 hard              1983       1876   107    459     233        82    108  57342
MIPS32R2 G6 hard              1989       1882   107    433     233        82    108  56620
```

Both hard-float targets trade six one-time entry instructions for 26 fewer
loads, with no added stores, nops, or control flow.  MIPS32R2 BE and LE match.

Three clean sequential alternating Malta64 512-repetition pairs measured:

```text
G5: 13550.866, 13582.148, 13519.872; average 13550.962
G6: 13879.807, 13919.777, 13834.799; average 13878.128
```

Every pair favors G6; the average improves by 2.41%.

### G6 Regression Gates

Cross VR4300 regression passed 294/294 runtime cases.  Native VR4300 PCC
compiled 304 cases, observed 30 expected failures, and passed 294/294 runtime
cases with no unexpected compile or runtime failure.

| Board | CPU | Endian | Float | PCC Linpack KFLOPS | Result |
| --- | --- | --- | --- | --- | --- |
| Malta64 | VR4300 | big | hard | 11426.844 / 11683.786 | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | 947.181 / 939.330 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | hard | 13640.618 / 13679.007 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | soft | 1068.773 / 1062.261 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | hard | 9238.793 / 9325.104 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | soft | 1132.767 / 1125.718 | `PCC_SMOKE_ALL_RC:0` |

All six full PCC-kernel/PCC-rootfs profiles reported zero failures.  These
concurrent smoke timings are correctness evidence only; the isolated A/B pairs
above are the performance comparison.  Physical N64 validation passed as
described below.

### G6 N64 Comparison Artifact

The clean hardware image uses a GCC kernel and PCC VR4300 hard-float a.out
userland.  GCC Linpack is linked with its separate GCC-built runtime.

```text
/Users/sash/Work/N64/retrobsd-build/n64-g6-specialized-stack-params-kgcc-upcc-hard-aout/pcc-debug.z64
build stamp: .build-mode.gcc.1.0.0.1
size: 6619136 bytes
sha256: 2792c760271601826bc55aceac5e3d21c9744e6cb00489acf12e9e1bced1c3f7
kernel ELF sha256: a1068b0e6aa93dbc8e14a94141b13b2b889ce641b87a4521d8f58e683d1c2cb1
linpack-gcc: 24600 section bytes, sha256 f4c17de2f62a9dfc8054281b3b8f584b405bce20cf99acb7dc63e06b2cad1d6c
linpack-pcc: 39648 section bytes, sha256 0ea4d2162ef7c7488d903280320fb58cbd8d28dd5171eb921877b2cdd64f1a9c
debug runner sha256: 069cd338accd3206b291606f6b186747ed5be11efd8219f0c6ed67a2e8058cd1
```

The image passes all five `fsutil --check` phases.  The GCC binary and kernel
ELF are byte-identical to G5.  PCC Linpack grows by 16 section bytes, matching
the one-time entry loads.

The physical N64 run measured:

```text
       Reps       GCC KFLOPS       PCC KFLOPS       PCC/GCC
          8          4457.402          3378.316        75.79%
         16          4367.120          3404.961        77.97%
```

The stable 16-repetition PCC row improves 3.86% over G5 while the GCC control
changes by -0.003%.  Both Linpack return codes, `N64_PCC_DEBUG_END`, and
`N64_PCC_DEBUG_RUNNER_RC` were zero; the terminal `N64_PCC_DEBUG_RC_END`
marker was present.  This closes the G6 physical and commit gates.

## Rejected G7 Prototypes

Three follow-up prototypes reduced static code but failed the strict Malta64
runtime gate and were removed.  They remain useful boundaries for subsequent
work:

- Two bounded constant-specialization signatures reduced VR4300 Linpack from
  1872 to 1711 instructions, but measured 13904.908 KFLOPS against 13930.863
  for G6.  Removing one-time dispatch and dead branches did not change the hot
  loops.
- CSE of complete `base + scaled-index` addresses reduced Linpack to 1830
  instructions, but measured 13853.742 KFLOPS.  The longer pointer lifetimes
  outweighed the saved address additions.
- Affine scale canonicalization reduced Linpack to 1861 instructions with no
  load, store, nop, or spill growth.  Three alternating direct-PCC runs averaged
  14330.861 KFLOPS against 14347.187 for G6, a 0.114% regression.  Sharing the
  scaled index without converting the loop to pointer induction is neutral.

The next address optimization must therefore use loop-carried pointers and
short constant load/store offsets.  Repeating local CSE or extending the life
of complete addresses is not justified by these measurements.

## Milestone G7: VR4300 Pointer Induction

G7 adds a narrow target-independent SSA lowering hook for repeated affine
addresses in a natural induction loop.  For each eligible expression family
`base + ((i+c) << scale)`, the pass requires at least two uses, a loop-invariant
base TEMP, a signed 16-bit byte offset and latch increment, and an integer
induction phi.  It creates one pointer phi, initializes it on the preheader,
uses short constant offsets in the loop, and advances the pointer once on the
latch.  Known nonzero initial induction values are folded into the preheader
address; dynamic initial values retain one preheader shift.

The MIPS hook enables this only for hard-float VR4300.  Soft-float, generic
MIPS3/R4000, and final MIPS32R2 output are byte-identical to G6.  The transform
does not change FP instruction scheduling or delay-slot selection, so the
VR4300 multiply erratum default and the `-mfix4300` / `-mno-fix4300` controls
remain unchanged.

### G7 Static And A/B Results

```text
                       instructions  non-nops  nops  loads  stores  branches  jumps  bytes
VR4300 G6                      1872       1754   118    326     192        82    108  57335
VR4300 G7                      1775       1657   118    326     192        82    108  55583
```

The hot unrolled loops now use constant load/store offsets and two pointer
increments per four elements instead of rebuilding every scaled address.
`daxpy_ur` reaches 16 live GPRs but has no spill load or store growth.

Three strict sequential Malta64 pairs, using an identical G6 kernel, rootfs,
and runtime and replacing only the PCC Linpack binary, measured:

```text
G6: 14375.177, 14343.729, 14377.344; average 14365.417
G7: 14426.506, 14414.313, 14406.145; average 14415.655
```

Every pair favors G7; the average improves by 0.35%.  A measured MIPS32R2
prototype reduced its static count from 1989 to 1890 instructions but averaged
14238.985 KFLOPS against 14282.328 for G6, a 0.303% regression.  The MIPS32R2
hook was therefore removed rather than accepting an unproven static-only win.

### G7 Regression Gates

Cross VR4300 regression compiled 331 of 334 tests with the same three expected
failures and passed 294/294 runtime cases.  Native VR4300 PCC compiled 304
cases, observed 30 expected failures, and passed 294/294 runtime cases.  The
new ascending, nonzero-initial, and dynamic descending pointer-induction cases
all pass in both suites.

| Board | CPU | Endian | Float | PCC Linpack KFLOPS | Result |
| --- | --- | --- | --- | --- | --- |
| Malta64 | VR4300 | big | hard | 13452.178 / 13177.379 | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | 1015.812 / 1015.623 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | hard | 14379.732 / 14128.447 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | soft | 1066.252 / 1063.640 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | hard | 9761.552 / 9811.942 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | soft | 1160.635 / 1167.508 | `PCC_SMOKE_ALL_RC:0` |

All six profiles were clean out-of-tree `-j4` builds with PCC kernels and PCC
root filesystems.  Each kernel booted and every full `pcc-smoke-all.sh` run
reported `PCC_SMOKE_ALL_FAILURES 0`.  These concurrent timings are correctness
evidence only; the isolated A/B pairs above are the performance gate.

The clean builds also exposed and fixed three build-graph defects.  The MIPS
architecture Makefile now forwards all rootfs patch/repack targets, Malta64
repacking excludes `.romdisk` from `unix.bin`, and the userland awk grammar has
one stamp target that atomically owns generation of `awk.g.c` and `y.tab.h`.

### G7 N64 Comparison Artifact

The hardware image uses a GCC kernel and PCC VR4300 hard-float a.out
userland.  GCC Linpack is linked against its separate GCC-built `crt0.o`,
`libc.a`, and `libm.a`; PCC Linpack uses the PCC-built runtime.

```text
/Users/sash/Work/N64/retrobsd-build/n64-g7-pointer-induction-kgcc-upcc-hard-aout/pcc-debug.z64
build stamp: .build-mode.gcc.1.0.0.1
size: 6619136 bytes
sha256: 9ad25d3a132a85257ffcdb4408c464b4eadb3d17379923e983f96b75052f653e
kernel ELF sha256: a1068b0e6aa93dbc8e14a94141b13b2b889ce641b87a4521d8f58e683d1c2cb1
linpack-gcc: 24600 section bytes, sha256 f4c17de2f62a9dfc8054281b3b8f584b405bce20cf99acb7dc63e06b2cad1d6c
linpack-pcc: 39200 section bytes, sha256 4249f1c7015d4dab4f10ee87dbd9874b36b7fdd28807f70d00f6903be5958ac7
debug runner sha256: 87a75c56ef5a60d70b6ba853312a6d732efcffc1ef116988f4324e2a084d12a8
```

The image passes all five `fsutil --check` phases.  Both Linpack binaries have
zero undefined symbols, and the GCC and PCC libc archives are distinct.  The
GCC kernel and GCC Linpack remain byte-identical to G6; G7 reduces linked PCC
Linpack by 448 section bytes.

The physical N64 run measured:

```text
       Reps       GCC KFLOPS       PCC KFLOPS       PCC/GCC
          8          4449.710          3637.251        81.74%
         16          4453.809          3631.062        81.53%
```

The stable 16-repetition PCC row improves 6.64% over G6 and reaches 81.53% of
the same-image GCC control.  Both Linpack return codes, `N64_PCC_DEBUG_END`,
and `N64_PCC_DEBUG_RUNNER_RC` were zero; the terminal
`N64_PCC_DEBUG_RC_END` marker was present.  This closes the G7 physical and
commit gates.

## Milestone H2: Hard-Float Local Register Promotion

The H1 physical profile identified rolled `ddot` as the worst remaining
floating-point kernel at 52.35% of GCC.  Its loop-carried `double` accumulator
was stored to and loaded from the frame on every iteration even though an FPR
was available.  H2 makes the existing MIPS `cisreg()` policy accept automatic
`float`, `double`, and o32 `long double` values in hard-float mode.  Escaped
locals still remain in memory, and soft-float is unchanged.

This is a three-line target hook rather than a loop transform.  Rolled `ddot`
drops from 74 to 64 instructions and from seven to zero FP stack accesses.
Across the complete Linpack kernel benchmark, static instructions fall from
3238 to 3175 and FP stack accesses from 189 to 110.

Longer-lived FP locals also exposed conversion templates that emit `jal`
without a CALL tree node.  Their explicit MIPS register requirements now
clobber all o32 caller-saved GPRs and FPRs.  FP-returning templates preserve
`F0` as the result and clobber `F2` through `F18`; otherwise a local could
incorrectly survive `__floatunsdidf` in `F2`.  The new `fpaccum001` regression
checks a register accumulator, an address-taken local, and a live FP value
across that helper.

### H2 Regression Gates

The focused host smoke passes for VR4300 and MIPS32R2 hard-float and verifies
that VR4300 soft-float emits no hardware FP accumulator operations.  Native
hard-float regression passes 296/296 runtime tests on each of Malta64, Malta,
and Maltael.  The complete PCC-kernel/PCC-rootfs matrix is:

| Board | CPU | Endian | Float | PCC Linpack KFLOPS | Result |
| --- | --- | --- | --- | --- | --- |
| Malta64 | VR4300 | big | hard | 14221.925 / 14152.471 | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | 967.163 / 971.398 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | hard | 9288.280 / 9694.114 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | soft | 1044.128 / 1035.347 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | hard | 9486.788 / 9494.160 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | soft | 1085.256 / 1063.983 | `PCC_SMOKE_ALL_RC:0` |

Every full smoke reports `PCC_SMOKE_ALL_FAILURES 0`.  These QEMU values prove
correctness only.  The optimization does not change multiply scheduling, so
the default VR4300 `-mfix4300` workaround and `-mno-fix4300` opt-out are
unchanged.

### H2 N64 Comparison Artifact

The physical image uses a GCC kernel and PCC VR4300 hard-float a.out
userland with separate GCC/PCC runtimes and ordinary/per-kernel Linpack pairs:

```text
/Users/sash/Work/N64/retrobsd-build/n64-h2-fp-local-temp-kgcc-upcc-hard-aout/obj/sys/mips/n64/pcc-debug.z64
build stamp: .build-mode.gcc.1.0.0.1
size: 6619136 bytes
sha256: b8111a4e59d135e57e01869a11d691bde26d2e1282f8d04343e7ca1084e01165
```

The image passes all `fsutil --check` phases and contains `linpack-gcc`,
`linpack-pcc`, `linpack-kernels-gcc`, and `linpack-kernels-pcc`.

The physical N64 ordinary Linpack run measured:

| Reps | GCC KFLOPS | PCC KFLOPS | PCC/GCC |
| ---: | ---: | ---: | ---: |
| 8 | 4371.099 | 3681.212 | 84.22% |
| 16 | 4413.155 | 3706.069 | 83.98% |

The stable PCC row improves 0.89% over H1 and the PCC/GCC ratio gains 0.79
percentage points.  The per-kernel physical profile is:

| Kernel | GCC Melem/s | PCC Melem/s | PCC/GCC |
| --- | ---: | ---: | ---: |
| `daxpy_r` | 5.151 | 3.333 | 64.71% |
| `daxpy_ur` | 5.087 | 4.314 | 84.80% |
| `ddot_r` | 5.988 | 3.521 | 58.80% |
| `ddot_ur` | 6.767 | 5.444 | 80.45% |
| `dscal_r` | 7.217 | 4.878 | 67.59% |
| `dscal_ur` | 8.376 | 6.294 | 75.14% |
| `idamax` | 6.555 | 5.333 | 81.36% |

`ddot_r` improves 11.39% over H1 PCC and its GCC ratio rises from 52.35% to
58.80%; `ddot_ur` improves 3.71%.  Register promotion also keeps the
loop-carried `idamax` maximum in an FPR, improving it 10.76% and raising its
ratio from 73.44% to 81.36%.  Both self-tests and every numeric status marker
are zero, and terminal `N64_PCC_DEBUG_RC_END` is present.  This closes the H2
physical and commit gates while leaving the 90% overall target open.

## Milestone H3: Single-Use VR4300 FP Pointer Induction

H2 leaves rolled `daxpy` at 64.71% of GCC on physical VR4300.  Its PCC loop
already carries the read/write `dy` pointer through G7, but still computes the
single-use `dx + (i << 3)` address on every iteration.  H3 extends the existing
target-independent G7 pass with one target policy hook: a target may accept an
otherwise valid affine address with one use instead of the default two.

The MIPS policy permits this only for pointers to `float`, `double`, or the o32
`long double` alias, and only while the outer G7 gate selects VR4300 hard-float.
The resulting rolled `daxpy` loop carries and increments both pointers.
Soft-float, generic MIPS3, integer pointers, and MIPS32R2 retain the old
two-use threshold.  The earlier G7 MIPS32R2 A/B prototype regressed 0.303%, so
H3 does not turn a static-only reduction into a broader policy change.

An unrestricted one-use prototype was also rejected.  It shortened the whole
benchmark but extended integer-pointer lifetimes in `dgefa`, adding four loads
and four stores.  Restricting one-use induction to FP pointees removes that
register-pressure regression.

### H3 Static Results

```text
                       instructions  non-nops  nops  loads  stores  branches  jumps  bytes
VR4300 H2                      3136       2916   220    542     292       165    206  96753
VR4300 H3                      3117       2897   220    542     292       165    206  96359
```

H3 removes 19 instructions and 394 assembly bytes without increasing spills,
memory operations, control transfers, or delay-slot nops.  The host assembly
smoke requires zero scaled-index shifts in the single-use FP loop only for
VR4300 hard-float, while the equivalent integer loop and every other profile
retain one shift.

### H3 Regression Gates

Native hard-float PCC regression passes 296/296 runtime cases on Malta64,
Malta, and Maltael.  The complete PCC-kernel/PCC-rootfs matrix is:

| Board | CPU | Endian | Float | PCC Linpack KFLOPS | Result |
| --- | --- | --- | --- | --- | --- |
| Malta64 | VR4300 | big | hard | 14034.307 / 13764.868 | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | 986.886 / 987.346 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | hard | 9421.538 / 9502.427 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | soft | 1013.900 / 1003.679 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | hard | 9459.689 / 9633.487 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | soft | 1046.693 / 1055.555 | `PCC_SMOKE_ALL_RC:0` |

Every full smoke reports `PCC_SMOKE_ALL_FAILURES 0`.  Concurrent QEMU timing
is correctness evidence only; the physical N64 result below is the performance
and commit gate.  H3 does not change multiply scheduling, the default VR4300
`-mfix4300` workaround, or the explicit `-mno-fix4300` opt-out.

### H3 N64 Comparison Artifact

The physical image uses a GCC debug-UART kernel and PCC VR4300 hard-float a.out
userland.  GCC and PCC ordinary/per-kernel Linpack binaries use their
separately built runtimes:

```text
/Users/sash/Work/N64/retrobsd-build/n64-h3-fp-single-pointer-kgcc-upcc-hard-aout/obj/sys/mips/n64/pcc-debug.z64
build stamp: .build-mode.gcc.1.0.0.1
size: 6619136 bytes
sha256: 07162ebf10802a6a7d41c9ec3fe7853fb96d01e6d32562d53aca2b63d8a5684f
linpack-gcc: 24600 section bytes, sha256 f4c17de2f62a9dfc8054281b3b8f584b405bce20cf99acb7dc63e06b2cad1d6c
linpack-pcc: 38944 section bytes, sha256 7b615c08758fc241a5416030bcd6649169a0b72c93e46cf6081b4f0da0e2537a
linpack-kernels-gcc: 25584 section bytes, sha256 b4a7de6d6a36b9fa2998f35d52d9d7672e18b4f92ff127463de99f645499d309
linpack-kernels-pcc: 45400 section bytes, sha256 2fbcb2fdc0fd79e1e4a96777a18ee3f511b72a5f911f2d23196fbf7da56d6a42
```

The image passes all five `fsutil --check` phases.  Physical ordinary Linpack
measured:

| Reps | GCC KFLOPS | PCC KFLOPS | PCC/GCC |
| ---: | ---: | ---: | ---: |
| 8 | 4370.516 | 3747.746 | 85.75% |
| 16 | 4410.514 | 3693.268 | 83.74% |

The stable PCC row is 0.35% below H2 PCC, while the GCC control changes by
-0.06%; the ratio is 0.24 percentage points lower.  H3 therefore does not
establish an overall ordinary Linpack improvement.  Its per-kernel result
shows the intended rolled-loop effect:

| Kernel | GCC Melem/s | PCC Melem/s | PCC/GCC | PCC vs H2 |
| --- | ---: | ---: | ---: | ---: |
| `daxpy_r` | 5.188 | 3.583 | 69.06% | +7.50% |
| `daxpy_ur` | 5.124 | 4.313 | 84.17% | -0.02% |
| `ddot_r` | 5.988 | 4.149 | 69.29% | +17.84% |
| `ddot_ur` | 6.767 | 5.438 | 80.36% | -0.11% |
| `dscal_r` | 7.217 | 4.846 | 67.15% | -0.66% |
| `dscal_ur` | 8.376 | 6.350 | 75.81% | +0.89% |
| `idamax` | 6.555 | 5.372 | 81.95% | +0.73% |

`daxpy_r` gains 7.50% and improves its GCC ratio by 4.35 percentage points.
The two single-use input addresses in rolled `ddot` also become loop-carried,
raising `ddot_r` by 17.84% and its GCC ratio by 10.49 percentage points.  Both
self-tests, all benchmark return codes, `N64_PCC_DEBUG_END`, and
`N64_PCC_DEBUG_RUNNER_RC` are zero; terminal `N64_PCC_DEBUG_RC_END` is
present.  This closes the narrow H3 physical and commit gates while leaving
the 90% overall target open.

## Milestone H4: Bounded Hard-Float Copy Forwarding

H3 still emits repeated `mov.d $f2,$f0` copies before otherwise independent
three-operand FP operations.  The backend cannot express the profitable form
with a simple table change: making FP `MUL` destructive retained the copies,
while an `RLEFT` no-copy prototype recolored the result without emitting the
required move and generated wrong code.  Both table prototypes were rejected.

H4 instead adds a narrow final-assembly copy-forwarding pass for VR4300 and
MIPS32R2 hard-float code.  Starting at `mov.s` or `mov.d`, it scans at most 12
straight-line instructions, forwards the copied source into one recognized
three-operand `add`, `sub`, `mul`, or `div`, and removes the copy only after a
subsequent write proves the temporary dead.  It stops at labels, directives,
control transfers, unsupported instructions, or any conflicting FPR access.
Source and destination masks must be disjoint, a double kill must overwrite
both registers in the pair, and moves in branch delay slots are never changed.
This is deliberately local; it adds no general dataflow graph or scheduler
state to the backend.

The pass runs before the final VR4300 errata repair.  Consequently the default
`-mfix4300` pass still sees the final multiply stream and inserts separation
where required, while `-mno-fix4300` remains the explicit opt-out.  Host
assembly probes cover single and double forwarding, a live copied temporary,
a partial double-pair overwrite, a branch-delay-slot move, load-gap scheduling,
both CPU profiles, and both errata options.

### H4 Static Results

```text
                       instructions  non-nops  nops  loads  stores  branches  jumps  mov.s/d  bytes
VR4300 H3                      3117       2897   220    542     292       165    206       33  96359
VR4300 H4                      3094       2874   220    542     292       165    206       10  95255
```

H4 removes 23 FP copies and 1104 assembly bytes without changing memory
operations, control transfers, or delay-slot nops.  The final Linpack assembly
contains no adjacent FP-multiply/any-multiply erratum pattern.

### H4 Regression Gates

Native hard-float PCC regression passes 296/296 runtime cases on Malta64,
Malta, and Maltael.  The complete PCC-kernel/PCC-rootfs matrix is:

| Board | CPU | Endian | Float | PCC Linpack KFLOPS | Result |
| --- | --- | --- | --- | --- | --- |
| Malta64 | VR4300 | big | hard | 8173.039 / 9139.928 | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | 844.769 / 880.938 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | hard | 9880.550 / 9704.472 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | soft | 1037.474 / 1033.275 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | hard | 7964.690 / 8508.434 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | soft | 906.967 / 890.824 | `PCC_SMOKE_ALL_RC:0` |

Every full smoke reports `PCC_SMOKE_ALL_FAILURES 0`.  MaltaEL hard was also
rebuilt from a second fresh object directory and passed the complete smoke.
Concurrent QEMU timing is correctness evidence only; the physical VR4300
result below is the performance and commit gate.

### H4 N64 Comparison Artifact

The physical hardware image uses a GCC debug-UART kernel and PCC VR4300
hard-float a.out userland with independent GCC/PCC ordinary and per-kernel
Linpack runtimes:

```text
/Users/sash/Work/N64/retrobsd-build/n64-h4-fpu-copy-forward-kgcc-upcc-hard-aout/obj/sys/mips/n64/pcc-debug.z64
build stamp: .build-mode.gcc.1.0.0.1
size: 6619136 bytes
sha256: bc937face6146d435a1e063f9582d7f0e0cd209b145f7dcb9eddffdc2f5dc6ea
kernel ELF sha256: a6a22e43ca2cbc5fc792eeaa4a8596070034f63c4749da6e9164af8496bb717d
linpack-gcc: 24600 section bytes, sha256 f4c17de2f62a9dfc8054281b3b8f584b405bce20cf99acb7dc63e06b2cad1d6c
linpack-pcc: 38912 section bytes, sha256 5d8c6013441d27969da649f841e96661d82ddaeb22041a8dfcbaa66520460b12
linpack-kernels-gcc: 25584 section bytes, sha256 b4a7de6d6a36b9fa2998f35d52d9d7672e18b4f92ff127463de99f645499d309
linpack-kernels-pcc: 45320 section bytes, sha256 32be8ac76edbdeb86d020190287f773c236998b562365050a5043435a633c2fc
debug runner sha256: bc50579bd112eac1af472f17d63ed1e5f2d79ea0ad5e95edf263465d620a9357
```

The image passes all five `fsutil --check` phases and all four benchmark
binaries have zero undefined symbols.  GCC controls are byte-identical to H3;
H4 reduces ordinary PCC Linpack by 32 section bytes and PCC kernel Linpack by
80.  Physical ordinary Linpack measured:

| Reps | GCC KFLOPS | PCC KFLOPS | PCC/GCC | PCC vs H3 |
| ---: | ---: | ---: | ---: | ---: |
| 8 | 4369.936 | 3758.258 | 86.00% | +0.28% |
| 16 | 4412.843 | 3783.781 | 85.74% | +2.45% |

The stable row improves the PCC/GCC ratio by 2.01 percentage points over H3.
The physical per-kernel profile is:

| Kernel | GCC Melem/s | PCC Melem/s | PCC/GCC | PCC vs H3 |
| --- | ---: | ---: | ---: | ---: |
| `daxpy_r` | 5.188 | 3.729 | 71.88% | +4.07% |
| `daxpy_ur` | 5.124 | 4.517 | 88.15% | +4.73% |
| `ddot_r` | 5.988 | 4.150 | 69.31% | +0.02% |
| `ddot_ur` | 6.766 | 5.437 | 80.36% | -0.02% |
| `dscal_r` | 7.216 | 4.847 | 67.17% | +0.02% |
| `dscal_ur` | 8.376 | 6.439 | 76.87% | +1.40% |
| `idamax` | 6.501 | 5.333 | 82.03% | -0.73% |

The intended copy-heavy `daxpy` paths improve by 4.07% and 4.73%, and
unrolled `dscal` improves by 1.40%.  Both kernel self-tests, every benchmark
return code, `N64_PCC_DEBUG_END`, and `N64_PCC_DEBUG_RUNNER_RC` are zero;
terminal `N64_PCC_DEBUG_RC_END` is present.  This closes the H4 physical and
commit gates while leaving the 90% overall target open.

## Milestone H5: General MIPS Compiler Corpus And Correctness

H5 broadens measurement beyond Linpack before another optimization is chosen.
The new deterministic corpus runs 11 kernels: integer mixing, constant
division, branches, switches, indexed memory, libc memory operations, calls,
64-bit integer pairs, `float`, `double`, and FP/integer conversions.  Each
kernel verifies a fixed result before its adaptive timing loop.  The N64 image
runs equivalent GCC and PCC binaries linked against independently GCC- and
PCC-built runtimes, so libc or compiler-runtime code is not shared between the
two measurements.

The corpus immediately found two target correctness defects:

- A 64-bit simple operation could allocate its result pair with one register
  overlapping a source pair.  The low-word operation then destroyed the high
  word needed by the next instruction.  The MIPS table now requires an
  independent result pair for these operations, and the `$t6`/`$t7`
  `ROVERLAP` entries now name their actual adjacent pairs.
- Hard-float `float`/`double` to signed integer conversion used `cvt.w.s/d`,
  whose result follows FCSR rounding mode instead of C truncation semantics.
  Signed conversion now uses `trunc.w.s/d`.  Unsigned conversion is separate
  and calls the existing full-range `__fixunssfsi`/`__fixunsdfsi` helpers.

`misc__llpack001` reproduces the partial pair-overlap failure with an optimized
64-bit checksum.  `misc__fpint001` covers positive and negative signed
truncation plus an unsigned value above `INT_MAX`.  Malta64 hard-float native
regression passes 298/298 runtime cases, including both additions.

### H5 Regression Gates

The full PCC kernel/PCC rootfs matrix reports the following markers:

| Board | CPU | Endian | Float | General self-test | Full smoke |
| --- | --- | --- | --- | --- | --- |
| Malta64 | VR4300 | big | hard | `0` | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | `0` | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | hard | `0` | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | soft | `0` | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | hard | `0` | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | soft | `0` | `PCC_SMOKE_ALL_RC:0` |

Every profile also reports `PCC_SMOKE_ALL_FAILURES 0`.  Hard-float assembly
inspection confirms `trunc.w.s/d` for signed conversions and the unsigned
helper call; soft-float continues through runtime helpers.  H5 does not change
multiply scheduling.  Default VR4300 `-mfix4300` remains enabled and its final
postpass still validates the emitted multiply stream; `-mno-fix4300` remains
the explicit opt-out.

### H5 N64 Comparison Artifact

The hardware candidate uses a GCC debug-UART kernel and PCC VR4300 hard-float
a.out userland:

```text
/Users/sash/Work/N64/retrobsd-build/n64-h5-general-corpus-kgcc-upcc-hard-aout/obj/sys/mips/n64/pcc-debug.z64
build stamp: .build-mode.gcc.1.0.0.1
size: 8716288 bytes
sha256: 6370f6402bc86ed43495fd559a3166fa2f193345ac2827c03a16fc9c508386ae
kernel ELF sha256: a6a22e43ca2cbc5fc792eeaa4a8596070034f63c4749da6e9164af8496bb717d
linpack-gcc: 24600 section bytes, sha256 f4c17de2f62a9dfc8054281b3b8f584b405bce20cf99acb7dc63e06b2cad1d6c
linpack-pcc: 38912 section bytes, sha256 f42d4d4f158001b7f9c6441721c5973403cf75d310229c02f37f4c5bfa56ff6c
linpack-kernels-gcc: 25584 section bytes, sha256 b4a7de6d6a36b9fa2998f35d52d9d7672e18b4f92ff127463de99f645499d309
linpack-kernels-pcc: 45320 section bytes, sha256 0820617044e323d2f216a17b4f851da72ae19b66b7ac56e352c7a64a799e5b1f
mips-compiler-bench-gcc: 31720 section bytes, sha256 d16f213e58b12437ff48d699d592bfcb5258499f7aedaec0885e78253e5b8bbe
mips-compiler-bench-pcc: 46464 section bytes, sha256 a120f0010186077628b15c21a6a34e45cf26505163d10ed640363fb6ace95f37
debug runner sha256: f26e50cc18a80a6cae2b3238c1dca1599a3314a91035a177642266461a0e9923
```

The 8 MiB diagnostic filesystem passes all five `fsutil --check` phases and
has 4211 free blocks.  All six benchmark executables have zero undefined
symbols.

Physical N64 validation passes both general-corpus self-tests, both Linpack
kernel self-tests, every benchmark return code, `N64_PCC_DEBUG_END`, and
`N64_PCC_DEBUG_RUNNER_RC`; terminal `N64_PCC_DEBUG_RC_END` is present.  The
stable ordinary Linpack row is 3786.472 PCC versus 4412.654 GCC KFLOPS, or
85.81%.  PCC changes by only +0.07% from H4 and the ratio changes by +0.06
percentage points, so H5 preserves rather than improves the Linpack result.

The physical general-corpus profile is:

| Kernel | GCC Mwork/s | PCC Mwork/s | PCC/GCC |
| --- | ---: | ---: | ---: |
| `int_mix` | 6.556 | 3.427 | 52.27% |
| `const_div` | 1.355 | 0.354 | 26.13% |
| `branch` | 4.760 | 2.507 | 52.67% |
| `switch` | 4.949 | 2.706 | 54.68% |
| `memory` | 10.214 | 4.261 | 41.72% |
| `libc_memory` | 51.768 | 22.265 | 43.01% |
| `calls` | 4.854 | 2.898 | 59.70% |
| `u64` | 2.889 | 1.154 | 39.94% |
| `float` | 4.843 | 2.236 | 46.17% |
| `double` | 2.654 | 1.784 | 67.22% |
| `convert` | 2.552 | 0.403 | 15.79% |

H5 therefore closes its physical and commit gates but shows that 85.81%
Linpack is not representative of general PCC output.  H6 begins by comparing
the conversion and constant-division assembly, then addresses the smallest
shared backend or optimizer deficiency that also benefits ordinary code.  No
future change is accepted only because it improves Linpack.

## Milestone H6: Inline Unsigned 32-Bit FP Conversion

The H5 physical profile places `convert` at 15.79% of GCC, `float` at 46.17%,
and `double` at 67.22%.  Assembly comparison shows PCC calling
`__floatunsisf` or `__floatunsidf` inside each measured loop.  The call also
forces live FP accumulators through the stack.  GCC instead uses hardware
conversion because its range analysis proves the small masked values signed.

H6 implements two compact MIPS paths:

- A constant `AND` mask no greater than `INT_MAX`, or an unsigned logical
  right shift by 1 through 31, proves that the high bit is clear.  The MIPS
  pass1 hook changes only that expression result to signed and selects the
  existing `cvt.s.w` or `cvt.d.w` rule.
- Every other hard-float `uint32_t` first uses `mtc1` and `cvt.d.w`.  If its
  sign bit was set, the generated leaf sequence adds exact double `2^32`.
  A float result is rounded once from that corrected double with `cvt.s.d`,
  matching GCC's full-range sequence.  No runtime call or call-clobber set is
  needed.  Soft-float keeps the runtime-helper ABI.

The bounded path is retained because it avoids the sign test and constant
construction.  The full-range path handles values carried through a TEMP, so
the optimization does not require a new SSA range lattice.  It is shared by
VR4300 and MIPS32R2.  `misc__ufprange001` covers masked `unsigned int`, shifted
`unsigned int`, masked `unsigned long`, exact `double(0xffffffffU)`, and the
correctly rounded float result.

### H6 Static And Software Gates

Across the general compiler benchmark, H6 removes all five static
`__floatunsi*` calls and reduces generated assembly from 1750 to 1699 lines.
The PCC executable changes from 46464 to 45568 section bytes, a reduction of
896 bytes or 1.93%.  GCC remains byte-identical.  Host assembly smoke proves
the direct path for VR4300 and MIPS32R2 while retaining the full-range
semantics test.

Hard-float runtime regression passes 299/299 on Malta64, Malta, and MaltaEL,
including `misc__ufprange001`.  The complete PCC kernel/PCC rootfs matrix is:

| Board | CPU | Endian | Float | General self-test | Full smoke |
| --- | --- | --- | --- | --- | --- |
| Malta64 | VR4300 | big | hard | `0` | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | `0` | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | hard | `0` | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | soft | `0` | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | hard | `0` | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | soft | `0` | `PCC_SMOKE_ALL_RC:0` |

Every profile reports `PCC_SMOKE_ALL_FAILURES 0`.  The new sequence contains
no multiply, so it does not affect the default VR4300 `-mfix4300` repair or
the explicit `-mno-fix4300` opt-out.

### H6 N64 Comparison Artifact

```text
/Users/sash/Work/N64/retrobsd-build/n64-h6-u32-fp-range-kgcc-upcc-hard-aout/obj/sys/mips/n64/pcc-debug.z64
build stamp: .build-mode.gcc.1.0.0.1
size: 8716288 bytes
sha256: e59c80500ae0b8510b992f88130ffe5952638f408edf5de456ff074a7481e494
kernel ELF sha256: a6a22e43ca2cbc5fc792eeaa4a8596070034f63c4749da6e9164af8496bb717d
mips-compiler-bench-gcc: 31720 section bytes, sha256 d16f213e58b12437ff48d699d592bfcb5258499f7aedaec0885e78253e5b8bbe
mips-compiler-bench-pcc: 45568 section bytes, sha256 a09523f0b856eeaf398c1bc7b4a91d1a9898d6a6501f1e932ce8b5e9e4192712
debug runner sha256: f26e50cc18a80a6cae2b3238c1dca1599a3314a91035a177642266461a0e9923
```

The image passes all five `fsutil --check` phases, contains independent
GCC/PCC runtimes, and both general benchmark binaries have zero undefined
symbols.

Physical N64 validation passes both general-corpus self-tests, both Linpack
kernel self-tests, every benchmark return code, `N64_PCC_DEBUG_END`, and
`N64_PCC_DEBUG_RUNNER_RC`; terminal `N64_PCC_DEBUG_RC_END` is present.  The
three directly affected general kernels measure:

| Kernel | GCC Mwork/s | H5 PCC | H6 PCC | H6 PCC/GCC | H6 vs H5 |
| --- | ---: | ---: | ---: | ---: | ---: |
| `float` | 4.843 | 2.236 | 2.362 | 48.77% | +5.64% |
| `double` | 2.654 | 1.784 | 1.820 | 68.58% | +2.02% |
| `convert` | 2.552 | 0.403 | 1.655 | 64.85% | +310.67% |

`convert` runs 4.11 times as fast and gains 49.06 percentage points against
GCC.  None of the five H5 `__floatunsi*` calls remain in the generated H6
benchmark executable.

The stable ordinary Linpack row is 3817.158 PCC versus 4409.879 GCC KFLOPS,
or 86.56%.  PCC improves 0.81% over H5 and the ratio gains 0.75 percentage
points, while isolated Linpack-kernel rates remain within run-to-run noise.
Unrelated general-corpus PCC rates are likewise stable.  This closes H6's
physical and commit gates.  Constant division at 26.13% of GCC is now the
largest directly diagnosed general backend gap for H7.

## Milestone H7: MIPS Constant Division Strength Reduction

H7 addresses the largest H6 assembly-confirmed general gap without adding a
target-independent optimizer pass or a large backend subsystem.  Four MIPS
table entries recognize signed and unsigned 32-bit `/` and `%` whose right
operand is a non-power-of-two constant.  The emitter computes the magic
multiplier and shift while compiling, then emits one of two standard forms:

- unsigned quotient uses the high half of `multu`, an optional branch-free
  correction, and a logical right shift;
- signed quotient uses the high half of `mult`, the magic-sign dividend
  adjustment, an arithmetic shift, and sign correction for C truncation.

Modulo reuses the quotient and emits `n - q * d`.  Divisors whose magnitude
is `2^k +/- 1` rebuild `q * d` with shift/add; other constants use the low
half of a second `multu`.  Existing specialized rules still handle powers of
two, and the hardware `div`/`divu` rules still handle variable divisors.  Zero,
`+1`, `-1`, and power-of-two constants are deliberately excluded from the new
shapes.

`misc__divconst001` covers six unsigned and ten signed divisors, including
large unsigned constants and negative signed constants.  It compares 32
constant quotient/remainder functions against four volatile variable-divisor
references over boundary vectors and 1,024 deterministic pseudorandom values
per divisor, for 16,384 random comparisons plus boundaries.  The host assembly
gate compiles the test for both VR4300 and MIPS32R2: all 32 constant functions
must contain multiply arithmetic and no `div`/`divu`, while the four reference
functions must retain exactly four hardware divides.

### H7 Static And Software Gates

In the general corpus, `bench_const_div` changes from 61 instructions with
six hardware divides to 86 instructions with six magic multiplies.  Complete
PCC assembly changes from 1699 to 1723 lines.  The executable grows from 45568
to 45744 section bytes, or 176 bytes (0.39%); the GCC control remains 31720
section bytes and byte-identical.  This is an intentional code-size exchange
for replacing the VR4300's long-latency divides.

The complete PCC kernel/PCC rootfs matrix is green:

| Board | CPU | Endian | Float | General self-test | Full smoke |
| --- | --- | --- | --- | --- | --- |
| Malta64 | VR4300 | big | hard | `0` | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | `0` | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | hard | `0` | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | soft | `0` | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | hard | `0` | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | soft | `0` | `PCC_SMOKE_ALL_RC:0` |

Every profile reports `PCC_SMOKE_ALL_FAILURES 0`.  Final hard-float runtime
regression passes 300/300 on Malta64, Malta, and MaltaEL, including the
expanded `misc__divconst001`.  QEMU timing is not used to judge this change
because TCG does not model the VR4300 divide/multiply latency ratio.

The new integer multiplies are visible to the existing final emitted-stream
repair.  Default VR4300 `-mfix4300` therefore still inserts separation if an
integer multiply could immediately follow a dangerous floating-point
multiply.  `-mno-fix4300` remains the explicit opt-out, and H7 does not weaken
either mode.

### H7 N64 Comparison Artifact

```text
/Users/sash/Work/N64/retrobsd-build/n64-h7-divconst-kgcc-upcc-hard-aout/obj/sys/mips/n64/pcc-debug.z64
build stamp: .build-mode.gcc.1.0.0.1
size: 6619136 bytes
sha256: 2641efd945fd1510a09ae97ef18b033967e377a4437506b94f4449bf1864f4a2
kernel ELF sha256: a6a22e43ca2cbc5fc792eeaa4a8596070034f63c4749da6e9164af8496bb717d
mips-compiler-bench-gcc: 31720 section bytes, sha256 d16f213e58b12437ff48d699d592bfcb5258499f7aedaec0885e78253e5b8bbe
mips-compiler-bench-pcc: 45744 section bytes, sha256 02b245059bd76c8fbd09b234d82b1bc8b4c4dd45c52f5456e4afc3e8644784aa
debug runner sha256: d10b91fd719386b1bd23c62938870ab0f7be5b133f091d828421ea7d6e593541
```

The 6 MiB rootfs passes all five `fsutil --check` phases and contains
independently linked GCC/PCC ordinary Linpack, kernel Linpack, and general
compiler benchmark binaries.  All six benchmark executables have zero
undefined symbols.  The physical N64 validation below is the final H7 gate.

Physical N64 validation passes both general-corpus self-tests, both Linpack
kernel self-tests, every benchmark return code, `N64_PCC_DEBUG_END`, and
`N64_PCC_DEBUG_RUNNER_RC`; terminal `N64_PCC_DEBUG_RC_END` is present.  The
targeted result is:

| Kernel | H6 GCC | H7 GCC | H6 PCC | H7 PCC | H7 PCC/GCC | PCC gain |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `const_div` | 1.355 | 1.365 | 0.354 | 0.926 | 67.84% | +161.58% |

The PCC/GCC ratio gains 41.71 percentage points from H6's 26.13%.  The GCC
control changes by only 0.74%, while PCC executes the same verified workload
2.62 times as fast.  The other ten general-corpus PCC rates remain within
0.64% of H6, including the H6 conversion results.

The ordinary PCC Linpack rows are 3821.347 and 3756.314 KFLOPS.  Their mean is
3788.831, versus 3785.421 for H6, a +0.09% change.  The 16-repetition row alone
is 1.59% below H6, but the first row and the two-row mean are stable, and every
isolated PCC kernel is within 0.09% or faster than H6 except for improvements
of 2.11% in `ddot_ur` and 2.29% in `dscal_ur`.  This provides no evidence of
an unrelated regression.  H7 therefore passes its physical and commit gates;
constant division remains below the 90% stretch target but is no longer the
26% outlier found by H5.

## Milestone H8: Promote Local 64-Bit Integer Pairs

H8 addresses the `u64` kernel selected by the physical H7 general benchmark.
The MIPS target now lets eligible local `long long` and `unsigned long long`
values become existing `TEMP` register pairs.  No optimizer pass, new register
class, or calling-convention rule is added.  The allocator continues to use
the established caller-saved pairs for these leaf-loop temporaries, so the
callee-save and prologue implementation is unchanged.

The same audit found an off-by-one boundary in `GCLASS`: pair 52, `$s6/$s7`,
was classified as `CLASSC` because the integer-pair range ended at `< 52`.
Extending it to `< 53` makes the last declared GPR pair consistently
`CLASSB`.  H8 deliberately does not mark saved-register pairs permanent;
doing that would require coordinated overlapping-register save accounting and
would unnecessarily enlarge this milestone.

The existing optimized `misc__llpack001` regression is also the focused host
assembly gate.  For both VR4300 and MIPS32R2, `mix_words` must contain its
carry and shift operations and no `lw` or `sw` through `$fp`.  The static
change is:

| Metric | H7 VR4300 | H8 VR4300 | H7 MIPS32R2 | H8 MIPS32R2 |
| --- | ---: | ---: | ---: | ---: |
| `mix_words` lines | 179 | 156 | 177 | 154 |
| local frame accesses | 32 | 0 | 32 | 0 |

In the general corpus, `bench_u64` falls from 96 to 72 instructions.  Its
memory traffic falls from 22 loads and 12 stores to the two array loads and no
stores; the three existing `nop` instructions are unchanged.  Complete PCC
assembly falls from 1723 to 1699 lines, and the linked PCC benchmark shrinks
from 45744 to 45584 section bytes.  All GCC controls are byte-identical to H7.

### H8 Software Gates And N64 Artifact

All six PCC-kernel/PCC-rootfs full-smoke profiles pass for Malta64 VR4300,
Malta MIPS32R2, and MaltaEL MIPS32R2 in hard- and soft-float mode.  Every
profile reports `MIPS_COMPILER_BENCH_SELFTEST 0`,
`PCC_SMOKE_ALL_FAILURES 0`, and `PCC_SMOKE_ALL_RC:0`.  Final hard-float
runtime regression passes 301/301 on all three boards, including
`jira__PCC-85`, `misc__llpack001`, and `misc__llcall001`.  Native Malta64
regression reports 311 compile passes, 30 expected compile failures, and
301/301 runtime passes.

The first physical H8 candidate did not pass the correctness gate.  It
completed every benchmark with zero self-test and benchmark return codes and
improved PCC `u64` from H7's 1.153 to 1.652 Mwork/s, a 43.28% gain and 57.18%
of the 2.889 Mwork/s GCC control.  Native `ccom` nevertheless faulted while
compiling `jira/PCC-85.c`, leaving final debug and runner status one.  The log
shows 8192 KiB physical memory, 4096 KiB user memory, and 4096 KiB swap, so
the completed suite and repeatable instruction address rule out OOM.

The root cause was the backend contract for hidden 64-bit division helpers.
`zzzcode` emits `__udivdi3` and `__umoddi3` calls, but the containing nodes are
not CALL nodes.  The allocator therefore relied entirely on `NDIVB`, which
previously omitted caller-saved GPR and FPR clobbers.  H8 register promotion
left a live pointer in `$t1`; the helper overwrote it before the following
store.  `NDIVB` now includes `MIPS_CALLER_SAVED_NEVER`, the same complete
clobber set used by other hidden helpers.  `misc__llcall001` covers a live
pointer and quotient across consecutive divide/remainder calls and is staged
in the physical N64 debug rootfs.  The required saves add 80 section bytes to
the general benchmark; the corrected 45664-byte executable remains 80 bytes
smaller than H7.

```text
/Users/sash/Work/N64/retrobsd-build/n64-h8-callclobber-kgcc-upcc-hard-aout/obj/sys/mips/n64/pcc-debug.z64
build stamp: .build-mode.gcc.1.0.0.1
size: 6619136 bytes
sha256: 6dbbffb1b32a0ca1b8fbe49b9d237082a6b351aba8115f9bc8963da7fc2f1b92
kernel ELF sha256: a1068b0e6aa93dbc8e14a94141b13b2b889ce641b87a4521d8f58e683d1c2cb1
mips-compiler-bench-gcc: 31720 section bytes, sha256 d16f213e58b12437ff48d699d592bfcb5258499f7aedaec0885e78253e5b8bbe
mips-compiler-bench-pcc: 45664 section bytes, sha256 13289fc6b4eed071f9b8816bf619d75d6079ad56fd78161440e105e445ee0720
debug runner sha256: e0f5230285ae763ead65616e0bd295928239a66cc71e18c9d5b459557839ec73
```

The 6 MiB rootfs passes all five `fsutil --check` phases and all six benchmark
executables have zero undefined symbols.  It contains native compile-and-run
entries for both `PCC-85.c` and `llcall001.c`.

Physical N64 validation of the corrected image passes the complete runner,
both general and both Linpack kernel self-tests, every benchmark return code,
zero-valued `N64_PCC_DEBUG_END` and `N64_PCC_DEBUG_RUNNER_RC`, and terminal
`N64_PCC_DEBUG_RC_END`.  The targeted result is:

| Kernel | H7 GCC | H8 GCC | H7 PCC | H8 PCC | H8 PCC/GCC | PCC gain |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `u64` | 2.888 | 2.868 | 1.153 | 1.653 | 57.64% | +43.37% |

The PCC/GCC ratio gains 17.71 percentage points from H7's 39.92%, while the
GCC control changes by only -0.69%.  The corrected result is effectively
identical to the rejected candidate's 1.652 Mwork/s, confirming that the
caller-clobber correction preserves the intended H8 optimization.

The ordinary PCC Linpack rows are 3756.395 and 3789.017 KFLOPS.  Their mean is
3772.706, only 0.43% below H7 and 85.12% of the current 4432.440 KFLOPS GCC
mean.  Every isolated kernel also passes its checksum and return-code gates.
H8 therefore passes its physical and commit gates; QEMU timing was not used
to judge the optimization.

## Milestone H9A: Integer And Symbol Address Induction

H9A broadens the existing target-independent SSA address-induction lowering
instead of adding another MIPS-only loop pass.  The matcher now accepts a
loop-invariant symbol `ICON` as the base of `base + (index << scale)`, carries
that base into the preheader, and advances a pointer phi on the latch.  It
also accepts PCC's mixed signedness representation of unsigned pointer
scaling: the induction TEMP must still have the exact phi type, but the
compiler-generated `LS` node and shift count may use another integer type.

The MIPS cost hooks enable integer address induction on VR4300 and MIPS32R2 in
both hard- and soft-float modes.  A register base still needs at least two
matching addresses per iteration.  A single global-symbol address is allowed
because its base load moves out of the loop; the previous single-use register
exception remains limited to FP pointees on hard-float VR4300.

An unrestricted prototype demonstrated why the distinction is necessary.
It converted the one-use `ipvt[k]` store in Linpack `dgefa`, occupied `$s7`
with the pointer, spilled the previous `$s7` value, restored a frame pointer,
and grew the function.  With the final gate, complete Linpack assembly is
byte-identical to H8 for both VR4300 and MIPS32R2.

The first loop in the general `bench_memory` kernel now materializes
`input_a`, `input_b`, and `output_a` once before the loop and uses three
four-byte pointer increments.  Its function changes from 59 to 56
instructions; `sll` changes from four to three and address `addu` from seven
to four.  The nonlinear `(i * 17) & 255` loop is deliberately unchanged.

`misc__ssastrength001` now includes signed and unsigned global arrays.  The
VR4300 and MIPS32R2 host gates require zero scaled-index shifts for repeated
integer register bases and one-use global bases.  A one-use integer parameter
retains its shift, while the existing one-use FP reduction remains specific
to hard-float VR4300.  Both host suites pass.

Hard-float runtime regression passes 301/301 on Malta64, Malta, and MaltaEL,
including the extended `misc__ssastrength001`.  The native Malta64 regression
reports 311 compile passes, 30 expected compile failures, and 301/301 runtime
passes.  The complete PCC-kernel/PCC-rootfs matrix is also green:

| Board | CPU | Endian | Float | General self-test | Full smoke |
| --- | --- | --- | --- | --- | --- |
| Malta64 | VR4300 | big | hard | `0` | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | `0` | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | hard | `0` | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | soft | `0` | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | hard | `0` | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | soft | `0` | `PCC_SMOKE_ALL_RC:0` |

Every full smoke reports `PCC_SMOKE_ALL_FAILURES 0`.

The retained physical candidate is a clean GCC-kernel/PCC-VR4300-hard-float
a.out-userland build at
`/Users/sash/Work/N64/retrobsd-build/n64-h9a-address-induction-kgcc-upcc-hard-aout/obj/sys/mips/n64/pcc-debug.z64`.
It is 6619136 bytes with SHA-256
`a0c5b5c7cf15f1c471bd8a39dad9b25864a39e3b9ac52c60c6736f203a99b5ad`
and stamp `.build-mode.gcc.1.0.0.1`.  The kernel `unix.elf` SHA-256 is
`a1068b0e6aa93dbc8e14a94141b13b2b889ce641b87a4521d8f58e683d1c2cb1`,
identical to H8.  The GCC Linpack, kernel-Linpack, and general benchmark
controls are also byte-identical to H8.

The final staged benchmark section sizes are 24600/39024 bytes for GCC/PCC
Linpack, 25584/45560 for GCC/PCC kernel Linpack, and 31720/45520 for the
GCC/PCC general benchmark.  The PCC general benchmark is 144 bytes smaller
than H8.  All six executables have zero undefined symbols.  The 6144 KiB
debug UFS passes all five `fsutil --check` phases with 357 files, 3964 used
blocks, and 2155 free blocks.  The boot-time C runner includes both
`misc__llcall001` and `misc__ssastrength001`; this closes the gap where those
sources were present in the retained shell script but absent from the runner
actually invoked by `/etc/rc`.

Physical N64 validation passes both general and Linpack-kernel self-tests,
all benchmark return codes, zero-valued `N64_PCC_DEBUG_END` and
`N64_PCC_DEBUG_RUNNER_RC`, and the terminal `N64_PCC_DEBUG_RC_END` marker.
The general benchmark comparison against the corrected H8 hardware run is:

| Kernel | H8 PCC | H9A PCC | PCC gain | H8 PCC/GCC | H9A PCC/GCC |
| --- | ---: | ---: | ---: | ---: | ---: |
| `int_mix` | 3.426 | 3.846 | +12.26% | 52.25% | 58.16% |
| `const_div` | 0.926 | 0.954 | +3.02% | 68.29% | 69.89% |
| `branch` | 2.505 | 3.082 | +23.03% | 52.63% | 64.75% |
| `switch` | 2.723 | 3.079 | +13.07% | 55.02% | 61.81% |
| `memory` | 4.262 | 5.352 | +25.57% | 41.74% | 52.41% |
| `libc_memory` | 22.127 | 22.252 | +0.56% | 42.45% | 42.99% |
| `calls` | 2.876 | 3.087 | +7.34% | 59.24% | 63.18% |
| `u64` | 1.653 | 1.849 | +11.86% | 57.64% | 63.98% |
| `float` | 2.361 | 2.554 | +8.17% | 48.75% | 52.74% |
| `double` | 1.820 | 2.108 | +15.82% | 68.09% | 79.43% |
| `convert` | 1.641 | 1.780 | +8.47% | 64.30% | 69.75% |

The GCC controls vary only with measurement noise and are byte-identical to
H8.  PCC `memory` gains 25.57% and its PCC/GCC ratio gains 10.67 percentage
points.  No other general PCC kernel regresses.  The seven isolated PCC
Linpack kernels range from -1.02% to +0.93% versus H8, so none changes beyond
the approximately one-percent timing noise of the run.  Ordinary PCC Linpack
averages 3792.478 KFLOPS versus H8's 3772.706, a 0.52% gain.  The current GCC
mean is 4448.748 KFLOPS, putting H9A at 85.25% of GCC versus H8's 85.12%.
This closes the H9A physical and commit gates.

H9A emits no multiply instruction and does not alter instruction scheduling,
so the default `-mfix4300` final-stream workaround and explicit
`-mno-fix4300` opt-out are unchanged.

## Milestone H10: Automatic Pointer TEMP Promotion

H10 lets ordinary non-volatile MIPS automatic pointer variables use PCC's
existing compiler `TEMP` path.  The MIPS `cisreg()` hook now accepts pointer
types; the established front-end qualifier check still excludes volatile
objects, and the existing address-taken handling materializes stack storage
when required.  No optimizer pass, register class, calling-convention rule, or
MIPS-specific allocator path is added.

This removes artificial OREG lifetimes from pointer-walk loops before pass2.
The permanent `misc__pointertemp001` regression covers copy, fill, compare
with an early return, an address-taken local pointer, an indirect function
pointer, and a pointer into a global table.  Host assembly gates require no
frame-relative loads or stores in the three pointer-walk loops, require stack
storage in the address-taken case, and report zero selected allocator spills.

### Hidden GAS `$at` Dependency

The first self-hosted `ccom` test exposed a separate scheduler correctness
defect.  PCC emitted a correct sequence that formed a `keywords` address in
`$at`, consumed it with `addu`, and then loaded the bare symbol `inattr`.
The driver load-delay scheduler moved the `addu` after the load.  GAS expands
a bare-symbol load or store through `$at`, so the linked program instead used
the `inattr` address as the keyword-table base and failed lexical analysis.

The driver now treats bare-symbol integer and FPU loads/stores as implicit
`$at` users when checking scheduling dependencies.  Parenthesized operands
with an explicit base register are unchanged.  The host regression requires
the global-table address consumer to remain before the following bare-symbol
load, and the rebuilt native `ccom` passes both its runtime smoke and the full
native regression.

### H10 Software Gates

The cross regression passes 302/302 runtime cases.  Native Malta64 reports
312 compile passes, 30 expected compile failures, and 302/302 runtime passes,
including `misc__pointertemp001`.  All six fresh PCC-kernel/PCC-rootfs builds
pass the complete `/root/pcc-smoke-all.sh` suite:

| Board | CPU | Endian | Float | Linpack KFLOPS | `libc_memory` Mwork/s | Result |
| --- | --- | --- | --- | ---: | ---: | --- |
| Malta64 | VR4300 | big | hard | 14111.770 / 14293.023 | 442.326 | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | 1128.687 / 1129.645 | 470.591 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | hard | 13996.707 / 13929.411 | 522.693 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | soft | 1203.347 / 1203.787 | 416.663 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | hard | 13538.940 / 13026.335 | 476.836 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | soft | 1202.750 / 1216.471 | 457.653 | `PCC_SMOKE_ALL_RC:0` |

The complete general benchmark rates, in `int_mix`, `const_div`, `branch`,
`switch`, `memory`, `libc_memory`, `calls`, `u64`, `float`, `double`, and
`convert` order, are:

| Profile | General benchmark Mwork/s |
| --- | --- |
| Malta64 hard | 57.419, 52.361, 53.823, 46.973, 94.886, 442.326, 23.420, 50.335, 4.189, 5.202, 5.146 |
| Malta64 soft | 73.884, 51.967, 54.235, 57.608, 128.930, 470.591, 21.518, 43.407, 0.399, 0.276, 0.346 |
| Malta hard | 108.520, 80.173, 52.505, 48.266, 139.643, 522.693, 23.433, 85.557, 4.890, 5.246, 5.541 |
| Malta soft | 107.436, 74.573, 53.219, 47.182, 110.075, 416.663, 25.995, 74.426, 0.439, 0.293, 0.294 |
| MaltaEL hard | 92.125, 73.091, 47.007, 53.611, 126.831, 476.836, 23.044, 56.122, 4.288, 3.225, 5.148 |
| MaltaEL soft | 112.720, 83.194, 53.912, 59.211, 134.519, 457.653, 26.014, 86.302, 0.512, 0.368, 0.352 |

Every profile reports `MIPS_COMPILER_BENCH_SELFTEST 0` and
`PCC_SMOKE_ALL_FAILURES 0`.  Kernel builds retain
`-msoft-float -fomit-frame-pointer`.  QEMU timing is host-load sensitive and
is retained as directional evidence; the commit gate remains the complete
physical N64 correctness and GCC/PCC performance comparison.  H10 does not
change multiply emission or the final-stream erratum repair, so default
`-mfix4300` and explicit `-mno-fix4300` behavior are unchanged.

### H10 N64 Candidate

The retained physical candidate is a clean GCC-kernel/PCC-VR4300-hard-float
a.out-userland build:

```text
/Users/sash/Work/N64/retrobsd-build/n64-h10-pointer-temp-kgcc-upcc-hard-aout/obj/sys/mips/n64/pcc-debug.z64
build stamp: .build-mode.gcc.1.0.0.1
size: 6619136 bytes
sha256: 7731ff096837bbb25f434ecbfa1d2808bfb5ad7a273b9a3b5ed7898fa23b582b
kernel ELF sha256: a1068b0e6aa93dbc8e14a94141b13b2b889ce641b87a4521d8f58e683d1c2cb1
debug runner sha256: 109535aa7cf1565bdaad16daf6da7f3fb3ce14d2bd37a5fbdfc193bdff32881f
```

The kernel and all three GCC controls are byte-identical to H9A.  Pointer TEMP
promotion reduces every PCC comparison binary:

| Binary | H9A section bytes | H10 section bytes | Change |
| --- | ---: | ---: | ---: |
| General compiler benchmark | 45520 | 43840 | -1680 (-3.69%) |
| Linpack | 39024 | 37392 | -1632 (-4.18%) |
| Linpack kernels | 45560 | 43928 | -1632 (-3.58%) |

All six benchmark executables and the debug runner have zero undefined
symbols.  The 6144 KiB rootfs passes all five `fsutil --check` phases with 358
files, 3867 used blocks, and 2252 free blocks.  It stages
`misc__pointertemp001`, and the boot-time C runner compiles and executes it.
Physical N64 correctness and GCC/PCC performance validation remain mandatory
before H10 is committed.

Physical N64 validation passes the complete native runner, both general and
Linpack-kernel self-tests, and every benchmark return code.
`N64_PCC_DEBUG_END` and `N64_PCC_DEBUG_RUNNER_RC` are zero and terminal
`N64_PCC_DEBUG_RC_END` is present.  Because `misc__pointertemp001` is an
extended runner entry, the final zero runner status also confirms its native
compile-and-run gate.

The complete general benchmark comparison is:

| Kernel | H9A PCC | H10 PCC | PCC change | H10 PCC/GCC |
| --- | ---: | ---: | ---: | ---: |
| `int_mix` | 3.846 | 3.846 | +0.00% | 58.15% |
| `const_div` | 0.954 | 0.945 | -0.94% | 69.69% |
| `branch` | 3.082 | 3.105 | +0.75% | 64.82% |
| `switch` | 3.079 | 3.101 | +0.71% | 62.67% |
| `memory` | 5.352 | 5.387 | +0.65% | 52.76% |
| `libc_memory` | 22.252 | 38.912 | +74.87% | 74.65% |
| `calls` | 3.087 | 3.086 | -0.03% | 63.56% |
| `u64` | 1.849 | 1.848 | -0.05% | 64.44% |
| `float` | 2.554 | 2.553 | -0.04% | 52.72% |
| `double` | 2.108 | 2.113 | +0.24% | 79.05% |
| `convert` | 1.780 | 1.696 | -4.72% | 66.88% |

The target result is `libc_memory`: pointer TEMP promotion raises PCC by
74.87% and its GCC ratio by 31.66 percentage points, from 42.99% to 74.65%.
The H9A and H10 compiler-generated assembly for every timed benchmark function
is byte-identical; H10 changes only untimed `main` in this source.  Therefore
the raw `convert` outlier does not correspond to a generated-code change and
is measurement variance, not an H10 regression.

The seven isolated PCC Linpack kernels range from -0.78% to +5.08% against
H9A.  Ordinary PCC Linpack rows are 3806.532 and 3741.679 KFLOPS, averaging
3774.106.  That is 0.48% below H9A's 3792.478 mean and 84.79% of the current
4451.006 GCC mean, within the normal run-to-run range.  H10 therefore closes
its physical and commit gates with a broad libc pointer-loop gain and no
material Linpack regression.

## Milestone H11: Constant Reverse Address Induction

H11 extends the existing target-independent SSA address-induction transform
from `i` and `i+C` to the affine reverse form `C-i`.  Candidates carry a
direction of `+1` or `-1`; the preheader initializes the pointer at the
directed induction value and the latch applies the signed byte step.  The
constant `C` remains an address displacement.  This is an extension of the
existing pass and target profitability hooks, not a MIPS-only pass or a new
register class.

`misc__ssastrength001` covers two reverse accesses sharing a register base, a
dynamic initial index, and a single reverse global double access.  The dynamic
case proves that only one preheader scale remains; no scaled-index operation is
rebuilt in the loop.  Host assembly gates cover generic MIPS3, VR4300, and
MIPS32R2 in hard- and soft-float modes.

In the general benchmark, H11 changes only `bench_float` and `bench_double`.
Their bodies fall from 54 to 51 and 52 to 49 instructions, respectively, and
each loses its remaining scaled-index shift.  Loads, stores, nops, frame size,
and selected spills are unchanged.  Peak GPR pressure rises from 13 to 14;
FPR pressure remains four.  The linked PCC general benchmark shrinks from
43840 to 43808 section bytes, while PCC Linpack and kernel-Linpack remain
37392 and 43928 bytes.

Three alternating Malta64/R4000 H10/H11 runs used one second per general
kernel and one-second Linpack rows.  Mean PCC rates were:

| Metric | H10 | H11 | Change |
| --- | ---: | ---: | ---: |
| `float` Mwork/s | 4.732333 | 4.666667 | -1.39% |
| `double` Mwork/s | 4.428000 | 4.457333 | +0.66% |
| Linpack KFLOPS | 14317.561 | 14325.736 | +0.06% |

The small QEMU `float` movement conflicts with the strictly smaller loop and
is retained as a physical-measurement risk, not treated as a material software
regression.  Linpack is neutral and no new spill or stack traffic appears.

Host gates pass.  Cross runtime passes 302/302.  Native Malta64 reports 312
compile passes, 30 expected compile failures, and 302/302 runtime passes.  All
six fresh full-smoke profiles pass:

| Board | CPU | Endian | Float | Linpack KFLOPS | Result |
| --- | --- | --- | --- | ---: | --- |
| Malta64 | VR4300 | big | hard | 14379.284 / 14353.997 | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | 1047.771 / 1072.815 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | hard | 13832.838 / 13945.319 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | soft | 1199.964 / 1182.511 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | hard | 13825.435 / 13857.820 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | soft | 1224.618 / 1219.068 | `PCC_SMOKE_ALL_RC:0` |

Every profile also reports `MIPS_COMPILER_BENCH_SELFTEST 0`,
`CCOM_STRESS_DONE:100`, and `PCC_SMOKE_ALL_FAILURES 0`.

The retained physical candidate is:

```text
/Users/sash/Work/N64/retrobsd-build/n64-h11-reverse-induction-kgcc-upcc-hard-aout/obj/sys/mips/n64/pcc-debug.z64
build stamp: .build-mode.gcc.1.0.0.1
size: 6619136 bytes
sha256: 0939b1961dfa2d678d0d1f79d6fab1778c010ea7cadc500203bf59b21d617909
kernel ELF sha256: a1068b0e6aa93dbc8e14a94141b13b2b889ce641b87a4521d8f58e683d1c2cb1
debug runner sha256: 109535aa7cf1565bdaad16daf6da7f3fb3ce14d2bd37a5fbdfc193bdff32881f
```

It uses a GCC kernel and PCC VR4300 hard-float a.out userland.  The kernel,
runner, and all three GCC controls are byte-identical to H10.  All six
benchmark executables and the runner have zero undefined symbols.  The 6144
KiB debug rootfs passes all five `fsutil` phases with 358 files, 3868 used
blocks, and 2251 free blocks.  Physical N64 correctness and performance
validation remain mandatory before commit.

H11 does not emit multiply instructions or alter final scheduling.  The
default `-mfix4300` final-stream erratum repair and explicit `-mno-fix4300`
opt-out remain unchanged.

Physical N64 validation passes the expanded native `misc__ssastrength001`
compile-and-run gate, both general self-tests, both Linpack-kernel self-tests,
every benchmark return code, zero-valued `N64_PCC_DEBUG_END` and
`N64_PCC_DEBUG_RUNNER_RC`, and the terminal `N64_PCC_DEBUG_RC_END` marker.
The H10/H11 general comparison is:

| Kernel | H10 PCC | H11 PCC | PCC change | H11 PCC/GCC |
| --- | ---: | ---: | ---: | ---: |
| `int_mix` | 3.846 | 3.846 | +0.00% | 58.15% |
| `const_div` | 0.945 | 0.954 | +0.95% | 69.89% |
| `branch` | 3.105 | 3.081 | -0.77% | 64.73% |
| `switch` | 3.101 | 3.101 | +0.00% | 62.26% |
| `memory` | 5.387 | 5.349 | -0.71% | 52.38% |
| `libc_memory` | 38.912 | 38.916 | +0.01% | 75.17% |
| `calls` | 3.086 | 3.087 | +0.03% | 63.18% |
| `u64` | 1.848 | 1.848 | +0.00% | 63.94% |
| `float` | 2.553 | 2.956 | +15.79% | 61.05% |
| `double` | 2.113 | 2.382 | +12.73% | 89.75% |
| `convert` | 1.696 | 1.680 | -0.94% | 66.25% |

The targeted reverse-address loops improve materially on VR4300: `float`
gains 15.79% and `double` gains 12.73%, raising their current GCC ratios by
8.33 and 10.70 percentage points relative to H10.  Untouched kernels remain
within one percent of H10.

Ordinary PCC Linpack rows are 3742.671 and 3803.952 KFLOPS, averaging
3773.312.  This is 0.02% below H10's 3774.106 mean and 85.59% of the current
4408.832 GCC mean.  The seven PCC kernel self-tests pass and report 3.726,
4.503, 4.280, 5.703, 4.846, 6.529, and 5.371 Melem/s in `daxpy_r`,
`daxpy_ur`, `ddot_r`, `ddot_ur`, `dscal_r`, `dscal_ur`, and `idamax` order.
H11 therefore closes its physical and commit gates with the intended general
FP-loop gain and no material Linpack or unrelated-code regression.
