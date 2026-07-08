# VR4300 Errata Workarounds

This document records the VR4300/R4300i errata policy used by the ReBSD PCC
MIPS target.

## Back-to-back multiply bug

The Nintendo 64 CPU is a NEC VR4300, also known as MIPS R4300i.  Known
affected revisions can produce an incorrect result for the second multiply in
this executed instruction sequence:

- the first instruction is `mul.s` or `mul.d`;
- one source operand of that floating-point multiply is sNaN, zero, or
  infinity;
- the next executed multiply is one of `mul.s`, `mul.d`, `mult`, `multu`,
  `dmult`, or `dmultu`.

The workaround is to ensure that at least one real instruction is executed
between the floating-point multiply and the following multiply.  A `nop` is a
valid separator.

## Compiler Policy

The public options are:

- `-mfix4300`: enable the VR4300 multiply erratum workaround.
- `-mno-fix4300`: disable the workaround.

For ReBSD/MIPS PCC, `-march=vr4300` enables the workaround by default unless a
user explicitly selected `-mno-fix4300`.  `-march=mips32r2` disables it by
default unless a user explicitly selected `-mfix4300`; the repair pass still
only runs when the selected CPU is VR4300.

The workaround is intentionally narrow.  It does not enable unrelated
R4000/R4400, VR41xx, R10000, RM7000, R5900, 24K, SB1, Loongson, or Cavium
MIPS errata workarounds.

## Implementation

The PCC driver runs a MIPS assembly postprocess before emitting `-S` output or
invoking the assembler.  When the VR4300 fix is active, the postprocess inserts
a `nop` before any multiply instruction that immediately follows `mul.s` or
`mul.d` in the executed linear stream.

The postprocess keeps the compiler delay-slot filler from moving `mul.s` or
`mul.d` into branch or jump delay slots when the fix is active.  For handwritten
assembly, the driver repairs simple adjacent multiply cases on a temporary
copy of the source and warns when `mul.s` or `mul.d` appears in a delay slot,
because full control-flow repair of handwritten assembly is intentionally out
of scope for the C compiler driver.

## Testing

QEMU is useful for proving that the generated code is architecturally valid and
that normal smoke tests still pass.  It should not be used as proof that this
revision-specific VR4300 pipeline bug is absent, because QEMU normally does
not model this erratum.  The workaround is tested by inspecting emitted
assembly, and real VR4300 hardware is the final authority for the bug itself.
