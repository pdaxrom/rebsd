# ReBSD Toolchain Policy

The kernel and userland build with the existing GCC-based toolchain by default.

PortableCC/pcc is available as a supported opt-in userland compiler for the
MIPS rootfs and N64 userland.  It has passed the Malta, Malta64/R4000, and
real N64 C userland gates, but it is still not the default compiler.

Current policy:

- Kernel build: GCC by default.  PCC kernel builds are experimental only and
  currently blocked by a documented PCC frontend bug.
- Userland build: GCC by default.
- Supported userland compiler selectors: `gcc` and `pcc`.
- Current PCC target endianness: build-time big-endian `mips-rebsd`.
- PCC CPU selectors: `vr4300` and `mips32r2`, with CPU-specific instruction
  mode and C ABI alignment.
- Native `/usr/bin/cc` and `/usr/bin/pcc`: imported PCC in the rootfs.
- C++/`p++`: deferred to future work and not installed by default.

The userland compiler selector is intentionally separate from the kernel
compiler.  There are two make variable names because the tree has two entry
points into the same userland/rootfs choice:

- `N64_USERLAND_COMPILER` is the direct N64 userland selector.
- `MIPS_ROOTFS_COMPILER` is the Malta/Malta64 rootfs selector.

They are aliases for the same compiler mode.  If only one is set, the other
entry point inherits it.  If both are set to different values, the build fails
early.  Both variables default to `gcc`; `pcc` selects the imported PCC
frontend for userland while keeping the in-tree ReBSD a.out `as`, `ld`, `ar`,
and `ranlib` as the target binary tools.

Common build forms:

```sh
make -C sys/mips/malta MIPS_ROOTFS_COMPILER=pcc native-pcc-smoke-runtime
make -C sys/mips/malta MIPS_ROOTFS_COMPILER=pcc native-pcc-regress-runtime
make -C sys/mips/malta64 MIPS_ROOTFS_COMPILER=pcc native-pcc-regress-runtime
make -C sys/mips/n64 N64_USERLAND_COMPILER=pcc kernel.z64 preflight.z64
```

`pcc` mode controls how the target userland and libraries are built.  It does
not switch the kernel, N64 stage0, or host-side bootstrap tools away from the
existing GCC-based flow.  Kernel PCC experiments must be requested explicitly
with `MIPS_KERNEL_COMPILER=pcc` or `N64_KERNEL_COMPILER=pcc`; they are expected
to fail until the documented `rdwri()` frontend bug is fixed.

For the current big-endian PCC target, CPU/ISA selection is a runtime compiler
mode but endian selection is not.  A future little-endian Malta port should use
a separate `mipsel-rebsd` compiler target so headers, predefined macros, and
libraries stay internally consistent.
