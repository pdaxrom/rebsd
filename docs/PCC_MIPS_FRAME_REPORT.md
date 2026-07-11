# PCC MIPS Frame Lowering Report

## Milestone D1: Fixed Register-Call Home Area

Implementation commit `34c0b9d8` removes the blanket frame-pointer fallback
for a bounded class of MIPS non-leaf functions compiled with
`-fomit-frame-pointer`.  If every visible call uses only register arguments,
the backend reserves one fixed 16-byte o32 argument home area below all local
and callee-save slots.  Calls then reuse that area instead of decrementing and
restoring `$sp` around every call.

The fixed area is added after local, spill, and callee-save offsets have been
calculated and aligned.  Therefore offsets `0..15($sp)` belong only to the
callee, while the caller's first private slot is at offset 16 or greater.
Frame references are rewritten through the existing omit-FP path using the
larger fixed frame adjustment.

The optimization is disabled for `FUNARG`, `STARG`, `alloca`, explicit `$fp`
assignment, and raw frame-address expressions.  Stack-passed arguments,
aggregate arguments, varargs, and address-taken locals therefore retain the
previous stable `$fp` and dynamic call lowering.  Compilation without
`-fomit-frame-pointer` is byte-identical to Milestone C4.

The call table uses one conditional emitter in place of 12 duplicated literal
home-area adjustments.  In the fixed case the `jal` delay slot is initially a
`nop` and the post-call `$sp` restoration disappears.  This removes one
executed instruction per call; the existing final delay-slot pass remains free
to fill the slot when it has a proven candidate.

## Permanent Probes

The host smoke compiles and assembles the frame probes for both VR4300 and
MIPS32R2.  It requires a register-only non-leaf call to omit `$fp`, avoid a
dynamic call-area adjustment, and keep every callee-save slot above offset 15.
Separate stack-argument, address-taken-local, `alloca`, and varargs probes must
retain `$fp` and the old dynamic lowering.

## Static Results

Normal Linpack output is unchanged:

```text
VR4300:   2471 instructions, 153 nops, 643 loads, 244 stores
sha256:   168a5871b79216b204259821397ba7992039e544288d1de7b60a9b3a5b184d2e
MIPS32R2: 2545 instructions, 108 nops, 772 loads, 293 stores
sha256:   b7741b749544cbe2a8601688b581564bc03e2e6dad5f37ecfe7217e120bc9a3f
```

With `-fomit-frame-pointer`, `main` and `idamax` become fixed-area functions.
Functions with stack arguments or raw local addresses remain unchanged:

```text
VR4300 before:   2453 instructions, 153 nops, 636 loads, 237 stores
VR4300 after:    2425 instructions, 175 nops, 634 loads, 235 stores
VR4300 sha256:   e6b0382b89f4a344606086a0db27556015f1507d4b481f6af3b857588cdd387d

MIPS32R2 before: 2527 instructions, 108 nops, 765 loads, 286 stores
MIPS32R2 after:  2493 instructions, 124 nops, 763 loads, 284 stores
MIPS32R2 sha256: a59b0200b75daaff8a5e3e189114011fdb0f9d2b33d1d4b8ca405871ff240e69
```

The `nop` increase replaces useful dynamic `$sp` adjustments in call delay
slots, while the matching post-call restorations disappear.  Non-`nop`
instructions fall by 50 on each CPU.  Unoptimized `optim003.c` remains
byte-identical with SHA-256
`bff0d1f27f8ab0b61e07de14db8313979e788d03be521282055fb327f6fb08ef`.

## Regression Gates

Cross regression compiled 329 of 332 tests with only the three documented
expected failures and passed 292/292 runtime candidates.  Native PCC compiled
302 cases, observed 30 expected compile failures, and passed 292/292 runtime
cases.  Unexpected counts were zero.

| Board | CPU | Endian | Float | PCC Linpack KFLOPS | Result |
| --- | --- | --- | --- | --- | --- |
| Malta64 | VR4300 | big | hard | 13902 / 13727 | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | 983 / 997 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | hard | 13420 / 13617 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | soft | 1136 / 1146 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | hard | 13637 / 13672 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | soft | 1143 / 1145 | `PCC_SMOKE_ALL_RC:0` |

All kernels and root filesystems were built with PCC.  Every profile has
exactly one `PCC_SMOKE_ALL_OK`, `PCC_SMOKE_ALL_FAILURES 0`, and
`PCC_SMOKE_ALL_RC:0`.  Kernel builds use `-msoft-float` and
`-fomit-frame-pointer`; soft userland profiles also use `-msoft-float`.  Logs
are `/private/tmp/pcc-d1-{malta64,malta,maltael}-{hard,soft}.log`.

## N64 Artifact

```text
sys/mips/n64/builds/20260711-milestone-d1-fixed-call-area/pcc-debug.z64
implementation commit: 34c0b9d8
size: 6619136 bytes
sha256: 00fb26fc83d7eb892ec64c19257980fe7245054df3240d14dc53c34f19c266ed
cross pcc sha256: 0ee591be1f4c732d7f2c2858ee6facbe343ce5212a8c08f6e429992dbb76da4b
cross ccom sha256: 1b92c5af7cc12a22b15327c67066c8bd49f93462567cab74b14689053d994e55
native ccom sha256: eadf1d246cb52d918b4f748496c6ee8055cafb055e95f658e827e56f8f53d405
```

The hard-float a.out image contains a PCC-built kernel and userland.  The
kernel uses `-fomit-frame-pointer`; the userland uses hard float; the compiler
uses default `-mfix4300`.  Real N64 validation passed on 2026-07-11 with:

```text
N64_PCC_DEBUG_END 0
N64_PCC_DEBUG_RUNNER_RC 0
N64_PCC_DEBUG_RC_END
```

The supplied final marker did not include a numeric value, so none is inferred
here.

## Milestone D2: Fixed Stack-Argument Area

Implementation commit `194d264e` extends the D1 fixed outgoing area to
ordinary calls with stack-passed scalar arguments.  Pass2 scans the already
ABI-lowered `FUNARG` nodes, records the largest call area needed by the
function, and assigns o32 stack slots in ABI order.  The fixed frame reserves
the 16-byte register home area plus stack-argument bytes and any call-alignment
padding.  Argument five starts at `16($sp)`; following slots advance from that
offset independently of pre-call padding.

Eligible `-fomit-frame-pointer` functions store stack arguments directly into
the fixed area.  They no longer decrement and restore `$sp` around each call.
The old dynamic push emitters remain unchanged for non-omit-FP functions and
fallback cases.  `STARG`, raw frame addresses, `alloca`, explicit `$fp`, and
variadic callees still require a stable frame pointer.  Aggregate calls remain
on that path because pass1 materializes a frame temporary.  Nested scalar calls
are eligible because pass1 computes their values before pass2 emits the outer
argument list.

The implementation adds 84 net lines to the MIPS backend: one bounded pass2
scan and three small emitters shared by the call table.  It does not add a
second call-lowering pipeline or inspect generated assembly text.

## D2 Permanent Probes

The VR4300 and MIPS32R2 host smoke now compiles and assembles value-sensitive
probes for register-only calls, one and two stack arguments, aligned 64-bit
stack slots, variadic callers, nested calls, aggregate arguments,
address-taken locals, `alloca`, and variadic callees.  The probes require
scalar slots at `16`, `20`, and `24($sp)` and require every unsupported case to
retain the dynamic `$fp` path.

These probes caught two ABI mistakes during development.  Alignment padding
was initially added to argument offsets instead of only to the reserved area,
and a later version assigned offsets in emission order rather than o32 ABI
order.  The first failed before smoke execution; the second reached the native
compiler before failing.  Both were corrected before commit, and the final
six-profile gates below exercise the fixed implementation.

## D2 Static Results

Normal Linpack remains byte-identical to D1:

```text
VR4300 sha256:   168a5871b79216b204259821397ba7992039e544288d1de7b60a9b3a5b184d2e
MIPS32R2 sha256: b7741b749544cbe2a8601688b581564bc03e2e6dad5f37ecfe7217e120bc9a3f
```

With `-fomit-frame-pointer`, D2 produces:

```text
VR4300 D1: 2425 instructions, 2250 non-nop, 175 nops, 634 loads, 235 stores
VR4300 D2: 2367 instructions, 2182 non-nop, 185 nops, 632 loads, 233 stores
sha256:    746504885469ea7f3e52f3c3e51addc8ac879f61b78f9be498c46f93d5de8577

MIPS32R2 D1: 2493 instructions, 2369 non-nop, 124 nops, 763 loads, 284 stores
MIPS32R2 D2: 2435 instructions, 2301 non-nop, 134 nops, 761 loads, 282 stores
sha256:      a0a0ddec8217b66cd35077006d08930d650081c910ae1667734eff6687fff9ea
```

Both CPUs remove 68 executed non-`nop` instructions, two loads, and two stores
from the omit-FP benchmark.  The remaining `$fp` functions are `linpack` and
`second`; D1 also required `$fp` in `dgefa` and `dgesl`.  Static inspection of
the real `rdwri` kernel call on Malta64 and Malta confirms arguments five,
six, and seven at `16`, `20`, and `24($sp)`.

## D2 Regression Gates

Cross regression compiled 329 of 332 tests with the three documented failures
and passed 292/292 runtime candidates.  Native PCC compiled 302 cases, saw 30
expected compile failures, and passed 292/292 runtime cases.  Unexpected
counts were zero.  Non-omit-FP output remains byte-identical.

| Board | CPU | Endian | Float | PCC Linpack KFLOPS | Result |
| --- | --- | --- | --- | --- | --- |
| Malta64 | VR4300 | big | hard | 13079.567 / 12857.509 | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | 975.160 / 978.446 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | hard | 13168.642 / 13136.922 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | soft | 1149.115 / 1129.113 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | hard | 12542.934 / 12767.421 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | soft | 1156.488 / 1157.711 | `PCC_SMOKE_ALL_RC:0` |

Every final post-fix PCC-kernel/PCC-rootfs profile has exactly one
`PCC_SMOKE_ALL_OK`, `PCC_SMOKE_ALL_FAILURES 0`, and `PCC_SMOKE_ALL_RC:0`.
Kernels use `-msoft-float -fomit-frame-pointer`; userland float mode is varied.
Logs are `/private/tmp/pcc-d2-{malta64,malta,maltael}-{hard,soft}.log`.

## D2 N64 Artifact

```text
sys/mips/n64/builds/20260711-milestone-d2-fixed-stack-args/pcc-debug.z64
implementation commit: 194d264e
size: 6717440 bytes
sha256: 76aeb3251bf15e4b4ba7f5ad4d9d3f17a2b00c16e7d09dceba32ccf878198633
cross pcc sha256: be0639b2e406dff1c9d02b310c720401bf6f6aee035748820abb0d79f99bd115
cross ccom sha256: 98023a77416531ccbd1056fc3b9c6484d3e0fa5b36a5b14713dd3f1d0211befa
native ccom sha256: 6e873b9fcb786e78752da7b71d16a84ec0df0c6c236c62c89b5bb591f50747cc
```

The image contains a PCC-built `-msoft-float -fomit-frame-pointer` kernel and
PCC-built hard-float a.out userland.  VR4300 multiplication erratum handling
uses the default `-mfix4300`.  Real N64 validation is pending.

## Next Step

Run the D2 artifact on real N64 and require `N64_PCC_DEBUG_END 0`,
`N64_PCC_DEBUG_RUNNER_RC 0`, and `N64_PCC_DEBUG_RC_END`.  Do not begin the next
risky backend substep until that gate passes.  Afterward, close the current
Milestone D frame work and profile VR4300 hard-float FPU padding before choosing
the next compact scheduler change.
