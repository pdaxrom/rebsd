# PortableCC Porting Notes

Initial upstream layout notes after importing PortableCC:

- MIPS backend already exists in `src/dev/pcc/pcc/arch/mips`.
- OS-specific compiler configuration lives under `src/dev/pcc/pcc/os/<os>`.
- Driver defaults and predefined macros are centralized in
  `src/dev/pcc/pcc/cc/driver/platform.c` and `os/<os>/ccconfig.h`.
- `configure.ac` maps target triples to `targmach`, `targos`, `abi`, and
  endianness. ReBSD should add `rebsd*` OS handling there, with
  `mipseb`/`mips` mapping to `targmach=mips` and big-endian output.
- Existing `os/none` is too freestanding for native `/usr/bin/cc`; existing
  `os/bsd` is close historically but only covers older non-MIPS assumptions.

Likely first local glue files:

- `src/dev/pcc/pcc/os/rebsd/ccconfig.h`
- `src/dev/pcc/pcc/configure.ac`
- generated `src/dev/pcc/pcc/configure`, unless the build flow gains an
  explicit autoreconf step
- `src/dev/pcc/pcc/cc/driver/platform.c`, only if `ccconfig.h` is not enough
  to express ReBSD include paths, library paths, startup files, and macros

Initial ReBSD target behavior should be conservative:

- target triples: `mips-rebsd`, `mips-unknown-rebsd`
- compatibility aliases later: `mips-retrobsd`, `mips-unknown-retrobsd`
- target machine: big-endian MIPS o32
- object/link path: existing ReBSD a.out tools
- default startup: `/usr/lib/crt0.o`
- default libraries: `/usr/lib/libc.a`
- no shared libraries, PIC, TLS, C++, or kernel build switch in the first pcc
  milestone
