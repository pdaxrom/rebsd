# ReBSD Toolchain Policy

The kernel and userland build with the existing GCC-based toolchain by default.

PortableCC/pcc is available as a supported opt-in userland compiler for the
MIPS rootfs and N64 userland.  It has passed the Malta, Malta64/R4000, and
real N64 C userland gates, but it is still not the default compiler.

Current policy:

- Kernel build: GCC only.
- Userland build: GCC by default.
- Supported userland compiler selectors: `gcc` and `pcc`.
- Native `/usr/bin/cc` and `/usr/bin/pcc`: imported PCC in the rootfs.
- C++/`p++`: not installed by default; it needs a separate runtime gate.

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
existing GCC-based flow.
