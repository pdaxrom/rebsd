# RetroBSD MIPS ports

This tree is the shared home for big-endian MIPS ports.

Layout:

- `common/` - MIPS CPU, exception, FPU, TLB and userland ABI code shared by
  all MIPS boards.
- `n64/` - Nintendo 64 board support: RDRAM layout, video, SI/Joybus and
  n64cart hardware.
- `malta/` - QEMU Malta board support: 32 MB RAM, 16550 serial console,
  16 MB RAM-loaded ROM root filesystem and RAM-backed swap.

Both Malta and N64 are built as boards under the shared `sys/mips` architecture.
Use `make -C sys/mips BOARD=n64 kernel.z64` for the N64 cartridge image and
`make -C sys/mips BOARD=malta kernel` for the QEMU Malta kernel.

## Running Malta in QEMU

Build the Malta root filesystem and kernel from the repository root:

```
make -C sys/mips/malta rootfs.img kernel
```

Run QEMU through the board makefile:

```
make -C sys/mips/malta run
```

Or run the generated kernel directly:

```
qemu-system-mips -M malta -m 32M -nographic -serial mon:stdio \
    -no-reboot -kernel sys/mips/malta/unix.elf
```

Do not lower `-m 32M` for the current Malta layout. The kernel root filesystem
is linked at physical `0x00600000`, reserves 16 MiB, and RAM swap starts after
that image. Booting with less RAM can corrupt the first process swap image and
produce misleading scheduler or `longjmp` crashes.

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
  frees a sector, and writes enforce NOR `1 -> 0` programming semantics. This
  keeps QEMU ROMFS tests close to real flash behavior without reserving an 8 MiB
  kernel `.bss` image.

Malta mounts the fake flash automatically:

```
/dev/cartflash0 /cart romfs rw 0 0
```

The generated `run` target uses the current Malta layout:

```
make -C sys/mips/malta run
```

For Ethernet bring-up in QEMU, use the ISA NE2000 virtual adapter target:

```
make -C sys/mips/malta run-net
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
```

Keep this as the real-device bring-up path until the Malta virtual NIC is
stable.  The N64 hardware network backend is expected to be a later USB network
adapter design, not a direct first step.

This starts QEMU with `-m 32M`. The kernel keeps the normal 2 MiB user window,
loads the root filesystem at physical `0x00600000`, reserves 16 MiB for that
image, keeps `/var` on a 1 MiB RAM disk, and uses the remaining high RAM for
swap.

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
- the `romdisk` memory region length in `sys/mips/malta/malta.ld`.

If the root image grows past the current 32 MiB address plan, also raise
`MALTA_QEMU_RAM` and `MALTA_RAM_SIZE`. The current `/cart` device is separate:
it is an 8 MiB sparse RAM-backed NOR flash emulator for ROMFS tests, not the
boot root filesystem.

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
