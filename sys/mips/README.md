# ReBSD MIPS ports

This tree is the shared home for MIPS ports.

Layout:

- `common/` - MIPS CPU, exception, FPU, TLB and userland ABI code shared by
  all MIPS boards.
- `n64/` - Nintendo 64 board support: RDRAM layout, video, SI/Joybus and
  n64cart hardware.
- `malta/` - QEMU Malta board support: 16550 serial console, QEMU-loaded
  read-only root filesystem, per-process VM address spaces, and RAM-backed
  swap.
- `malta64/` - QEMU Malta/R4000 compatibility board config.  It uses the same
  shared Malta support with the VR4300/MIPS-III ABI profile.
- `maltael/` - little-endian QEMU Malta board config.  It reuses the Malta
  board support code and supports both external `mipsel` GCC and PCC rootfs
  builds.

Both Malta and N64 are built as boards under the shared `sys/mips` architecture.
Use `make -C sys/mips BOARD=n64 kernel.z64` for the N64 cartridge image and
`make -C sys/mips BOARD=malta kernel` for the QEMU Malta kernel.
Use `make -C sys/mips BOARD=maltael rootfs.img kernel` for the little-endian
Malta bring-up. The obsolete PIC32 port has been removed from ReBSD.

All generated files are placed in an object root.  In `O=/path/to/build`, `O`
is the uppercase Latin letter O (for object/output), not the digit zero `0`.
Omit it to use the automatic sibling `../retrobsd-build/<profile>` directory.
Board Makefiles and kconfig C sources are generated there, so do not run make
directly in `sys/mips/<board>`.  See
[out-of-tree builds](../../docs/OUT_OF_TREE_BUILDS.md) for the directory
layout, parallel-build rules, and make compatibility.

## Build Matrix

The shared MIPS rootfs rules support these compiler and ABI selectors:

```text
MIPS_ROOTFS_COMPILER=gcc|pcc
MIPS_KERNEL_COMPILER=gcc|pcc
MIPS_ROOTFS_CPU=vr4300|mips32r2
MIPS_ROOTFS_FLOAT=hard|soft
MIPS_ROOTFS_ENDIAN=big|little
```

The normal big-endian boards are:

```sh
make tools
make -C sys/mips BOARD=malta rootfs.img kernel
make -C sys/mips BOARD=malta64 rootfs.img kernel
make -C sys/mips BOARD=malta MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc kernel
make -C sys/mips BOARD=malta64 MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc kernel
```

The little-endian board supports GCC and PCC:

```sh
make -C sys/mips BOARD=maltael rootfs.img kernel
make -C sys/mips BOARD=maltael MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc rootfs.img kernel
```

## Time-of-day clocks

All MIPS boards use the common TODR provider and Gregorian conversion code.
Boards without an attached hardware clock retain the existing embedded
filesystem timestamp fallback.  Creator Ci20 attaches two hardware providers:
the board PCF8563 on I2C4 at address `0x51` is primary and the JZ4780 internal
RTC is secondary.  The I2C4 adapter contains only JZ4780 clock, pinmux and
register access; PCF8563 calendar/BCD handling and JZ4780 RTC semantics live
under `sys/rtc` and are reusable by other boards.  A successful
`settimeofday` writes every writable attached RTC so the primary and fallback
remain synchronized.

## Minimal rootfs regression matrix

Every filesystem image is checked for the boot-critical files (`init`, `sh`,
`getty`, `mkfs`, `mount`, configuration files, and console/root/swap device
entries) before `fsutil` is allowed to package it.  The shared minimal profile
is 6 MiB and omits development files and the native compiler while retaining
the libc ABI smoke binary.  The N64-specific minimal profile additionally
retains its VR4300 VM and PCC diagnostics.

The same contract makes manual packaging follow the rootfs contents.
Command pages in sections 1 and 8 are included only with the corresponding
installed executable.  Pages for formats, configuration files, ABIs, and
other non-command objects have explicit object rules; a page without its
described object, a described object with an available but omitted page, or a
new non-command page without a rule fails the build.

Run the complete isolated matrix with:

```sh
make -C sys/mips rootfs-board-matrix
```

The matrix creates a timestamped directory below
`/private/tmp/rebsd-rootfs-matrix` unless `ROOTFS_MATRIX_OUT` is supplied.  It
boots and logs in on QEMU Malta, Malta64, and MaltaEL; build-checks the CI20
image; builds the N64 `uartmin-gcc`, `uartmin-pcc-gcc`, and
`uartmin-pcc-pcc` ROMs; and finally boots each exact N64 filesystem under
the Malta64/R4000 8 MiB memory profile.  Every board and N64 compiler
combination uses a separate object tree.  A single board build can use the
same gate directly:

```sh
make -C sys/mips BOARD=malta O=/private/tmp/rebsd-malta-min \
    MIPS_ROOTFS_PROFILE=minimal MIPS_ROOTFS_NATIVE_PCC=0 \
    rootfs-boot-smoke-runtime
```

The full-profile boot gate exercises the complete interactive command set.
The minimal-profile gate runs the shared `libc-abi-smoke` binary because that
profile intentionally omits the manuals and commands required by
`rootfs-boot-smoke.sh`.  Both profiles also run the common process-reaping
check.

CI20 has no matching QEMU machine in this tree, so its matrix entry is a
kernel/rootfs build plus the same rootfs contract check.

The N64 UART-only PCC configurations deliberately select the ReBSD assembler
and linker rather than GNU binutils:

```sh
# PCC kernel, GCC userland.
make -C sys/mips BOARD=n64 O=/private/tmp/rebsd-n64-pcc-gcc \
    N64_BUILD_CONFIG=uartmin-pcc-gcc all

# PCC kernel, PCC userland.
make -C sys/mips BOARD=n64 O=/private/tmp/rebsd-n64-pcc-pcc \
    N64_BUILD_CONFIG=uartmin-pcc-pcc all
```

`maltael` sets `MIPS_ROOTFS_ENDIAN=little`, uses
`qemu-system-mipsel`, and defaults to
`/Users/sash/Library/mipsel-toolchain/bin/mipsel-elf-` for the GCC path.

The full PCC QEMU smoke gate is `pcc-smoke-all-runtime`.  It boots QEMU,
runs `/root/pcc-smoke-all.sh`, and expects `PCC_SMOKE_ALL_RC:0`:

```sh
make -C sys/mips BOARD=malta64 MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc pcc-smoke-all-runtime
make -C sys/mips BOARD=malta64 MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc MIPS_ROOTFS_FLOAT=soft pcc-smoke-all-runtime
make -C sys/mips BOARD=malta MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc pcc-smoke-all-runtime
make -C sys/mips BOARD=malta MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc MIPS_ROOTFS_FLOAT=soft pcc-smoke-all-runtime
make -C sys/mips BOARD=maltael MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc pcc-smoke-all-runtime
make -C sys/mips BOARD=maltael MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc MIPS_ROOTFS_FLOAT=soft pcc-smoke-all-runtime
```

For the hardware-image build gates used by the VM matrix:

```sh
make -C sys/mips BOARD=n64 N64_KERNEL_COMPILER=gcc N64_USERLAND_COMPILER=pcc N64_ROOTFS_KBYTES=32768 all
make -C sys/mips BOARD=ci20 MIPS_KERNEL_COMPILER=gcc MIPS_ROOTFS_COMPILER=gcc all
```

Focused VM gates build every required artifact in the selected object root,
boot the resulting kernel in QEMU, and write their serial logs under
`O/tests`.  They do not copy binaries from another build profile:

```sh
make -C sys/mips BOARD=malta O=/work/rebsd-malta vm-smoke-runtime
make -C sys/mips BOARD=malta O=/work/rebsd-malta \
    VM_STRESS_ITERATIONS=100 vm-stress-runtime
make -C sys/mips BOARD=malta O=/work/rebsd-malta vm-io-smoke-runtime
make -C sys/mips BOARD=malta O=/work/rebsd-malta vm-ne2k-smoke-runtime
make -C sys/mips BOARD=malta O=/work/rebsd-malta native-pcc-smoke-runtime
```

`vm-smoke-runtime` covers the process/VM ABI, `vm-stress-runtime` repeats that
coverage and requires all resource counters to return to their warmed-up
baseline, `vm-io-smoke-runtime` covers loopback networking, the fake USB peer,
and RAM-backed filesystem/block tools, and `vm-ne2k-smoke-runtime` exercises
the QEMU ISA NE2000 and a TCP guest-forward.  The native compiler gate uses a
small in-guest PCC compile/link workload plus compiler-qualified `net-smoke`
and `libc-abi-smoke` binaries built by the rootfs dependency graph.  Set
`NET_SMOKE_NATIVE_COMPILE=1` or `LIBC_ABI_NATIVE_COMPILE=1` only for an
explicit in-guest rebuild on a profile with enough process memory.

### N64 8 MiB QEMU gate

`BOARD=malta64 MALTA_MEMORY_PROFILE=n64-8m` uses the R4000 emulator with the
same kernel load address, 4 MiB user limit, framebuffer, `/var`, and swap
reservations as an 8 MiB N64.  The profile fixes the kernel-visible RAM at
8192 KiB and enables the shared MIPS compressed RAM-swap backend: the 1792 KiB
physical store exposes 3584 KiB of logical swap.  QEMU still needs 32 or 64
MiB of backing RAM because the emulator-only rootfs blob starts at physical
8 MiB; that backing is excluded from the kernel physical map.

Use separate object roots for the GCC and PCC userland gates:

```sh
make -C sys/mips BOARD=malta64 O=/work/rebsd-qemu/n64-8m-gcc \
    MALTA_MEMORY_PROFILE=n64-8m MALTA_QEMU_RAM=32M \
    MIPS_KERNEL_COMPILER=gcc MIPS_ROOTFS_COMPILER=gcc \
    MIPS_ROOTFS_NATIVE_PCC=0 MIPS_ROOTFS_KBYTES=16384 all
make -C sys/mips BOARD=malta64 O=/work/rebsd-qemu/n64-8m-gcc-pcc \
    MALTA_MEMORY_PROFILE=n64-8m MALTA_QEMU_RAM=64M \
    MIPS_KERNEL_COMPILER=gcc MIPS_ROOTFS_COMPILER=pcc \
    MIPS_ROOTFS_NATIVE_PCC=1 MIPS_ROOTFS_EXEC_FORMAT=aout \
    MIPS_ROOTFS_KBYTES=32768 all
make -C sys/mips BOARD=malta64 O=/work/rebsd-qemu/n64-8m-pcc-pcc \
    MALTA_MEMORY_PROFILE=n64-8m MALTA_QEMU_RAM=64M \
    MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc \
    MIPS_ROOTFS_NATIVE_PCC=1 MIPS_ROOTFS_EXEC_FORMAT=aout \
    MIPS_ROOTFS_KBYTES=32768 all

make -C sys/mips BOARD=malta64 O=/work/rebsd-qemu/n64-8m-gcc \
    MALTA_MEMORY_PROFILE=n64-8m MALTA_QEMU_RAM=32M \
    MIPS_KERNEL_COMPILER=gcc MIPS_ROOTFS_COMPILER=gcc \
    MIPS_ROOTFS_NATIVE_PCC=0 MIPS_ROOTFS_KBYTES=16384 \
    VM_STRESS_ITERATIONS=100 vm-stress-runtime
make -C sys/mips BOARD=malta64 O=/work/rebsd-qemu/n64-8m-gcc-pcc \
    MALTA_MEMORY_PROFILE=n64-8m MALTA_QEMU_RAM=64M \
    MIPS_KERNEL_COMPILER=gcc MIPS_ROOTFS_COMPILER=pcc \
    MIPS_ROOTFS_NATIVE_PCC=1 MIPS_ROOTFS_EXEC_FORMAT=aout \
    MIPS_ROOTFS_KBYTES=32768 native-pcc-smoke-runtime
```

All kernel, rootfs, compiler, and smoke-test inputs are built or staged by
these dependency graphs; the gate does not copy artifacts between profiles.
The PCC/PCC profile is additional compiler coverage for shared kernel code;
the N64 hardware candidate remains the GCC-kernel/PCC-userland profile.

## ELF Userland

MIPS userland is linked as static ELF32.  The kernel can execute both the new
ELF binaries and legacy a.out binaries.  Rootfs endian is carried through all
generated artifacts:

- `MIPS_ROOTFS_ENDIAN=big` uses `-EB`, `elf32ebmip`, and
  `/usr/lib/ldscripts/elf32-bigmips.ld`.
- `MIPS_ROOTFS_ENDIAN=little` uses `-EL`, `elf32elmip`, and
  `/usr/lib/ldscripts/elf32-littlemips.ld`.

The linker scripts are installed under `/usr/lib/ldscripts` in the target
rootfs.  The old flat `/usr/lib/elf32-mips.ld` path is not installed.  Native
and cross PCC builds use the ReBSD MIPS `as` and `ld` in ELF mode; GNU binutils
are not part of the PCC target toolchain.

Standalone PCC SDK builds use the same selectors:

```sh
sys/mips/tools/build-cross-pcc-sdk.sh --cpu vr4300 --float hard --endian big --prefix /path/cross-pcc
sys/mips/tools/build-cross-pcc-sdk.sh --cpu mips32r2 --float hard --endian little --prefix /path/cross-pcc
```

Big-endian SDKs install `mips-rebsd-*` tools and
`mips-rebsd/lib/ldscripts/elf32-bigmips.ld`.  Little-endian SDKs install
`mipsel-rebsd-*` tools and
`mipsel-rebsd/lib/ldscripts/elf32-littlemips.ld`.

## Running Malta in QEMU

Build the Malta root filesystem and kernel from the repository root:

```
make -C sys/mips BOARD=malta O=/work/rebsd-malta rootfs.img kernel
```

Run QEMU through the board makefile:

```
make -C sys/mips BOARD=malta O=/work/rebsd-malta run
```

Or run the generated kernel directly:

```
qemu-system-mips -M malta -m 64M -nographic -serial mon:stdio \
    -no-reboot -kernel /work/rebsd-malta/obj/sys/mips/malta/unix.elf
```

For little-endian Malta, build the default GCC image or select PCC explicitly:

```
make -C sys/mips BOARD=maltael rootfs.img kernel
make -C sys/mips BOARD=maltael MIPS_KERNEL_COMPILER=pcc MIPS_ROOTFS_COMPILER=pcc rootfs.img kernel
```

The `maltael` config defaults to
`/Users/sash/Library/mipsel-toolchain/bin/mipsel-elf-`, sets
`MIPS_ROOTFS_ENDIAN=little`, and uses `mipsel-rebsd-*` for PCC rootfs builds.
Run it with:

```
make -C sys/mips BOARD=maltael run
```

The Malta kernel-visible RAM size is controlled by `MALTA_RAM_KBYTES`; QEMU's
`-m`/`MALTA_QEMU_RAM` must still be large enough to contain the QEMU-loaded
root image and optional RAM swap backing.  The root filesystem is linked at
physical `0x00800000`, outside `physmem` in 8 MiB low-memory smoke runs, and
RAM swap starts after that image unless `MALTA_RAMSWAP_KBYTES` overrides the
logical swap size.  QEMU Malta's real BIOS/pflash ROM window is only 4 MiB, so
the normal 16-32 MiB PCC rootfs cannot be stored there.

At the login prompt, use the passwordless root account:

```
login: root
```

Useful first checks after login:

```
mount
df -T
/root/net-smoke.sh
/root/net-header-smoke.sh
/root/types-smoke.sh
```

The serial console is attached to stdio. Exit QEMU with `Ctrl-A x` when using
`-serial mon:stdio`.

## ROMFS and cart flash

The writable cartridge ROMFS VFS code lives in `sys/mips/common` and uses a
small board backend instead of calling N64 hardware directly.

- N64 uses `sys/mips/n64/romfs_backend.c`, which routes sector read/write/erase
  to the n64cart flash driver.
- Malta uses `sys/mips/malta/cartflash.c`, which exposes `/dev/cartflash0` as
  an 8 MiB sparse NOR-flash emulator. Unallocated sectors read as `0xff`, erase
  frees a sector, and writes enforce NOR `1 -> 0` programming semantics. The
  sparse sector backing store is linked after the QEMU-loaded root image, not in
  the kernel `.bss`, so PCC kernel builds do not consume the fixed 2 MiB kernel
  link area with test flash storage.

Malta mounts the fake flash automatically:

```
/dev/cartflash0 /cart romfs rw 0 0
```

The generated `run` target uses the current Malta layout:

```
make -C sys/mips BOARD=malta run
```

For Ethernet bring-up in QEMU, use the ISA NE2000 virtual adapter target:

```
make -C sys/mips BOARD=malta run-net
```

This starts QEMU with `ne2k_isa` at I/O base `0x300`, IRQ `9`, and MAC
`52:54:00:12:34:56`.  After logging in as `root`, a minimal external-network
smoke is:

```
/sbin/ifconfig ne0 inet 10.0.2.15 netmask 255.255.255.0 up
/sbin/route add default 10.0.2.2 1
/usr/bin/ping -c 1 10.0.2.2
/usr/bin/netstat -i
/usr/bin/netstat -r
/root/ne2k-smoke.sh
```

`run-net` also enables QEMU user networking `guestfwd` at
`10.0.2.100:2323`, connected to host `/bin/cat`.  The staged
`/root/ne2k-smoke.sh` test uses that endpoint to verify a real TCP send/receive
path through the NE2K driver, not just loopback.

Keep this as the real-device bring-up path until the Malta virtual NIC is
stable.  The N64 hardware network backend is expected to be a later USB network
adapter design, not a direct first step.  `run-net` inherits `MALTA_QEMU_RAM`,
`MALTA_RAM_KBYTES`, and `MIPS_ROOTFS_KBYTES` from the selected build profile;
it uses the same per-process VM layout and root image as the normal `run`
target.

Malta and N64 use the same shared MIPS rootfs manifest,
`sys/mips/rootfs.manifest`. The board build first stages the common userland
layout from `sys/mips/rootfs`, then applies the board overlay and
board-specific device manifest.
Malta reuses the generated N64 userland staging tree so missing utilities are
caught in QEMU before flashing hardware, but it rebuilds `/usr/include/machine`
from generic `sys/mips` headers and overlays Malta-specific `/etc` files.

Both boards keep the same small `/bin` boot/single-user command set. Diagnostics
and the native toolchain live under `/usr/bin`, with headers and archives under
`/usr/include` and `/usr/lib`. The login profile sets
`PATH=/bin:/sbin:/usr/bin:/usr/sbin`, so target-side smoke scripts can call
`as`, `cc`, `pcc`, `romfsctl`, `ps`, `vmstat`, `w`, and `wc` without hard-coded
absolute paths.

To increase the Malta root filesystem, keep these three values in sync:

- `MIPS_ROOTFS_KBYTES` in `sys/mips/Makefile.kconf`;
- `MALTA_ROMDISK_BYTES` and the following `MALTA_RAMSWAP_PHYS_START` layout in
  `sys/mips/layout.h`;
- the `romdisk` memory region length in `sys/mips/malta/malta.ld` and
  `sys/mips/malta/malta64.ld`;
- the `rootfs` region length in `sys/mips/malta64/stage0_linker.ld`.

If the root image grows past the current 32 MiB address plan, also raise
`MALTA_QEMU_RAM`.  Raise `MALTA_RAM_KBYTES` only when the guest should see more
kernel-visible RAM. The current `/cart` device is separate: it is an 8 MiB
sparse RAM-backed NOR flash emulator for ROMFS tests, not the boot root
filesystem.

After logging in as `root`:

```
mount
romfsctl info
df -T /cart
cd /cart && diskspeed -m 1
mkdir /cart/malta-test
echo hello >/cart/malta-test/a.txt
cat /cart/malta-test/a.txt
mv /cart/malta-test/a.txt /cart/malta-test/b.txt
rm /cart/malta-test/b.txt
rmdir /cart/malta-test
cd /
/sbin/umount /cart
```

Shared target-side regression checks that should pass on Malta before moving a
new rootfs or toolchain change to N64 hardware:

```
/root/lang-smoke.sh
/root/cpp-calendar-smoke.sh
/root/secondary-cc-smoke.sh
/root/types-smoke.sh
/root/ll-smoke.sh
/root/ll-abi-smoke.sh
/root/cc-pcc-smoke.sh
/root/ne2k-smoke.sh
/root/runtime-stress.sh quick
smoke-as-vr4300
matrix-as-vr4300
```

`lang-smoke.sh` covers the installed scripting/interpreter tools in the shared
MIPS rootfs: shell failure handling and command substitution, `awk`, `pdc`,
classic `forth`, `retroforth`, `picoc`, and `tcl`.

`cpp-calendar-smoke.sh` covers the `/bin/cpp` compatibility link and
`calendar(1)` preprocessing of a writable `/var/tmp` calendar file.

`secondary-cc-smoke.sh` covers the currently staged secondary C compiler
backends (`scc`, direct `smlrc`, and `lcc`) through assembly and `ld -r` only.
It is not a full executable ABI smoke; use the `cc`/`pcc` tests for that.

`runtime-stress.sh` is the long-running Malta/N64 runtime stress.  Use
`/root/runtime-stress.sh quick` for a short QEMU sanity run.  For a multi-hour
hardware or QEMU run, start `/root/runtime-stress.sh` with no arguments and
stop it with `Ctrl-C`.  It writes only to `/var/tmp` and repeatedly exercises
`date`, `sleep`, fork/exec, pipes, shell child commands, and the kmem-reading
diagnostics (`ps`, `vmstat`, `w`, and `pstat`).

The Malta sparse flash backend intentionally keeps a limited number of RAM
sectors, so use `diskspeed -m 1` for QEMU smoke runs instead of the command's
default 8 MiB test size.
