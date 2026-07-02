# ReBSD Toolchain Policy

The kernel and userland build with the existing GCC-based toolchain by default.

PortableCC/pcc is available as an alternate opt-in userland compiler. It must
not become the default compiler until the full PCC-built userland passes the
required QEMU Malta gates.

Current policy:

- Kernel build: GCC only.
- Userland build: GCC by default.
- Native `/usr/bin/cc` and `/usr/bin/pcc`: may be staged for tests.
- pcc transition: opt-in only until Malta tests pass.

The userland compiler selector is intentionally separate from the kernel
compiler. `N64_USERLAND_COMPILER` defaults to `gcc`; `pcc` selects the imported
PCC frontend for userland while keeping the in-tree ReBSD a.out `as`, `ld`,
`ar`, and `ranlib` as the target binary tools. The kernel, stage0, and default
userland path remain on the existing GCC flow.
