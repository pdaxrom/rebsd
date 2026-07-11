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

Real N64 validation is required because QEMU does not establish the physical
VR4300 MCI timing.  Hardware status is pending.

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
