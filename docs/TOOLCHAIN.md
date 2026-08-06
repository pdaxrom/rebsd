# ReBSD Toolchain Policy

The kernel and userland build with the existing GCC-based toolchain by default.

PortableCC/pcc is available as a supported opt-in compiler for i686 and MIPS
userland.  PCC kernel builds are also available as explicit MIPS gates.  PCC
has passed the i686 GCC-kernel/PCC-userland QEMU self-build gate and the Malta,
Malta64/R4000, and MaltaEL QEMU hard-float and soft-float rootfs gates, plus
hard-float PCC kernel/rootfs QEMU gates for Malta and Malta64.  The updated
N64 UART-only boot matrix passed on
real hardware with PCC and GCC kernels.  The current N64 hard-float PCC ROM
with a runtime compressed RAM block device boots on real hardware to root login and basic
shell use (`ls`, `uptime`).  The full updated N64 hard/soft PCC rootfs smoke
still needs fresh real hardware passes before becoming the N64 hardware
baseline.  On 2026-07-07 the full N64 hard-float PCC userland build gate
completed with both GCC and PCC kernels; both images used a 32768 KiB rootfs
and passed `fsutil --check`.

Current policy:

- Kernel build: GCC by default.  MIPS PCC kernel builds are opt-in with
  `KERNEL_COMPILER=pcc`; i686 kernels use GCC.
- Userland build: GCC by default.
- Supported userland compiler selectors: `gcc` and `pcc`.
- Supported rootfs endian selectors: `big` and `little`.  Big-endian PCC uses
  the `mips-rebsd` cross target; little-endian PCC uses `mipsel-rebsd`.
  `maltael` is the QEMU little-endian PCC gate.
- PCC CPU selectors: `vr4300` and `mips32r2`, with CPU-specific instruction
  mode and C ABI alignment.
- PCC float ABI selectors: `hard` and `soft`, with ABI-specific build stamps.
- PCC MIPS code generation supports `-fomit-frame-pointer`; PCC kernel builds
  use it to match the historical GCC kernel stack layout more closely while
  preserving `$fp` in functions whose frame cannot be safely addressed from
  `$sp`.
- PCC kernel builds use the in-tree ReBSD assembler and linker.  For N64 this
  means PCC-generated kernel assembly is assembled with
  `mips-rebsd-as --elf -EB -march=vr4300`, and the final kernel link uses
  `mips-rebsd-ld --elf -EB`.
- Native `/usr/bin/cc` and `/usr/bin/pcc`: imported PCC in the rootfs.
- C++/`p++`: deferred to future work and not installed by default.

The compiler selectors are common to every architecture and keep the kernel
choice independent from the userland choice:

- `KERNEL_COMPILER` selects the kernel compiler.  i686 accepts `gcc`; MIPS
  accepts `gcc` and `pcc`.
- `USERLAND_COMPILER=gcc|pcc` selects the userland/rootfs compiler.
- `MIPS_ROOTFS_CPU` selects the rootfs CPU ABI.  Supported values are
  `vr4300` and `mips32r2`.
- `N64_USERLAND_FLOAT` and `MIPS_ROOTFS_FLOAT` select the userland/rootfs float
  ABI when PCC is used.  Supported values are `hard` and `soft`.
- `MIPS_ROOTFS_ENDIAN` selects the rootfs endian ABI.  Supported values are
  `big` and `little`; `little` is used by `BOARD=maltael`.
- `N64_USERLAND_CPU`, `N64_USERLAND_FLOAT`, and `N64_USERLAND_ENDIAN` are N64
  aliases for the same ABI selectors.  If both alias families are set, they
  must agree.

Both compiler selectors default to `gcc`.  Setting `USERLAND_COMPILER=pcc`
selects the imported PCC frontend for userland while keeping the in-tree ReBSD
`as`, `ld`, `ar`, and `ranlib` as the target binary tools.

The i686 PCC gate builds the kernel with GCC, builds the complete userland with
PCC, then uses the target-native PCC toolchain inside QEMU:

```sh
make -C sys/i386 BOARD=pc KERNEL_COMPILER=gcc USERLAND_COMPILER=pcc \
    pcc-host-smoke
make -C sys/i386 BOARD=pc KERNEL_COMPILER=gcc USERLAND_COMPILER=pcc \
    pcc-smoke-runtime
```

## MIPS ELF Support

MIPS userland is now linked as static ELF32 and the kernel keeps compatibility
with legacy a.out exec.  The in-tree ReBSD MIPS tools are still the required
assembler/linker/archive tools; they are not replaced by GNU binutils:

- `as` accepts `--elf`, `-EB`, `-EL`, and CPU selectors such as
  `-march=vr4300` or `-march=mips32r2`.
- `ld` accepts `--elf`, `-EB`, `-EL`, `-T script`, `-L`, `-l`, and standard
  linker-script constructs used by the current MIPS scripts, including
  `MEMORY`, `SECTIONS`, `NOLOAD`, explicit section addresses, `> region`,
  `/DISCARD/`, `KEEP`, `PROVIDE`, and `ASSERT`.
- The installed user linker scripts live under `/usr/lib/ldscripts`, not
  directly under `/usr/lib`.
- Big-endian rootfs builds install `/usr/lib/ldscripts/elf32-bigmips.ld`.
- Little-endian rootfs builds install `/usr/lib/ldscripts/elf32-littlemips.ld`.
- The old compatibility path `/usr/lib/elf32-mips.ld` is deliberately not
  installed.

For cross PCC SDKs, the same scripts are installed under the target sysroot
inside the SDK:

```text
cross-pcc/mips-rebsd/lib/ldscripts/elf32-bigmips.ld
cross-pcc/mipsel-rebsd/lib/ldscripts/elf32-littlemips.ld
```

The ReBSD linker chooses the default script from its own target name and endian
mode.  A `mips-rebsd-ld` search uses the big-endian target directory, while a
`mipsel-rebsd-ld` search uses the little-endian target directory.  Passing
`-T/path/to/script.ld` remains the explicit override.

Common build forms:

```sh
make tools
make -C sys/mips BOARD=malta USERLAND_COMPILER=pcc native-pcc-smoke-runtime
make -C sys/mips BOARD=malta USERLAND_COMPILER=pcc native-pcc-regress-runtime
make -C sys/mips BOARD=malta USERLAND_COMPILER=pcc MIPS_ROOTFS_CPU=mips32r2 MIPS_ROOTFS_FLOAT=soft linpack-smoke-runtime
make -C sys/mips BOARD=malta64 USERLAND_COMPILER=pcc native-pcc-regress-runtime
make -C sys/mips BOARD=malta64 USERLAND_COMPILER=pcc MIPS_ROOTFS_FLOAT=soft native-pcc-regress-runtime
make -C sys/mips BOARD=maltael KERNEL_COMPILER=pcc USERLAND_COMPILER=pcc rootfs.img kernel
make -C sys/mips BOARD=n64 USERLAND_COMPILER=pcc kernel.z64 preflight.z64
make -C sys/mips BOARD=malta64 KERNEL_COMPILER=pcc USERLAND_COMPILER=pcc kernel
make -C sys/mips BOARD=malta KERNEL_COMPILER=pcc USERLAND_COMPILER=pcc kernel
make -C sys/mips BOARD=n64 KERNEL_COMPILER=gcc USERLAND_COMPILER=pcc N64_ROOTFS_KBYTES=32768 kernel.z64
make -C sys/mips BOARD=n64 KERNEL_COMPILER=pcc USERLAND_COMPILER=pcc N64_ROOTFS_KBYTES=32768 kernel.z64
make -C sys/mips BOARD=n64 KERNEL_COMPILER=pcc USERLAND_COMPILER=pcc kernel.z64 preflight.z64
make -C sys/mips BOARD=n64 KERNEL_COMPILER=pcc USERLAND_COMPILER=pcc N64_MINIMAL_UART_ONLY=1 kernel.z64 preflight.z64
```

Use `KERNEL_COMPILER=gcc USERLAND_COMPILER=pcc` for normal N64
hardware and performance validation.  PCC-kernel N64 builds are currently
substantially slower and should be treated as explicit compiler-correctness
tests rather than the default hardware gate.

Fresh checkouts should run `make tools` before MIPS builds.  Always use the
shared wrapper form, `make -C sys/mips BOARD=... target`; it forwards the
PCC smoke targets to the selected board.  The full QEMU PCC gate is
`pcc-smoke-all-runtime`; it boots QEMU, runs `/root/pcc-smoke-all.sh`, and
requires `PCC_SMOKE_ALL_RC:0`.

Full PCC QEMU gates:

```sh
make -C sys/mips BOARD=malta64 KERNEL_COMPILER=pcc USERLAND_COMPILER=pcc pcc-smoke-all-runtime
make -C sys/mips BOARD=malta64 KERNEL_COMPILER=pcc USERLAND_COMPILER=pcc MIPS_ROOTFS_FLOAT=soft pcc-smoke-all-runtime
make -C sys/mips BOARD=malta KERNEL_COMPILER=pcc USERLAND_COMPILER=pcc pcc-smoke-all-runtime
make -C sys/mips BOARD=malta KERNEL_COMPILER=pcc USERLAND_COMPILER=pcc MIPS_ROOTFS_FLOAT=soft pcc-smoke-all-runtime
make -C sys/mips BOARD=maltael KERNEL_COMPILER=pcc USERLAND_COMPILER=pcc pcc-smoke-all-runtime
make -C sys/mips BOARD=maltael KERNEL_COMPILER=pcc USERLAND_COMPILER=pcc MIPS_ROOTFS_FLOAT=soft pcc-smoke-all-runtime
```

The QEMU PCC smoke matrix verified on 2026-07-12 from clean, independent
out-of-tree builds after removing the obsolete PIC32 port is:

```text
malta64 vr4300  hard  /root/pcc-smoke-all.sh  PCC_SMOKE_ALL_RC:0
malta64 vr4300  soft  /root/pcc-smoke-all.sh  PCC_SMOKE_ALL_RC:0
malta   mips32r2 hard  /root/pcc-smoke-all.sh  PCC_SMOKE_ALL_RC:0
malta   mips32r2 soft  /root/pcc-smoke-all.sh  PCC_SMOKE_ALL_RC:0
maltael mips32r2 hard  /root/pcc-smoke-all.sh  PCC_SMOKE_ALL_RC:0
maltael mips32r2 soft  /root/pcc-smoke-all.sh  PCC_SMOKE_ALL_RC:0
```

The same runs included `linpack-pcc`; representative results were about
13 MFLOPS for VR4300 hard-float, 12.5-13 MFLOPS for mips32r2 hard-float,
and 0.9 MFLOPS for soft-float.

Build-only PCC board gates for targets without a QEMU smoke target:

```sh
make -C sys/mips BOARD=n64 KERNEL_COMPILER=pcc USERLAND_COMPILER=pcc N64_ROOTFS_KBYTES=32768 all
make -C sys/mips BOARD=ci20 KERNEL_COMPILER=pcc USERLAND_COMPILER=pcc all
```

The obsolete PIC32 port is not part of ReBSD; the supported targets are the
MIPS boards listed above.

The PCC hard-float kernel/rootfs gates were rerun on 2026-07-06 after the MIPS
`-fomit-frame-pointer` pass-ordering fix:

```text
malta64 vr4300  hard  /root/pcc-smoke-all.sh  PCC_SMOKE_ALL_RC:0  linpack 12095.997 / 11946.666 KFLOPS
malta   mips32r2 hard  /root/pcc-smoke-all.sh  PCC_SMOKE_ALL_RC:0  linpack 10996.365 / 10692.598 KFLOPS
```

For N64-like Malta/Malta64 low-memory smoke, keep QEMU backing RAM large enough
for the staged root image but cap kernel-visible RAM and swap explicitly:

```sh
make -C sys/mips BOARD=malta64 USERLAND_COMPILER=pcc MIPS_ROOTFS_CPU=vr4300 \
    MIPS_ROOTFS_FLOAT=hard MIPS_ROOTFS_KBYTES=16384 \
    MALTA_RAM_KBYTES=8192 MALTA_QEMU_RAM=64M kernel
```

This layout maps the Malta root image at physical `0x00800000`, gives the guest
4 MiB of user memory with 8 MiB `physmem`, and keeps the rootfs outside
kernel-visible RAM.  QEMU Malta's real BIOS/pflash ROM window is 4 MiB, so it
is too small for the normal PCC rootfs.  The 2026-07-06 low-memory
`/root/pcc-smoke-all.sh` gate passed on both Malta64/VR4300 hard-float and
Malta/MIPS32r2 hard-float with `PCC_SMOKE_ALL_RC:0`.

`pcc` mode controls how the target userland and libraries are built.  It does
not switch the kernel or N64 stage0 by itself.  Kernel PCC builds must be
requested explicitly with `KERNEL_COMPILER=pcc`.  When that selector is
active, the kernel assembly
and link steps still use ReBSD tools, not GNU `as`/`ld`; N64 stage0 remains on
the external N64 GCC toolchain.  PCC runtime builds do not use GCC wrappers:
the standalone cross SDK first builds the ReBSD tools and cross PCC without
target runtime libraries, then uses that cross PCC to build `crt0.o`, libc,
libm, and `libpcc.a`, then builds the target rootfs and native PCC against
those PCC-built libraries.

Kernel version banners record the selected compiler and ABI, for example:

```text
ReBSD for Malta64: built on user@host with pcc Portable C Compiler ..., cpu=vr4300, float=hard, endian=big
```

Compression is a runtime property of a normal RAM block device, not of the VM
swap pager.  Every `ramN` is an equivalent, initially unconfigured device.
`ramctl create /dev/ram1 backing=2M size=2x compression` dynamically allocates
a 2 MiB backing store and exposes a 4 MiB logical device.  The device can then hold
Linux swap v1 or a filesystem; swap is attached and detached with `swapon` and
`swapoff`.  `ramctl destroy` returns the backing pages to VM.  There is no
build-time compressed-swap selector or board-reserved RAM-disk pool.

The 2026-07-06 real N64 UART-only boot isolation matrix passed for all four
debug ROMs: PCC and GCC kernels with raw and compressed RAM-device profiles.  These ROMs
use `N64_MINIMAL_UART_ONLY=1` and a small rootfs containing `/sbin/init`,
`/libexec/getty`, `/bin/login`, `/bin/sh`, and the basic mount/fs tools.  The
minimal rootfs must keep `/bin/login`; without it `getty` accepts a username,
fails its login exec, exits, and `init` immediately respawns a new login prompt.

The full 2026-07-06 N64 hard-float PCC compressed-RAM ROM also reached root login on
real hardware.  Basic shell commands including `ls -l /` and `uptime` worked.
The full smoke is not closed yet: `uname -a` currently triggers a kernel
`TLB load/fetch` panic after login, so that path remains an open runtime issue.
The 2026-07-07 build-only gate produced two full PCC-userland N64 ROMs, one
with a GCC kernel and one with a PCC kernel, using `N64_ROOTFS_KBYTES=32768`.
These ROMs still require real-hardware smoke before replacing the 2026-07-06
hardware notes.

Standalone cross SDK builds use common MIPS selectors:

```sh
sys/mips/tools/build-cross-pcc-sdk.sh --cpu vr4300 --float soft --endian big --prefix /path/cross-pcc
sys/mips/tools/build-cross-pcc-sdk.sh --cpu mips32r2 --float hard --endian little --prefix /path/cross-pcc
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
cross-pcc/mips-rebsd/lib/ldscripts/elf32-bigmips.ld
```

For `--endian little`, replace `mips-rebsd` with `mipsel-rebsd` and
`elf32-bigmips.ld` with `elf32-littlemips.ld`.

For rootfs builds, `MIPS_PCC_PROVIDER=cross` uses
`MIPS_PCC_HOST_PREFIX=/path/cross-pcc`.  `MIPS_PCC_PROVIDER=system` may be used
on a ReBSD host with a compatible CPU/toolchain, so the build does not require
building a host PCC unconditionally.

CPU/ISA and float ABI are runtime compiler modes.  Endian is target identity:
big-endian SDKs install `mips-rebsd-*` tools and little-endian SDKs install
`mipsel-rebsd-*` tools so headers, predefined macros, linker scripts, and
libraries stay internally consistent.
