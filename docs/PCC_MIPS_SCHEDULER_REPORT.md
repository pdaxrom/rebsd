# PCC MIPS Scheduler Report

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

## Next Step

Keep Milestone C open.  The next scheduler substep should use the same small
window to separate dependent FP compute operations from FPU loads when one
parsed independent register instruction is available.  Do not add memory
movement until aliasing is modelled explicitly, and do not move `mul.s` or
`mul.d` into branch delay slots under `-mfix4300`.
