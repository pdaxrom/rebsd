# PCC MIPS Frame Lowering Report

## 2026-07-11 Clean-Build Audit

The original C2 through D1 N64 hardware artifacts are not valid evidence of a
PCC-built kernel.  The generated N64 build-mode stamp did not include
`N64_KERNEL_COMPILER`, so switching from GCC to PCC could reuse GCC kernel
objects while rebuilding `vers.o` with a `with pcc` version string.  The D1
`main` prologue extracted from the archived ROM matches a fresh GCC `-O2`
compile, including its 64-byte frame, ten saved registers, and call delay slot.
Those hardware runs still validate their PCC userland and debug runner, but
not the kernel compiler.

Commit `776e41af` adds the kernel compiler to the build stamp.  Every N64 PCC
kernel claim after this audit requires a preceding clean build or a
`.build-mode.pcc.*` stamp plus static inspection of PCC-generated kernel
assembly.

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

The image booted and its PCC hard-float a.out userland passed on real N64 on
2026-07-11 with:

```text
N64_PCC_DEBUG_END 0
N64_PCC_DEBUG_RUNNER_RC 0
N64_PCC_DEBUG_RC_END
```

The supplied final marker did not include a numeric value, so none is inferred
here.  The later clean-build audit found stale GCC kernel objects in this
artifact; this result must not be cited as PCC-kernel hardware validation.

## Milestone D2: Fixed Stack-Argument Area

Implementation commit `194d264e`, corrected by `5aa462ed`, extends the D1
fixed outgoing area to
ordinary calls with stack-passed scalar arguments.  Pass2 scans the already
ABI-lowered `FUNARG` nodes, records the largest call area needed by the
function, while `lastcall` initializes an emitter offset that follows the
existing dynamic-push order.  The fixed frame reserves
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

No ABI offset is stored in `NODE.n_qual`: that field carries C type qualifiers
and must remain semantic input.  The compact implementation uses one bounded
pass2 size scan and three small emitters shared by the call table.  It does not
add a second call-lowering pipeline or inspect generated assembly text.

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
| Malta64 | VR4300 | big | hard | 13029.463 / 13462.220 | `PCC_SMOKE_ALL_RC:0` |
| Malta64 | VR4300 | big | soft | 934.180 / 933.308 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | hard | 13002.969 / 13008.236 | `PCC_SMOKE_ALL_RC:0` |
| Malta | MIPS32R2 | big | soft | 1132.767 / 1127.656 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | hard | 13217.939 / 13062.511 | `PCC_SMOKE_ALL_RC:0` |
| MaltaEL | MIPS32R2 | little | soft | 1145.007 / 1148.982 | `PCC_SMOKE_ALL_RC:0` |

Every final post-fix PCC-kernel/PCC-rootfs profile has exactly one
`PCC_SMOKE_ALL_OK`, `PCC_SMOKE_ALL_FAILURES 0`, and `PCC_SMOKE_ALL_RC:0`.
Kernels use `-msoft-float -fomit-frame-pointer`; userland float mode is varied.
Logs are `/private/tmp/pcc-d2fix-{malta64,malta,maltael}-{hard,soft}.log`.

## D2 N64 Artifact

The original D2 artifact
`sys/mips/n64/builds/20260711-milestone-d2-fixed-stack-args/pcc-debug.z64`
failed on real N64 after stage0 printed `jump kernel entry=0x80001000`; no kernel
banner followed.  Unlike D1, it was a clean PCC kernel.  Static inspection
showed that the ELF linker selected the later weak `console_null` definition
of `n64_console_putc` instead of the earlier N64cart UART definition.

ELF symbol collection replaced an existing weak definition with every later
weak definition.  Commit `776e41af` preserves the first weak definition and
adds a permanent two-definition link probe for both endian modes.  The fixed
kernel's `n64_console_putc` calls `n64cart_uart_putc` and accesses the expected
`0xbfd01000` N64cart register window.

```text
sys/mips/n64/builds/20260711-milestone-d2-clean-pcc-fix/pcc-debug.z64
implementation commits: 194d264e, 5aa462ed, 776e41af
size: 6717440 bytes
sha256: 97939aea1d4c3048472974b001fefa11fb3872b3538264db545c2d4d743f17a9
kernel ELF sha256: 0436af61a37f585814cb65ccbe3410f65d264f4ff756955d676b3909654cf789
cross pcc sha256: 319710e219a4100eed644f0f545c58e7a9bc3f755d4a40cd5963e264ba983751
cross ccom sha256: a99c9f907511be8c3302af538a5151cc41be2544cd1f644706f3a928fe0c4f5b
native ccom sha256: e1b5143cb17a0f6ffee4af95ad71b26e960382158ae9d95da14674ffef9d6499
```

This image was produced after `make clean` and removal of the N64 PCC temporary
build/install directories.  It contains a PCC-built
`-msoft-float -fomit-frame-pointer` kernel and PCC-built hard-float a.out
userland.  VR4300 multiplication erratum handling uses the default
`-mfix4300`.  Cross and native regressions pass 292/292, and all six full QEMU
profiles pass.  Real N64 validation of this corrected clean-PCC image passed
on 2026-07-11 with:

```text
N64_PCC_DEBUG_END 0
N64_PCC_DEBUG_RUNNER_RC 0
N64_PCC_DEBUG_RC_END
```

## Next Step

D2 and the current Milestone D frame work have passed the corrected clean-PCC
real-N64 gate.  Profile VR4300 hard-float FPU padding before choosing the next
compact scheduler change; keep `-mfix4300` enabled by default and preserve the
existing hard/soft-float and big/little-endian gates.
