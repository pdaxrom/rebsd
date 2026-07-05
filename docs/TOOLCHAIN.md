# ReBSD Toolchain Policy

The kernel and userland build with the existing GCC-based toolchain by default.

PortableCC/pcc is available as a supported opt-in userland compiler for the
MIPS rootfs and N64 userland.  It has passed the Malta and Malta64/R4000 QEMU
hard-float and soft-float rootfs gates, plus the earlier normal hard-float N64
hardware C userland gate.  The updated soft-float PCC path still needs the real
N64 hardware pass before becoming the N64 hardware baseline.

Current policy:

- Kernel build: GCC by default.  PCC kernel builds are experimental only and
  currently blocked by a documented PCC frontend bug.
- Userland build: GCC by default.
- Supported userland compiler selectors: `gcc` and `pcc`.
- Current PCC target endianness: build-time big-endian `mips-rebsd`.
- PCC CPU selectors: `vr4300` and `mips32r2`, with CPU-specific instruction
  mode and C ABI alignment.
- PCC float ABI selectors: `hard` and `soft`, with ABI-specific build stamps.
- Native `/usr/bin/cc` and `/usr/bin/pcc`: imported PCC in the rootfs.
- C++/`p++`: deferred to future work and not installed by default.

The userland compiler selector is intentionally separate from the kernel
compiler.  There are two make variable names because the tree has two entry
points into the same userland/rootfs choice:

- `N64_USERLAND_COMPILER` is the direct N64 userland selector.
- `MIPS_ROOTFS_COMPILER` is the Malta/Malta64 rootfs selector.
- `MIPS_ROOTFS_CPU` selects the rootfs CPU ABI.  Supported values are
  `vr4300` and `mips32r2`.
- `N64_USERLAND_FLOAT` and `MIPS_ROOTFS_FLOAT` select the userland/rootfs float
  ABI when PCC is used.  Supported values are `hard` and `soft`.
- `MIPS_ROOTFS_ENDIAN` selects the future rootfs endian ABI.  The current
  supported value is `big`; `little` is reserved for the later mipsel Malta
  port.

They are aliases for the same compiler mode.  If only one is set, the other
entry point inherits it.  If both are set to different values, the build fails
early.  Both variables default to `gcc`; `pcc` selects the imported PCC
frontend for userland while keeping the in-tree ReBSD a.out `as`, `ld`, `ar`,
and `ranlib` as the target binary tools.

Common build forms:

```sh
make -C sys/mips/malta MIPS_ROOTFS_COMPILER=pcc native-pcc-smoke-runtime
make -C sys/mips/malta MIPS_ROOTFS_COMPILER=pcc native-pcc-regress-runtime
make -C sys/mips/malta MIPS_ROOTFS_COMPILER=pcc MIPS_ROOTFS_CPU=mips32r2 MIPS_ROOTFS_FLOAT=soft linpack-smoke-runtime
make -C sys/mips/malta64 MIPS_ROOTFS_COMPILER=pcc native-pcc-regress-runtime
make -C sys/mips/malta64 MIPS_ROOTFS_COMPILER=pcc MIPS_ROOTFS_FLOAT=soft native-pcc-regress-runtime
make -C sys/mips/n64 N64_USERLAND_COMPILER=pcc kernel.z64 preflight.z64
```

The QEMU PCC smoke matrix verified on 2026-07-05 is:

```text
malta64 vr4300  hard  /root/pcc-smoke-all.sh  PCC_SMOKE_ALL_RC:0
malta64 vr4300  soft  /root/pcc-smoke-all.sh  PCC_SMOKE_ALL_RC:0
malta   mips32r2 hard  /root/pcc-smoke-all.sh  PCC_SMOKE_ALL_RC:0
malta   mips32r2 soft  /root/pcc-smoke-all.sh  PCC_SMOKE_ALL_RC:0
```

The same runs included `linpack-pcc`; representative results were about
11.3 MFLOPS for VR4300 hard-float, 10.5-11.0 MFLOPS for mips32r2 hard-float,
and 0.7-0.8 MFLOPS for soft-float.

`pcc` mode controls how the target userland and libraries are built.  It does
not switch the kernel, N64 stage0, or target a.out binary tools away from the
existing flow.  PCC runtime builds do not use GCC wrappers: the standalone
cross SDK first builds the a.out tools and cross PCC without target runtime
libraries, then uses that cross PCC to build `crt0.o`, libc, libm, and
`libpcc.a`, then builds the target rootfs and native PCC against those
PCC-built libraries.  Kernel PCC experiments must be requested explicitly with
`MIPS_KERNEL_COMPILER=pcc` or `N64_KERNEL_COMPILER=pcc`; they are expected to
fail until the documented `rdwri()` frontend bug is fixed.

Standalone cross SDK builds use common MIPS selectors:

```sh
sys/mips/tools/build-cross-pcc-sdk.sh --cpu vr4300 --float soft --endian big --prefix /path/cross-pcc
make -C sys/mips -f sdk.mk cross-pcc-sdk CPU=mips32r2 FLOAT=hard ENDIAN=big MIPS_SDK_PREFIX=/path/cross-pcc
make -C sys/mips -f sdk.mk cross-pcc-sdk-tools CPU=mips32r2 FLOAT=soft ENDIAN=big MIPS_SDK_PREFIX=/path/cross-pcc
make -C sys/mips -f sdk.mk cross-pcc-sdk-runtime CPU=mips32r2 FLOAT=soft ENDIAN=big MIPS_SDK_PREFIX=/path/cross-pcc
```

The SDK layout is intended to work without wrappers:

```text
cross-pcc/bin/mips-rebsd-pcc
cross-pcc/bin/mips-rebsd-cc
cross-pcc/bin/mips-rebsd-as
cross-pcc/bin/mips-rebsd-ld
cross-pcc/bin/mips-rebsd-aout
cross-pcc/mips-rebsd/lib/crt0.o
cross-pcc/mips-rebsd/lib/libc.a
cross-pcc/mips-rebsd/lib/libm.a
cross-pcc/mips-rebsd/lib/libpcc.a
cross-pcc/mips-rebsd/lib/softfloat/libpcc.a
```

For rootfs builds, `MIPS_PCC_PROVIDER=cross` uses
`MIPS_PCC_HOST_PREFIX=/path/cross-pcc`.  `MIPS_PCC_PROVIDER=system` may be used
on a ReBSD host with a compatible CPU/toolchain, so the build does not require
building a host PCC unconditionally.

For the current big-endian PCC target, CPU/ISA selection is a runtime compiler
mode but endian selection is not.  A future little-endian Malta port should use
a separate `mipsel-rebsd` compiler target so headers, predefined macros, and
libraries stay internally consistent.
