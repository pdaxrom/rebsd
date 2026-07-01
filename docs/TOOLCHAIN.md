# ReBSD Toolchain Policy

The kernel and userland build with the existing GCC-based toolchain by default.

PortableCC/pcc is being adapted as an alternate system compiler. Until it passes
the full QEMU Malta regression suite, pcc must not become the default compiler
for kernel or userland builds.

Current policy:

- Kernel build: GCC only.
- Userland build: GCC by default.
- Native `/usr/bin/cc` and `/usr/bin/pcc`: may be staged for tests.
- pcc transition: opt-in only until Malta tests pass.

The userland compiler selector is intentionally separate from the kernel
compiler. `N64_USERLAND_COMPILER` defaults to `gcc`; `pcc` is reserved and
currently rejected at makefile parse time. A future `N64_USERLAND_COMPILER=pcc`
mode may build the userland with pcc after the pcc port passes the required
smoke, ABI, archive, ranlib, and runtime tests under QEMU Malta.
