# ReBSD N64 port notes

This document describes the current Nintendo 64 port state, how the ROM is
built, and how the early platform code works on real N64 hardware.

The port targets the Nintendo 64 VR4300 as a 32-bit big-endian MIPS III
machine using the o32 ABI. The current hardware target is a stock N64 with
4 MiB base RDRAM or 8 MiB with the Expansion Pak. The current cartridge
hardware target is the `n64cart` UART/RGB LED register block; display output
uses the N64 VI framebuffer.

## Current status

The current port boots a base RetroBSD system from a cartridge ROM image:

- stage0 starts from the N64 ROM and loads an ELF32 big-endian kernel blob.
- The kernel installs exception vectors and temporary bootstrap user TLB
  entries, then switches process 1 to the shared per-process MIPS pmap.
- RDRAM size is detected at startup and printed by the kernel.
- Root is a read-only UFS romdisk stored in the ROM image.
- Swap and volatile `/var` storage are RAM-backed block devices; `/tmp` is a
  rootfs symlink to `/var/tmp`.
- `/dev/console` is a real tty-backed console with VI framebuffer output.
- `/dev/tty` is the controlling tty major.
- `/dev/ttyS0` is the n64cart serial tty.
- `/dev/rgbled0` controls the n64cart RGB LED through ioctl.
- `/dev/cartflash0` exposes the n64cart SPI flash command path for controlled
  sector read/write/erase ioctls. Write and erase operations are rejected below
  the reported `romfs_offset`, so the firmware area is protected from ROMFS
  tools and mounts.
- `/dev/cartflash0` is intentionally a character-device flash interface rather
  than a generic block device; the cartridge ROMFS driver is expected to own
  erase/program/map/list handling directly.
- The n64cart flash driver also exposes kernel-callable raw helpers for future
  ROMFS mounting, so the filesystem implementation can share the same SPI
  flash access path without routing through the userland ioctl ABI.
- `/etc/rc` mounts `/dev/cartflash0` on `/cart` as writable `romfs`; the same
  path can still be mounted manually with `mount -t romfs /dev/cartflash0
  /cart`.
- `/dev/fb0` exposes the current 16-bit framebuffer, mode ioctls, and a
  fixed uncached user mapping.
- `/dev/joypad0`..`/dev/joypad3`, `/dev/mouse0`..`/dev/mouse3`, and
  `/dev/kbd0`..`/dev/kbd3` expose synchronous Joybus input snapshots.
- A RandNET keyboard on any controller port is polled through SI/Joybus and
  feeds `/dev/console` input while `/dev/ttyS0` remains available for serial
  login.
- `/dev/romdisk`, `/dev/swap`, `/dev/ram0`, `/dev/null`,
  `/dev/zero`, `/dev/ttyS0`, `/dev/rgbled0`, `/dev/cartflash0`,
  `/dev/fb0`, Joybus input devices, and the pty nodes are generated into the
  root filesystem from kernel device definitions.
- Userland is built from the normal `src/cmd` tree as static ELF32 big-endian
  binaries linked for the N64 user address window.  The kernel also keeps
  legacy a.out exec compatibility.
- The N64 rootfs selects its BSD command subset, `/sbin` tools, and
  library-backed interactive utilities through the shared `src/cmd/Makefile`
  install flow.
- The normal boot path runs `/etc/rc`, starts `/libexec/getty` for the enabled
  `/etc/ttys` lines, and logs in through `/bin/login`.
- Userland FPU is enabled and the kernel saves/restores FPU state.
- The in-tree ReBSD toolchain supports the current ELF userland path and still
  keeps target-endian a.out object I/O for legacy compatibility in `as`, `ld`,
  `ranlib`, `nm`, `aout`, `size`, `strip`, and libc `nlist()`.

Known hardware smoke test on a real 8 MiB system, verified 2026-06-12 before
the expanded command set:

This log predates the volatile `/var` RAM disk, expanded 8 MiB user window,
and compressed RAM swap. Current default 8 MiB builds reserve 1 MiB for
`/var` and print `user mem = 4096 kbytes` and
`swap size = 4096 kbytes`.  With `N64_ZSWAP=0`, the same physical RAM pool is
exposed as raw 2048 KiB swap.

```
ReBSD N64 stage0
kernel blob size=0x00023020
jump kernel entry=0x80001000
ReBSD N64 kernel entry
rdram size=0x00800000

2.11 BSD Unix for N64: local build
n64romdisk: rootfs offset=26800 size=400000 magic=3c3c5346
phys mem  = 8192 kbytes
user mem  = 2048 kbytes
root dev  = (0,0)
swap dev  = (1,0)
root size = 4096 kbytes
swap size = 4096 kbytes
June 12 05:57:11 init: kernel security level changed from 0 to 1
June 12 05:57:11 init: kernel security level changed from 1 to 0

# ls
bin         etc         root        tmp
dev         lost+found  sbin        var
# ls -l /bin
total 85
-rwxrwxr-x  1 0           35392 Jun 12 05:57 ls
-rwxrwxr-x  1 0           50832 Jun 12 05:57 sh
# ls -l /etc
total 3
-rw-rw-r--  1 0              93 Jun 12 05:57 motd
-rwxrwxr-x  1 0             211 Jun 12 05:57 rc
-rw-rw-r--  1 0             282 Jun 12 05:57 ttys
# ls -l /dev
total 0
crw-rw-r--  1 0          0,   0 Jun 12 05:57 console
c-w--wx-wT  1 0          1,   2 Jun 12 05:57 null
brw-rw-r--  1 0          0,   0 Jun 12 05:57 romdisk
brw-rw-r--  1 0          1,   0 Jun 12 05:57 swap
crw-rw-r--  1 0          2,   0 Jun 12 05:57 tty
c-w--wx-wT  1 0          1,   3 Jun 12 05:57 zero
```

Additional hardware smoke test on the same class of system, verified
2026-06-12 after the N64 `BASEPRI()` timer/callout fix:

```
# for i in 1 2 3 4 5 6; do echo $i; sleep 1; done
1
2
3
4
5
6
#
```

Additional rootfs/userland smoke test on the same class of system, verified
2026-06-12 after adding `man`, selected `/sbin` tools, and login profile
defaults:

```
# echo $PATH
/bin:/sbin
# echo $PAGER
/bin/cat
# uname -a
2.11BSD  2.11BSD 2.11 BSD Unix for N64: local build  mips
# mount
root on / (read-only)
# fsck -n /dev/romdisk
** /dev/romdisk (NO WRITE)
...
72 files, 747 used, 3332 free
# man uname
UNAME(1)              General Commands Manual                    UNAME(1)
...
# man id
ID(1)                         General Commands Manual                       ID(1)
...
```

Additional multi-user login smoke test on a real 8 MiB system, verified
2026-06-12 after enabling `console` getty/login and pty nodes:

This log also predates the volatile `/var` RAM disk.

```
2.11 BSD Unix for N64: local build
pty: 4 units
n64romdisk: rootfs offset=27c00 size=400000 magic=3c3c5346
phys mem  = 8192 kbytes
user mem  = 2048 kbytes
root dev  = (0,0)
swap dev  = (1,0)
root size = 4096 kbytes
swap size = 4096 kbytes
June 12 09:28:46 init: kernel security level changed from 0 to 1

ReBSD/N64 0.1-Resurgence (console)

login: root
ReBSD/N64 early rootfs

This read-only filesystem is embedded in the cartridge ROM image.
# pwd
/root
# ls -l /dev
crw-rw-r--  1 root       0,   0 Jun 12 09:29 console
crw-rw-rw-  1 root       9,   0 Jun 12 09:28 ptyp0
crw-rw-rw-  1 root       8,   0 Jun 12 09:28 ttyp0
...
# uname -a
2.11BSD  2.11BSD 2.11 BSD Unix for N64: local build  mips
```

## Loopback networking

The N64 kernel now builds the shared MIPS INET and AF_UNIX stack.  Loopback is
the verified hardware baseline.  The tree also builds `usbn0`, an N64cart USB
Ethernet gadget using the cartridge USB device controller and the shared
`if_usbn` upper half.  The default N64 configuration exposes this link as a
standard CDC ECM USB Ethernet gadget.

The same stack and rootfs scripts passed on Malta/QEMU on 2026-06-28:

```
/root/net-smoke.sh
/root/net-header-smoke.sh
```

The same loopback stack passed on real N64 hardware on 2026-06-28.  After
flashing `kernel.z64`, log in as root and run:

```
/sbin/ifconfig lo0
/sbin/ifconfig lo0 inet 127.0.0.1 up
/usr/bin/ping -c 1 127.0.0.1
/root/net-smoke.sh
/root/net-header-smoke.sh
```

`/root/net-smoke.sh` also compiles and runs a target-side socket program,
checks UDP and TCP loopback, exercises AF_UNIX sockets, verifies `netstat`,
checks TCP `TIME_WAIT`, and fails if `netstat -m` reports mbuf allocation
drops, waits, or protocol drain calls.

The verified N64 hardware run showed `lo0` up, `ping -c 1 127.0.0.1`
successful after `/root/net-smoke.sh` configured the address, TCP loopback
connections visible in `netstat`, `net socket smoke ok`, `net-smoke ok`, and
both `cc` and `pcc` header smoke passes.

The current N64 USB network backend is intentionally separate from ROMFS flash
access.  It uses the n64cart USB controller registers and EP1 OUT/EP2 IN bulk
packets only; it must not switch SPI/QSPI flash modes or touch flash
erase/write/read sequencing.  USB device events are handled through the
n64cart CART interrupt on CP0 IP3; the timer path is not the normal transport
driver.

This separation is part of the driver contract.  The USB Ethernet upper half
(`if_usbn`) and both N64cart USB lower halves (`n64cart_usbnet` and
`n64cart_usbecm`) do not include or call `cartflash`, `romfs`, or
`n64cart_flash` code.  Flash mode transitions, write/erase waits, reboot
shutdown, and ROMFS synchronization remain owned by `n64cart_flash.c`,
`romfs_backend.c`, and `romfs_vfs.c`.

Two N64 USB lower drivers are available for `usbn0`:

- `USBNET_ECM` is the default.  It builds `n64cart_usbecm.c` and enumerates as
  CDC ECM, so Linux and macOS can bind a normal USB Ethernet interface without
  `n64usbnet-bridge`.  The ECM descriptor advertises the host-side MAC address
  `02:64:00:00:00:01`; the N64 `usbn0` interface uses
  `02:64:00:00:00:10`.  Keep these distinct so ARP does not see frames from
  the host's own MAC address.
- `USBNET_VENDOR` builds `n64cart_usbnet.c`, the original vendor-specific
  EP1/EP2 bulk protocol.  Use this only with
  `tools/n64usbnet/n64usbnet-bridge`; see `tools/n64usbnet/README.md` for
  TAP/utun setup.

For kernel debugging, `N64_USB_GDB=1` replaces both USB-network lower halves
with a CDC ACM GDB RSP endpoint while leaving the cartridge UART untouched.
Build, connection, supported packets, and the VM-independent crash-safety
boundary are documented in [GDB.md](GDB.md).

Future USB network backends should keep the same separation.  CDC NCM is the
preferred next standards-based candidate if ECM throughput becomes limiting;
RNDIS should stay deferred unless Windows host support becomes a concrete
target.

Switching backend is done in `sys/mips/n64/Config` by keeping `service usbnet`
and selecting exactly one of `options "USBNET_ECM"` or
`options "USBNET_VENDOR"`, then running:

```
make -C sys/mips BOARD=n64 reconfig
make -C sys/mips BOARD=n64 kernel.z64
```

Vendor-specific hardware checks have covered USB enumeration, `ifconfig usbn0`,
ARP, ICMP ping, cable unplug/replug with re-enumeration, and error-free
interface counters.  CDC ECM has been verified on macOS through
`AppleUserECM`: macOS creates an `en*` Ethernet interface with host MAC
`02:64:00:00:00:01`, N64 uses `02:64:00:00:00:10`, ARP resolves correctly,
ICMP works in both directions, and cable unplug/replug recovers.

For a static CDC ECM smoke on macOS:

```
sudo ifconfig en11 10.64.0.1 netmask 255.255.255.0 up
/sbin/ifconfig usbn0 inet 10.64.0.2 netmask 255.255.255.0 up
/usr/bin/ping -c 3 10.64.0.1
```

For TCP coverage, start an echo listener on the host and run the target smoke:

```
tools/n64usbnet/n64usbnet-echo 10.64.0.1 2323
/root/usbn-tcp-smoke.sh
```

If the link is configured through DHCP instead, run the echo helper on the
host-side DHCP address and pass that address to the target smoke, for example:

```
tools/n64usbnet/n64usbnet-echo 192.168.2.1 2323
/root/usbn-tcp-smoke.sh 192.168.2.1
```

The DHCP-mode TCP smoke has been verified on real N64 hardware with macOS as
the host: N64 obtained `192.168.2.3`, connected to host `192.168.2.1:2323`,
and completed the echo check.

For DHCP coverage, run a DHCP server on the host ECM interface and then:

```
/root/usbn-dhcp-smoke.sh
```

The DHCP smoke has been verified on real N64 hardware: N64 obtained
`192.168.2.3`, installed default route `192.168.2.1`, wrote resolver
`192.168.2.1`, and pinged the DHCP router successfully.

## Network userland tools

The MIPS rootfs includes the first network userland set:

- `/sbin/dhclient`
- `/usr/bin/ping`
- `/usr/bin/netstat`
- `/sbin/ifconfig`
- `/sbin/route`
- `/usr/bin/wget`
- `/usr/bin/telnet`
- `/usr/libexec/telnetd`
- `/usr/sbin/inetd`

`dhclient` writes `/var/run/dhclient.lease` and `/var/run/resolv.conf`; the
rootfs keeps `/etc/resolv.conf` as a symlink to the writable resolver file.
After a successful DHCP lease, DNS-backed tools can resolve host names through
`gethostbyname`.

The current post-flash userland network smoke set is:

```
/root/usbn-dhcp-smoke.sh
/root/usbn-tcp-smoke.sh 192.168.2.1
/root/wget-smoke.sh http://10.64.0.1:8080/wget-smoke.txt
/root/telnet-smoke.sh
```

Use the actual host-side USB Ethernet address for the TCP smoke argument.  On
the macOS DHCP setup used during bring-up that address was `192.168.2.1`.
For `wget-smoke`, serve a file containing `retrobsd wget smoke` from the host
HTTP server and pass the reachable URL explicitly.

This smoke set has been verified on real N64 hardware with macOS as the host:
DHCP, TCP echo across USB, HTTP download over USB, direct telnetd, encrypted
`telnetd -K`, and `inetd -> telnetd -i` all passed.

`telnetd` defaults to `/bin/login` and can also run a controlled shell for
smoke tests with `-s /bin/sh`.  The shipped `/etc/inetd.conf` keeps plain
TELNET disabled by default:

```
#telnet stream tcp nowait root /usr/libexec/telnetd telnetd -i
```

For lab-only encrypted TELNET tests, run `telnetd` manually with `-K key` and
connect with the same key from `telnet -K key`.  This is a small PSK stream
mode for controlled testing, not an SSH replacement.

## Toolchain

The default N64 toolchain path is:

```
/Users/sash/Library/n64-toolchain-opengl
```

`target-n64.mk` uses:

- `mips64-elf-gcc`
- `mips64-elf-ld -m elf32ebmip`
- `mips64-elf-objdump`
- `mips64-elf-size`
- `mips64-elf-nm`
- `mips64-elf-strip`
- `n64tool`

Kernel and stage0 code are built with:

```
-EB -march=vr4300 -mtune=vr4300 -mips3 -mabi=32
-G0 -mno-abicalls -fno-pic
```

The kernel is compiled with `-msoft-float` so normal C code does not emit FPU
instructions. The FPU save/restore assembly and N64 userland are built with
hard-float support.

GCC remains the default kernel compiler.  `N64_KERNEL_COMPILER=pcc` is an
explicit gate.  In that mode, PCC compiles kernel C to assembly, the ReBSD
assembler assembles it as ELF big-endian VR4300 code, and the final kernel link
uses the ReBSD linker in ELF mode:

```
mips-rebsd-as --elf -EB -march=vr4300
mips-rebsd-ld --elf -EB
```

Stage0 still uses the external N64 GCC toolchain because it is cartridge boot
glue, not the ReBSD kernel image.

The in-tree RetroBSD toolchain is not yet the primary N64 build toolchain. Its
current N64 work is staged as follows:

- target-endian object I/O is shared by `as`, `ld`, `ranlib`, `nm`, `aout`,
  `size`, and `strip`;
- N64 builds define `TARGET_BIG_ENDIAN`, so those tools default to big-endian
  output and still accept `-EL`/`-EB` where applicable;
- the ReBSD assembler and linker accept ELF mode.  N64 PCC kernel builds use
  `mips-rebsd-as --elf -EB -march=vr4300` and `mips-rebsd-ld --elf -EB`;
- N64 builds define `TARGET_VR4300`, so the in-tree `ld` default executable
  text base is `0x00400000`. The original `0x7f008000` default is retained for
  non-VR4300 targets and is not valid for N64 user `exec`;
- N64 builds define `TARGET_VR4300`, so the in-tree assembler rejects known
  MIPS32/MIPS32r2-only mnemonics that the NEC VR4300 cannot execute, while
  accepting the 32-bit VR4300 cache/TLB opcodes used by the kernel;
- the in-tree assembler has the COP1/FPU subset used by hard-float GCC/PCC
  VR4300 output, including `$fN` registers, `lwc1`/`swc1`, `ldc1`/`sdc1`,
  compiler aliases `l.s`/`s.s`/`l.d`/`s.d`, move/control transfers,
  single/double arithmetic, compare, convert, round/trunc/ceil/floor, and
  `bc1*` branches;
- the active native C compiler path is the imported PCC under `src/dev/pcc/pcc`.
  The old `src/cmd/cc`, `src/cmd/cpp`, and `src/cmd/ccom` sources have been
  removed so there is only one PCC implementation in the tree;
- the PCC `ccom` backend emits native assembler-compatible `.word` pairs for
  64-bit integer initializers instead of GAS-only `.dword`; the focused o32
  big-endian `long long` ABI smoke now covers arguments, returns, structs,
  external objects, and helper-call interactions for both `cc` and `pcc`;
- the PCC build uses 8-byte compiler heap alignment for VR4300, because
  floating constants store `long double` values in AST nodes and hard-float
  `sdc1` faults on 4-byte-only aligned addresses;
- libc runtime provides the compiler ABI helpers currently needed by that
  path, including 64-bit shifts, clz/ctz/ffs helpers, and the first
  64-bit integer/double conversion helpers;
- the N64 rootfs stages the in-tree toolchain under `/usr`: `/usr/bin/pcc`,
  `/usr/bin/cc`, `/usr/bin/cpp`, `/usr/libexec/pcc/cpp`,
  `/usr/libexec/pcc/ccom`, `/usr/bin/as`, `/usr/bin/ld`, `/usr/bin/ar`,
  `/usr/bin/ranlib`, `/usr/bin/nm`, `/usr/bin/aout`, `/usr/bin/strip`, and
  no-header smoke sources in `/root`;
- the generated rootfs also stages target headers under `/usr/include` and the
  native compiler runtime/archive set under `/usr/lib`, including `crt0.o`,
  `libc.a`, `libm.a`, and `libpcc.a`. PCC soft-float builds also stage the
  ABI-specific helper archive under `/usr/lib/softfloat/libpcc.a`;
- the `/usr/lib` compiler runtime is generated as big-endian ELF for the
  in-tree toolchain. `crt0.o` is assembled directly by the ReBSD assembler from
  `lib/startup/crt0.s`; `libc.a`, `libm.a`, and `libpcc.a` are built in an
  isolated `n64-native-runtime` tree and reindexed with the ReBSD `ranlib`
  after staging, so `__.SYMDEF` matches the rootfs file mtimes;
- the generated N64 user linker script is installed as
  `/usr/lib/ldscripts/elf32-bigmips.ld`.  The old flat
  `/usr/lib/elf32-mips.ld` path is not installed;
- the shared rootfs no longer exposes root-level `/include`, `/.profile`, or
  `/lib/*.a` compatibility entries; target headers and static compiler runtime
  archives live under `/usr/include` and `/usr/lib`;
- `/root/pcc-smoke.sh` runs the target smoke from `/var/tmp`, so it does not
  try to write compiler outputs into the read-only root filesystem;
- `/root/cc-pcc-smoke.sh` verifies both driver names, `cc` and `pcc`, which
  resolve from `/usr/bin`: the main checks rely on the default `/` sysroot, and
  one final FPU check keeps explicit `--sysroot /` covered;
- `/root/ll-smoke.sh` verifies the first `long long` runtime cases through
  both `/usr/bin/cc` and `/usr/bin/pcc`: global initializers,
  signed/unsigned shifts, arithmetic, compares, mixed register arguments,
  stack-passed `int`, `long long`, and `double` arguments, returns, and struct
  layout.
  PCC `ccom` depends on target libc `%ll` formatting when it prints
  64-bit constants. On big-endian MIPS, PCC `ccom` keeps its internal 64-bit
  register pair order as low/high, but emits integer pairs through the normal
  o32 high/low physical register ABI and stores 64-bit objects high word first.
  The current v3 smoke, including the stack-argument expansion, has been
  confirmed on N64 with both driver names;
- `/root/ll-abi-smoke.sh` is the focused o32 big-endian `long long` ABI
  check. It combines C and hand-written assembly to verify external object
  layout, struct member layout/alignment, register arguments, stack arguments,
  C-to-assembly calls, assembly-to-C calls, and return values through both
  `/usr/bin/cc` and `/usr/bin/pcc`;
- `/root/types-smoke.sh` is the broad scalar/aggregate type smoke for both
  `/usr/bin/cc` and `/usr/bin/pcc`. It covers signed and unsigned `char`, `short`,
  `int`, `long`, `long long`, `enum`, pointers, function pointers, `float`,
  `double`, `long double`, stack-passed scalar/FPU arguments, return values,
  static/global/local initialization, `const` objects and pointers, struct
  layout, bitfields, and big-endian union byte order. It has been confirmed on
  N64 after increasing the volatile `/var` RAM disk to 1 MiB;
- N64 native `as`/`ld` keep text segments 8-byte aligned in ELF and legacy
  a.out paths.
  This is required because PCC emits local hard-float double literals in text
  and the VR4300 faults on `ldc1` from 4-byte-only aligned addresses. The
  linker also emits real padding between input text segments, so a start file
  or other object with a 4-byte-only text length cannot shift the following PCC
  object and break its internal `.p2align` guarantees;
- `/usr/bin/smoke-as-vr4300` and `/usr/bin/smoke-as-vr4300.sh` run the
  assembler opcode smoke directly on N64, using `/usr/bin/as` by default;
- `/usr/bin/matrix-as-vr4300` and `/usr/bin/matrix-as-vr4300.sh` run the broader
  target-side VR4300 instruction matrix directly on N64. The current matrix has
  207 checks and has been confirmed on hardware;
- on 2026-06-25, the `/usr` rootfs split was confirmed on real N64 hardware:
  `PATH` was `/bin:/sbin:/usr/bin:/usr/sbin`, `/usr/bin/as`, `/usr/bin/cc`,
  `/usr/libexec/pcc/ccom`, and `/bin/cpp -> ../usr/bin/cpp` were present, and
  `smoke-as-vr4300`, `matrix-as-vr4300`, `/root/cc-pcc-smoke.sh`,
  `/root/types-smoke.sh`, `/root/ll-smoke.sh`, and `/root/ll-abi-smoke.sh`
  all passed;
- on 2026-06-25, the follow-up `/bin`/`/usr/bin` split was smoke-tested on
  Malta/QEMU before hardware testing: `/bin` kept only the boot/single-user
  command set, `/usr/bin` carried diagnostics and the native toolchain, PATH
  resolved both, and the assembler, compiler, type, `long long`, ABI, ROMFS,
  and `diskspeed -m 1` smoke tests all passed;
- on 2026-06-25, the shared `sys/mips/rootfs` overlay move was smoke-tested on
  real N64 hardware: `smoke-as-vr4300`, `matrix-as-vr4300`,
  `/root/types-smoke.sh`, `/root/ll-smoke.sh`, `/root/ll-abi-smoke.sh`,
  `/root/cc-pcc-smoke.sh`, `mount`, `df`, `w`, `ps aux`, `/usr/sbin/pstat -T`,
  and `/root/romfs-smoke.sh` all passed;
- on 2026-07-06, the UART-only boot isolation ROMs all booted on real N64
  hardware: PCC/raw swap, PCC/zswap, GCC/raw swap, and GCC/zswap. These builds
  use `N64_MINIMAL_UART_ONLY=1`; the minimal rootfs must include `/bin/login`
  in addition to `/libexec/getty`, otherwise entering `root` only respawns the
  login prompt because `getty` cannot exec the login program;
- on 2026-07-06, the full hard-float PCC zswap ROM booted on real N64 hardware
  to `ttyS0` root login.  The full rootfs mounted from ROM, `ls -l /` showed
  the expected `/bin`, `/sbin`, `/usr`, `/var`, and `/cart` layout, and
  `uptime` worked.  Later long-run smoke failures that appeared as random
  userland faults were traced to the aggressive PI DOM1 pulse width in the
  default libdragon ROM header on the tested RP2040-based N64cart.  ReBSD N64
  ROMs now use the hardware-tested `0x40` pulse width by default; see
  [ROM image layout](#rom-image-layout);
- N64 disables core dumps by default because the volatile `/var` filesystem is
  small. If core dumps are enabled explicitly, a crashing compiler can still
  exhaust the RAM disk, but that must be reported as an I/O or space error and
  must not corrupt the kernel inode free list;
- the current smoke checks compile-to-assembly, assembly, relocatable link,
  full executable link/run, and the first FPU executable link/run path. Further
  work is extending instruction support for any additional syntax emitted by
  PCC `ccom` and interpreter paths.

## Build entry points

Preferred top-level N64 build:

```
make -C sys/mips BOARD=n64 all
```

Useful specific targets:

```
make -C sys/mips BOARD=n64 kernel.z64
make -C sys/mips BOARD=n64 preflight.z64
make -C sys/mips BOARD=n64 smoke-as-vr4300
make -C sys/mips BOARD=n64 reconfig
make -C sys/mips BOARD=n64 clean
make -C sys/mips BOARD=n64 clean-all
```

Full PCC userland builds need a larger root image than the historical default.
The current build gate uses 32768 KiB and covers both kernel compiler choices:

```
make -C sys/mips BOARD=n64 N64_KERNEL_COMPILER=gcc N64_USERLAND_COMPILER=pcc \
    N64_ROOTFS_KBYTES=32768 kernel.z64

make -C sys/mips BOARD=n64 N64_KERNEL_COMPILER=pcc N64_USERLAND_COMPILER=pcc \
    N64_ROOTFS_KBYTES=32768 kernel.z64
```

The 2026-07-07 run completed both variants, built `linpack-pcc`, passed
`fsutil --check`, and produced big-endian MIPS-III ELF kernels.  That was a
build-only gate; real-hardware smoke is tracked separately.

The PCC optimization hardware image uses a GCC kernel and PCC VR4300
hard-float a.out userland.  The architecture entry point creates a clean
out-of-tree profile automatically:

```
make -C sys/mips BOARD=n64 N64_KERNEL_COMPILER=gcc \
    N64_USERLAND_COMPILER=pcc N64_USERLAND_CPU=vr4300 \
    N64_USERLAND_FLOAT=hard N64_USERLAND_EXEC_FORMAT=aout \
    N64_PCC_DEBUG_ROOTFS_KBYTES=6144 pcc-debug-image
```

The resulting ROM is under
`../retrobsd-build/n64-kgcc-upcc-vr4300-hard-big-aout/obj/sys/mips/n64/`.

This image includes `/root/linpack-gcc`, `/root/linpack-pcc`,
`/root/linpack-kernels-gcc`, and `/root/linpack-kernels-pcc`, built from the
same source with `-O2`, array size 120, and a one-second minimum timing window.
The GCC binaries use a separate GCC-built a.out `crt0.o`, `libc.a`, and
`libm.a`; the PCC binaries use the PCC-built runtime.  The kernel benchmark
self-tests and times rolled/unrolled `daxpy`, `ddot`, and `dscal`, plus
`idamax`, through typed volatile function pointers so each compiler emits an
out-of-line generic kernel.  Malta/QEMU `linpack-smoke-runtime` runs this extra
set only when `LINPACK_KERNEL_BENCH=1` is selected.

The extended N64 debug runner executes GCC first and PCC second and reports:

```
N64_LINPACK_CONFIG array_size=120 min_seconds=1
N64_LINPACK_BEGIN gcc
N64_LINPACK_RC gcc 0
N64_LINPACK_END gcc
N64_LINPACK_BEGIN pcc
N64_LINPACK_RC pcc 0
N64_LINPACK_END pcc
N64_LINPACK_KERNEL_CONFIG array_size=120 min_seconds=1
N64_LINPACK_KERNEL_BEGIN gcc
N64_LINPACK_KERNEL_RC gcc 0
N64_LINPACK_KERNEL_END gcc
N64_LINPACK_KERNEL_BEGIN pcc
N64_LINPACK_KERNEL_RC pcc 0
N64_LINPACK_KERNEL_END pcc
```

Either nonzero Linpack status makes `N64_PCC_DEBUG_END` nonzero.  The GCC
runtime is isolated under `n64-linpack-gcc-runtime.<abi>` and normal
`make clean` removes it.

The N64 JPEG framebuffer viewer includes TJpgDec and has no external image
decoder source dependency.

The board build directory is `sys/mips/n64`.

Generated outputs:

- `sys/mips/n64/unix.elf`: RetroBSD kernel ELF.
- `sys/mips/n64/kernel.z64`: bootable ROM with the real kernel.
- `sys/mips/n64/preflight.z64`: bootable ROM with the preflight kernel.
- `sys/mips/n64/rootfs.img`: generated UFS root filesystem.
- `sys/mips/n64/n64.ld.S`: kernel linker script source.
- `sys/mips/n64/n64-user.ld`: generated user linker script.
- `sys/mips/n64/n64-userland.stamp`: local build stamp for the
  selected N64 user commands.

The N64 clean target also descends through the selected shared userland build
with the same N64 filters. Generated command artifacts such as
`src/cmd/rgbled/rgbled`, `src/cmd/ptytest/ptytest`, object files,
disassemblies, and formatted catman pages are removed by the command makefiles,
not by hand.

Do not edit generated files by hand. Edit the source files listed below and
rerun `make -C sys/mips BOARD=n64 reconfig` or `make -C sys/mips BOARD=n64 all`.

## Generated files and source files

The N64 makefile is generated by the existing RetroBSD kconfig tool:

- source: `sys/mips/n64/Makefile.kconf`
- source: `sys/mips/files.kconf`
- source: `sys/mips/n64/files.N64`
- source: `sys/mips/n64/devices.kconf`
- source: `sys/mips/n64/Config`
- generated: `sys/mips/n64/Makefile`
- generated: `sys/mips/n64/ioconf.c`
- generated: `sys/mips/n64/swapunix.c`

The linker scripts are also generated for the board build:

- source: `sys/mips/n64/layout.h`
- source: `sys/mips/n64/n64.ld.S`
- source: `sys/mips/n64/user/user.ld.S`
- generated: `sys/mips/n64/n64.ld`
- generated: `sys/mips/n64/n64-user.ld`

The root filesystem manifest is generated from the shared MIPS base manifest,
the generated target header list, and device nodes derived from the kernel
headers:

- shared source: `sys/mips/rootfs.manifest`
- shared source: `sys/mips/rootfs/`
- board source: `sys/mips/n64/rootfs/`
- source: `sys/mips/n64/devnodes.awk`
- source: `sys/mips/n64/romdisk.h`
- source: `sys/mips/n64/ramswap.h`
- source: `sys/mips/n64/devmajors.h`
- source: `sys/include/conf.h`
- generated: `sys/mips/n64/rootfs.devnodes.manifest`
- generated: `sys/mips/n64/rootfs.generated.manifest`
- generated: `sys/mips/n64/rootfs.img`

This is intentional: if major/minor values change in the kernel, `/dev` is
regenerated from the same definitions instead of drifting.

## ROM image layout

The ROM image is produced by `n64tool` with a TOC:

```
n64tool --toc --title "REBSD N64" \
    --output kernel.z64 \
    --align 256 kernel_stage0.stripped.elf \
    --align 1024 rootfs.img
```

Every N64 ROM build then validates the native Z64 header and sets the PI
Domain 1 pulse-width byte to `0x40`.  Consequently, released images start with
`80 37 40 40`, rather than the libdragon default `80 37 12 40`.  The more
conservative timing is the N64 hardware default for ReBSD and applies equally
to `kernel.z64`, `preflight.z64`, and `pcc-debug.z64`.

This setting is required for reliable sustained ROM access on the tested
RP2040-based N64cart.  With the default `0x12` pulse width, repeated native PCC
tests eventually failed at unrelated instructions after executable pages had
been loaded from the ROM-backed UFS root filesystem.  Uploading the same image
with N64cart's `--fix-pi-bus-speed=40` option eliminated those failures during
an overnight run, while the same workload remained stable for three days on
Creator CI20.  The build now writes the equivalent value into the image
itself, so N64cart uploads do not require that external option.

For `preflight.z64`, `stage0.stripped.elf` is used instead of
`kernel_stage0.stripped.elf`.

The stage0 ELF contains the kernel ELF as an embedded blob through
`stage0_kernel_blob.S`. The root filesystem is a separate TOC entry named
`rootfs.img`.

The kernel ROM reader uses uncached PI ROM space at `0xb0000000` and searches
the first MiB of ROM for a TOC. It finds the `rootfs.img` entry by name and
uses the recorded offset/size as the backing store for the romdisk block
device.

## N64cart flash and ROMFS diagnostic

The n64cart flash support is split into a raw flash command path and a kernel
ROMFS mount path:

- `/dev/cartflash0` is an N64-only character device on the n64cart hardware
  major. It reads the cartridge JEDEC ID, firmware size register, and flash
  geometry, then accepts bounded sector read/write/erase ioctls. The driver
  rejects write/erase requests below `romfs_offset`, so raw flash access cannot
  overwrite the cartridge firmware area.
  Flash transactions run with interrupts masked so the timer-driven n64cart
  UART poll cannot touch the same cartridge register block while SPI command
  mode is active.
- `/usr/bin/romfsctl` is a diagnostic user command that vendors the ROMFS map/list
  implementation from the local n64cart sources and calls it through
  `/dev/cartflash0`.
- `mount -t romfs /dev/cartflash0 /cart` uses the same ROMFS core source inside
  the kernel. The first mounted version loads the cartridge map/list tables,
  creates synthetic inodes, and supports `stat`, `open`, `read`, `lseek`,
  directory iteration, `statfs`, create, write, append, truncate, unlink,
  rename, mkdir, and rmdir. Direct flash diagnostics remain available through
  `romfsctl`.
- The kernel ROMFS mount path validates the system entries before accepting the
  mount, so a bad map/list start offset or corrupted firmware/list/map entry is
  rejected before writable access is enabled.
- The shared ROMFS core protects the system entries `firmware`, `flashlist`,
  and `flashmap`, plus any read-only/system/reserved entry, from write,
  truncate, unlink, rename, and rmdir.
- The shared ROMFS core can use an opt-in metadata journal stored as ROMFS
  files created by `romfsctl format`. Existing cartridge ROMFS images that do
  not contain the journal files continue to work without journal recovery.
  Journal entries carry sequence and CRC checks so mount/update code can recover
  from an interrupted metadata flush when a valid committed journal entry is
  present.

The host-side synthetic journal test is:

```
make -C src/cmd/romfsctl host-journal-test
```

It links the ROMFS core with an in-memory NOR flash backend, injects deterministic
erase/write failures across a metadata-only rename flush, restarts the ROMFS
state, and verifies that pre-commit failures keep the old name while post-commit
failures recover the new name from the valid journal. It also corrupts the
primary flashlist and flashmap sectors after a clean journaled update and checks
that journal recovery restores a mountable image. This test deliberately does
not open `/dev/cartflash0`. As of this note, the new journal power-cut matrix
has been verified on the host synthetic backend and the Malta/QEMU ROMFS path;
the deliberate power-cut test has not yet been run on real N64cart hardware.

The current UFS `rootfs.img` remains the system root and is still demand-read
from cartridge ROM through the romdisk block driver. Cartridge ROMFS is mounted
separately at `/cart`; because the n64cart flash is fixed cartridge hardware,
the N64 `/etc/fstab` lists it and `/etc/rc` mounts it automatically during
multi-user boot as a writable ROMFS mount.

The hardware smoke test used on real n64cart hardware is:

```
ls -l /dev/cartflash0
romfsctl info
romfsctl free
romfsctl list /
romfsctl list /roms
romfsctl list -h /roms
romfsctl cat /roms/kernel.z64 >/dev/null
romfsctl cat /roms/kernel.z64 | wc
romfsctl mkdir /retrobsd-test
romfsctl write /retrobsd-test/hello.txt hello from retrobsd
romfsctl list /retrobsd-test
romfsctl cat /retrobsd-test/hello.txt
romfsctl cat /retrobsd-test/hello.txt | wc
romfsctl rename /retrobsd-test/hello.txt /retrobsd-test/renamed.txt
romfsctl list /retrobsd-test
romfsctl cat /retrobsd-test/renamed.txt
romfsctl rm /retrobsd-test/renamed.txt
romfsctl rmdir /retrobsd-test
```

The kernel ROMFS read smoke test is:

```
/sbin/mount
ls -l /cart
ls -l /cart/roms
cat /cart/roms/kernel.z64 >/dev/null
cat /cart/roms/kernel.z64 | wc
df -T /cart
```

If the automatic `/etc/rc` mount was skipped or `/cart` was manually
unmounted, the manual mount command is:

```
/sbin/mount -t romfs /dev/cartflash0 /cart
/sbin/mount
ls -l /cart
ls -l /cart/roms
cat /cart/roms/kernel.z64 >/dev/null
cat /cart/roms/kernel.z64 | wc
df -T /cart
```

This path was verified on real N64cart hardware on 2026-06-14. The test mounted
`/dev/cartflash0` at `/cart`, listed the root and `/cart/roms`, and read
`/cart/roms/kernel.z64` both directly to `/dev/null` and through a pipe to
`wc`. `df -T /cart` reported the `romfs` filesystem type. Unmounting `/cart`
with `/sbin/umount /cart` was also verified after the ROMFS source device was
accepted as a character device by the kernel unmount path.

The kernel ROMFS write smoke test is:

```
/sbin/mount -t romfs /dev/cartflash0 /cart
mkdir /cart/retrobsd-vfs-test
echo hello >/cart/retrobsd-vfs-test/hello.txt
cat /cart/retrobsd-vfs-test/hello.txt
echo again >>/cart/retrobsd-vfs-test/hello.txt
cat /cart/retrobsd-vfs-test/hello.txt
echo reset >/cart/retrobsd-vfs-test/hello.txt
mv /cart/retrobsd-vfs-test/hello.txt /cart/retrobsd-vfs-test/renamed.txt
cat /cart/retrobsd-vfs-test/renamed.txt
echo overwrite >/cart/retrobsd-vfs-test/other.txt
mv /cart/retrobsd-vfs-test/renamed.txt /cart/retrobsd-vfs-test/other.txt
cat /cart/retrobsd-vfs-test/other.txt
rm /cart/retrobsd-vfs-test/other.txt
rmdir /cart/retrobsd-vfs-test
```

The writable VFS version creates, writes, appends, truncates, unlinks, renames,
renames over an existing compatible destination, and creates/removes directories
through the same ROMFS flash map/list implementation used by `romfsctl`.
Non-empty destination directories are still rejected through the ROMFS delete
path. The firmware area and system entries are protected, and journaled ROMFS
images can recover metadata from the latest valid committed journal entry.
Older ROMFS images without journal files remain writable, but they do not get
journal recovery until reformatted with journal support.

The ROMFS VFS write/delete path was verified on real N64cart hardware on
2026-06-14. The test wrote and read `/cart/retrobsd-vfs-test/hello.txt`,
renamed it to `renamed.txt`, read it after the rename, removed it with exit
status 0, confirmed the directory was empty, and removed the test directory.
The rename-over-existing path was also verified with `/cart/rename-test`:
after writing `old` to `a.txt` and `new` to `b.txt`, `mv a.txt b.txt` left
`b.txt` containing `old`.
After reboot, mounting `/dev/cartflash0` at `/cart` again showed that the test
directory stayed deleted, confirming that the ROMFS map/list changes were
persisted to cartridge flash.

On 2026-06-24, the ROMFS unmount and reboot flash barriers were verified on
real N64cart hardware. `/root/romfs-smoke.sh` passed, `/sbin/umount /cart`
returned 0, and a later `/root/romfs-smoke.sh && reboot` returned through
stage0, remounted `/dev/cartflash0` on `/cart`, and passed `/root/romfs-smoke.sh`
again. The ROMFS VFS `sync` and `unmount` callbacks call the board flash
backend `sync` hook, and the N64 reboot path calls `n64cart_flash_shutdown()`,
which waits for flash WIP to clear before restoring quad-ROM mode.

On 2026-06-25, the same ROMFS smoke also passed on the `/usr` rootfs split
image before and after `sync`, `/sbin/umount /cart`, `mount /cart`.

The N64 n64cart flash backend keeps a 32 KiB read-ahead cache above the SPI
command path. A miss reads up to eight contiguous 4 KiB flash sectors in one
transaction; later reads inside that window are copied from RAM. Sector
write/erase invalidates the cache. This is a conservative read optimization:
it does not rewrite the N64cart ROM lookup table and it does not change the
ROMFS on-flash format. The change has been built with
`make -C sys/mips BOARD=n64 kernel.z64`, Malta/QEMU passed
`/root/romfs-smoke.sh` plus `cd /cart && diskspeed -m 1`, and real N64cart
hardware passed `/root/romfs-smoke.sh`, `/cart` `diskspeed`, `sync`,
`umount`, remount, and a second `/root/romfs-smoke.sh`.

Real N64cart `diskspeed` numbers with 4 KiB blocks after read-ahead:

```
Write speed: 8 Mbytes in 101.740 seconds = 80 kbytes/sec
 Read speed: 8 Mbytes in 5.750 seconds = 1424 kbytes/sec
```

`/bin/df` is included in the N64 rootfs. The shared `df` command accepts `-T`
on N64 and PIC32 builds to print the filesystem type reported by `statfs`:

```
df
df -T
df -T /cart
```

After mounting cartridge ROMFS, `df -T /cart` reports the `romfs` type.

`cpp` and `calendar` can be smoke-tested from the generated rootfs without
writing to `/`:

```
printf '#define N64 ok\nN64\n' | cpp
cd /var/tmp
echo '#include <calendar.computer>' > calendar
calendar
```

`romfsctl info` reports:

- the flash JEDEC ID;
- detected cartridge flash size in bytes;
- n64cart firmware size from the register block;
- the aligned ROMFS start offset;
- the 4096-byte erase sector size.

`romfsctl list` uses the same mode/type/size/name output format as the
host-side `usb-romfs list` utility. `romfsctl list -h` prints human-readable
file sizes, matching `usb-romfs list -h`.

## Login path

The N64 root filesystem uses the common System V/IRIX-style multi-user path
instead of an N64-only shell jump:

1. the kernel starts `/sbin/init`;
2. the copied `icode` passes `"-"` as the init option string, matching the
   PIC32 bootstrap convention and not requesting `-s`;
3. `init` reads `/etc/inittab`, runs the `sysinit` entry
   `/etc/rc.sysinit`, and enters the configured default runlevel;
4. the runlevel `wait` entry dispatches `/etc/rc 2`;
5. the `console` and `ttyS0` `respawn` entries start
   `/libexec/getty std.default` independently;
6. `getty` opens `/dev/console` or `/dev/ttyS0`, prints the login prompt, and
   execs `/bin/login`;
7. `login` authenticates against `/etc/passwd`, reads `/etc/group`, prints
   `/etc/motd`, and starts `/bin/sh` as a login shell.

Before opening a named terminal, `getty` detaches any controlling terminal
inherited from `init`.  Consequently the `ttyS0` getty cannot revoke or hang up
the framebuffer console getty.  `/etc/inittab` controls which gettys run;
`/etc/ttys` retains the terminal type and `secure` login policy metadata.

Hardware smoke test on real n64cart hardware shows both login paths coming up:

```
ReBSD/N64 0.1-Resurgence (ttyS0)
login:

ReBSD/N64 0.1-Resurgence (console)
login:
```

The first N64 account database is intentionally small because the cartridge
rootfs is read-only:

- `root` has an empty password and `/root` as its home directory.
- `console` and `ttyS0` are marked `secure` in `/etc/ttys`, so root login is
  allowed there.
- `/etc/rc` creates volatile `/var/run/utmp` and `/var/log/wtmp` after
  mounting the RAM-backed `/var`; if those files are unavailable, the existing
  `login`/`libutil` code still tolerates that by skipping accounting writes.

## Signals

N64 uses the same MIPS user signal ABI shape as PIC32. `sendsig()` builds a
user stack frame with:

- four argument words for the trampoline call area;
- a `struct sigcontext` containing the interrupted user registers, stack,
  return address, HI/LO, program counter, signal mask, and alternate-stack
  state;
- on MIPS III only, a private shadow after `sigcontext` containing the upper
  and original lower halves of every GPR and HI/LO.  The public o32 structure
  and handler arguments remain unchanged.

The kernel then redirects the saved user frame to call the user handler with:

```
a0 = signal number
a1 = signal code
a2 = &sigcontext
ra = user sigtramp
sp = signal frame
pc = handler
```

The libc MIPS `sigtramp` executes syscall `SYS_sigreturn` when the handler
returns. N64 `sigreturn()` validates the user `sigcontext`, restores the saved
registers and signal mask, and returns with `EJUSTRETURN` so the syscall trap
path does not overwrite the restored frame.  If a handler deliberately edits
a public 32-bit register value, `sigreturn()` sign-extends that new value;
otherwise it restores the complete interrupted 64-bit MIPS III register.

The kernel and user ABI are still 32-bit.  `Status.UX` only permits VR4300
user code to execute MIPS III 64-bit GPR instructions used internally for
`long long`; the exception frame saves those hardware values with `sd`/`ld`.
This does not widen VM addresses, sizes, physical addresses, pointers, or the
o32 calling convention.

This matters for the login shell: the first N64 signal stub treated every
caught signal as fatal. With that stub, `Ctrl-C` during `sleep 10` killed the
shell and caused `init` to respawn `getty`. With signal frames enabled,
`Ctrl-C` should interrupt only the foreground command and return to the shell
prompt.

## Boot flow

1. The N64 starts stage0 from the ROM.
2. stage0 prints early messages through `stage0_console_putc`.
3. stage0 validates the embedded kernel ELF:
   - ELF class must be 32-bit.
   - Byte order must be big-endian.
   - Machine type must be MIPS.
   - Program headers must fit inside the embedded blob.
4. stage0 copies each `PT_LOAD` segment into RDRAM through uncached KSEG1.
5. stage0 zeros segment BSS.
6. stage0 invalidates the instruction cache for the loaded kernel range.
7. stage0 jumps to the kernel entry, currently `0x80001000`.
8. The kernel runs `startup()`:
   - prints `ReBSD N64 kernel entry`
   - clears the single bootstrap `u..u_end` area used by process zero
   - installs exception vectors
   - initializes the shared MIPS pmap/TLB layer; process one disables the
     bootstrap legacy user entry when its private pmap becomes active
   - initializes MI interrupt masks
   - enables CP0 interrupt masks for MI and timer interrupts
   - detects and prints RDRAM size
   - forces the root filesystem read-only with `RB_RDONLY`

## Reboot Path

`boot()`/`reboot(2)` on N64 no longer only prints the reboot request and spins.
For a normal reboot, the kernel syncs pending buffers, forces the n64cart flash
interface to finish any pending SPI flash write/erase, restores idle quad-ROM
mode with chip-select high, disables N64 interrupt sources, and jumps back to
the resident stage0 entry at `0x80300000`.
The system UFS root is mounted read-only, so the N64 reboot path does not force
the root superblock dirty before `sync()`. The cartridge ROMFS mount at `/cart`
is writable by default; ROMFS unmount and reboot both wait for the flash WIP bit
to clear before reboot jumps back to stage0.

This is a software restart through the ROM-loaded stage0 image, not a full
console hardware reset.  Plain `reboot` and `halt` first request System V
runlevels 6 and 0 respectively.  `init` stops supervised gettys before
running `/etc/rc6.d` or `/etc/rc0.d`; the final script performs the direct
kernel operation with `reboot -q` or `halt -q`.  The N64 halt path then disables
interrupts and stops the CPU in a `wait` loop without respawning login prompts.

Hardware smoke-test passed on Expansion Pak hardware: `/sbin/reboot` syncs,
jumps through stage0, reloads the kernel, detects `0x00800000` RDRAM, mounts
the ROM rootfs, starts `init`, and reaches both `ttyS0` and `console` getty
login prompts.

The local libdragon and n64cart sources handle the console reset button as a
pre-NMI event. They do not provide a software cold-reset primitive that is safe
to call from the kernel, so the N64 port does not poke undocumented reset
registers. If a documented reset path is found later, add it behind an
explicit N64 implementation.

## Memory layout

The first-stage memory map is centralized in `sys/mips/n64/layout.h`.

4 MiB system:

```
0x00000000..0x000fffff  kernel, vectors, bootstrap u area
0x00100000..0x002fffff  VM page pool after bootstrap
0x00300000..0x0033ffff  resident stage0/restart image
0x00340000..0x0037ffff  stage0/early 320x240x16 framebuffer alias
0x00380000..0x003fffff  RAM swap fallback
```

8 MiB system:

```
0x00000000..0x000fffff  kernel, vectors, bootstrap u area
0x00100000..0x002fffff  VM page pool after bootstrap
0x00300000..0x0037ffff  resident stage0/restart image
0x00380000..0x004fffff  VM page pool after bootstrap
0x00500000..0x005fffff  /var RAM disk
0x00600000..0x007fffff  Expansion Pak RAM swap store
```

After VM startup, VI mode changes use exact page-rounded physically
contiguous wired allocations instead of a fixed Expansion Pak framebuffer
reserve. The early 320x240x16 console may keep using the stage0 alias until
the first mode change.

Important constants:

- `N64_KERNEL_LOAD_VADDR`: kernel link/load address, `0x80001000`.
- `N64_KERNEL_RESERVED`: first 1 MiB reserved for the kernel, vectors, and
  bootstrap user area.
- `N64_UAREA_SIZE`: 8 KiB for the bootstrap area and each dynamically
  allocated per-process user area. The live `struct user` currently occupies
  about 1.1 KiB; the remaining space is the process's kernel stack, so the N64
  port keeps more headroom than the original PIC32 3 KiB u-area for nested
  `exec`, `namei`, signal, and FPU paths.
- `N64_USER_VADDR_START`: user virtual base, `0x00400000`.
- `N64_USER_MAXMEM_4M`: 2 MiB user address window for base systems.
- `N64_USER_MAXMEM_8M`: 4 MiB user address window for Expansion Pak systems.
- `N64_USER_PHYS_START`: legacy bootstrap user-mapping base, `0x00100000`;
  after the bootstrap TLB entries are removed, ordinary pages in this range
  are allocated by VM.
- `N64_BASE_SWAP_PHYS_START`: 4 MiB fallback swap base, `0x00380000`.
- `N64_BASE_FB_PHYS_START`: early 320x240x16 stage0 alias, `0x00340000`.
- `N64_EXPANSION_SWAP_PHYS_START`: 8 MiB RAM block pool base, `0x00500000`.
- `N64_FB_USER_VADDR_START`: uncached framebuffer user mapping base,
  `0x00800000`.

`machparam.h` maps the old RetroBSD platform names onto this layout:

- `KERNEL_DATA_START`
- `KERNEL_DATA_END`
- `USER_DATA_START`
- `USER_DATA_END`
- `MAXMEM`

This keeps the N64 memory model local to `sys/mips/n64` while common kernel code
continues to use the normal RetroBSD names.

## RDRAM detection

RDRAM size detection is in `sys/mips/n64/memory.c`.

The detection order is:

1. Read the IPL-provided memory size word at `N64_BOOT_MEM_SIZE_ADDR`.
2. Accept it as 8 MiB if it is inside the 8 MiB threshold window.
3. Accept it as 4 MiB if it is inside the 4 MiB threshold window.
4. If that was not conclusive, probe the 8 MiB address range by writing
   independent patterns at the 4 MiB and 8 MiB probe addresses.
5. Restore saved probe words.
6. Fall back to 4 MiB if nothing else proves Expansion Pak memory.

The kernel prints the final value directly from startup:

```
rdram size=0x00400000
```

or:

```
rdram size=0x00800000
```

## TLB and user address space

The N64 bootstrap temporarily installs wired TLB entries for the legacy user
window. Base 4 MiB systems get one 2 MiB user TLB pair:

```
virtual  0x00400000..0x005fffff
physical 0x00100000..0x002fffff
```

Expansion Pak systems get two 2 MiB user TLB pairs:

```
virtual  0x00400000..0x007fffff
physical 0x00100000..0x004fffff
```

Each entry uses two 1 MiB pages through `TLB_PAGEMASK_1M`.  These entries are
bootstrap compatibility mappings only.  They are removed before process 1
runs; the physical pages then belong to the normal VM allocator except for the
resident stage0/restart range.

The VM bootstrap invalidates those temporary entries, resets `C0_Wired`, and
uses per-process 4 KiB pmap entries with ASIDs for normal execution. The
framebuffer is no longer a global wired mapping. `/dev/fb0` authorizes an
uncached `MAP_SHARED` device mapping in the calling process; its physical
reserve remains rounded up so the mapping cannot overlap RAM swap:

```
4 MiB: physical 0x00340000..0x0037ffff
8 MiB: physical 0x00500000..0x0053ffff
8 MiB high-res: physical 0x00500000..0x0059ffff
```

`DRMFBIOC_GETMAP` reports `0x00800000` as a preferred virtual-address hint.
The actual address is the return value of `mmap(2)` and belongs only to that
process. `copyin`, `copyout`, and `baduaddr` validate framebuffer access through
the same `vmspace` checks as other user mappings; no fixed-address bypass
remains.

User `read(2)`, `write(2)`, `readv(2)`, and `writev(2)` validate every iovec
against that user address policy before entering filesystem or character-device
I/O. This is required on N64 because old kernel `uiomove()` paths still use
direct copies after syscall entry; a crashed or corrupted process must not be
able to hand the kernel an arbitrary KSEG address and overwrite kernel tables.

On successful `exec` and on process changes, the N64 machine layer flushes
the user data/instruction cache range so newly copied user code is executable
on the VR4300.

## Root filesystem

Root is a read-only UFS image stored in ROM as `rootfs.img`.

The root image is built from:

- common overlay files in `sys/mips/rootfs/`
- N64-specific overlay files in `sys/mips/n64/rootfs/`
- `sys/mips/rootfs.manifest`
- generated `/dev` nodes
- selected user commands installed into the staging tree through the normal
  RetroBSD `src/Makefile` install flow

This follows the same install model as the top-level PIC32 build: commands are
built from `src/cmd/*`, installed into a `DESTDIR`, and then `fsutil` creates
the filesystem from the staged tree plus a manifest. The N64 build keeps an
explicit command subset for the cartridge rootfs, but it does not copy command
binaries directly out of `src/cmd`.

The current shared manifest includes a broader first-pass BSD userland:

- boot/login configuration: `/etc/fstab`, `/etc/gettytab`, `/etc/group`,
  `/etc/motd`, `/etc/passwd`, `/etc/profile`, `/etc/rc`, `/etc/ttys`,
  `/root/.profile`
- core `/bin`: the small boot/single-user set (`sh`, `login`, `ls`, `cat`,
  `cp`, `mv`, `rm`, `mkdir`, `rmdir`, `chmod`, `date`, `dd`, `df`, `echo`,
  `expr`, `hostname`, `kill`, `ln`, `pwd`, `sed`, `sleep`, `stty`, `sync`,
  `test`, `tr`, `uname`, `true`, `false`, and `[`)
- main `/usr/bin`: the broader BSD command set, diagnostics, native ReBSD
  toolchain (`as`, `ld`, `ar`, `ranlib`, `nm`, `strip`, `cc`, `pcc`, `cpp`,
  PCC `ccom` via `/usr/libexec/pcc`), interpreter tools, and smoke scripts
- terminal and interpreter tools backed by additional shared libraries:
  `emg`, `med`, `pdc`, `setty`, `sl`, and `tcl`
- N64 diagnostics and tools: `deco`, `fbset`, `fbview`, `n64input`,
  `ptytest`, `rgbled`, `romfsctl`, and `smux`
- `/sbin`: `bootloader`, `chown`, `chroot`, `fastboot`, `fsck`, `halt`,
  `init`, `mkfs`, `mknod`, `mkpasswd`, `mount`, `poweroff`, `pstat`,
  `reboot`, `shutdown`, `umount`, and `updatedb`
- required helpers and data: `/usr/libexec/bigram`, `/usr/libexec/code`,
  `/libexec/diffh`, `/libexec/getty`, `/usr/lib/deco/*`,
  `/usr/share/calendar/*`, `/usr/share/misc/more.help`,
  `/usr/share/man/whatis`, and selected generated cat pages in
  `/usr/share/man/cat1` and `/usr/share/man/cat8`

The generated manifest stages headers under `/usr/include`; root-level
`/include` is intentionally not included in the N64 root image.

The staging tree may contain extra files installed by selected command
makefiles, for example `reboot` installs `halt`, `fastboot`, `poweroff`, and
`bootloader` aliases. Those files do not enter `rootfs.img` until the shared
MIPS manifest or the board-specific generated manifest lists them.

`man`, `apropos`, and `whatis` are included with generated cat pages. Some
selected command makefiles already install their own cat pages; the N64 board
makefile additionally formats selected portable pages from `src/man/man1`,
`src/man/man8`, and `src/cmd/env/env.1` into the staging tree. It then
generates `/usr/share/man/whatis` from the staged cat pages with the existing
`src/man/makewhatis.sed` script before creating `rootfs.img`; the database is
not checked in as a static file. The N64 userland build sets `GROFF_NO_SGR=1`
so host `nroff` emits the classic overstrike format expected by the existing
manual index script.

`man` uses `more -s` as the default pager on an interactive tty, so
`/usr/bin/more` and `/usr/share/misc/more.help` are part of the ROM rootfs. For the
first N64 rootfs, `/etc/profile` and `/root/.profile` set
`PAGER=/bin/cat` so manual pages print directly instead of depending on the
interactive pager. The same profiles set `PATH=/bin:/sbin:/usr/bin:/usr/sbin`,
which makes the selected `/sbin` and `/usr/bin` tools visible from the shell
prompt.

The rootfs size defaults to 16384 KiB. The image stays in cartridge ROM and is
not preloaded into RDRAM:

```
N64_ROOTFS_KBYTES ?= 16384
```

It can be overridden on the make command line if the root filesystem needs to
grow:

```
make -C sys/mips BOARD=n64 N64_ROOTFS_KBYTES=24576 kernel.z64
```

Full PCC userland currently requires 32768 KiB:

```
make -C sys/mips BOARD=n64 N64_USERLAND_COMPILER=pcc \
    N64_ROOTFS_KBYTES=32768 kernel.z64
```

The romdisk block driver is read-only. Attempts to open it for write return
`EROFS`; write strategies also fail with `EROFS`.

## Block devices

Block major 0 is the ROM-backed root disk:

```
/dev/romdisk  b 0,0
```

Block major 1 is the N64 RAM-backed block pool. Minor 0 is swap; minor 1 is
the volatile UFS target for `/var`:

```
/dev/swap     b 1,0
/dev/ram0     b 1,1
```

RAM block sizing:

- 4 MiB system: 128 KiB `/dev/ram0`, 384 KiB physical swap store.
- 8 MiB system: 1 MiB `/dev/ram0`, 2 MiB physical swap store. Framebuffers
  no longer consume this block pool.

`N64_ZSWAP=1` is the default.  It selects the shared VM zswap backend and
keeps the same physical RAM store while exposing twice as many logical swap
blocks to the VM swap pager.  Each logical 1 KiB swap block is stored as zero,
raw, or compressed data in 256-byte physical units.  Releasing a VM swap slot
also discards its compressed physical units, so repeated pageout/pagein cycles
can reuse the store.  If a page cannot be represented in the physical store,
swapout fails with `ENOMEM` instead of panicking.  `N64_ZSWAP=0` restores the
raw RAM swap sizing for comparison.  Other MIPS boards can select
`ZSWAP_ENABLED` in their board configuration and use the same backend.

`N64_MINIMAL_ROOTFS=1` builds a dependency-tracked hardware-test rootfs while
keeping the normal N64 console and device configuration.  It is still packaged
as `rootfs.img` in the ROM TOC, so the kernel/rootfs lookup path is identical to
normal ROMs.  Every minimal image contains `init`, login support,
`vm-process-smoke`, and `vm-stress-smoke.sh`.  With a PCC userland,
`N64_MINIMAL_PCC_SMOKE` defaults to `1` and adds the native compiler, the exact
commands/scripts/sources used by `pcc-smoke-all.sh`, and no unrelated userland
programs.  That profile defaults to a 7168 KiB rootfs; the VM-only profile
defaults to 2048 KiB.

The reproducible GCC-kernel/PCC-userland hardware-test build is:

```sh
make -C sys/mips BOARD=n64 O=/work/rebsd-hw/n64-vm-pcc-min \
    N64_KERNEL_COMPILER=gcc N64_USERLAND_COMPILER=pcc \
    N64_USERLAND_CPU=vr4300 N64_USERLAND_FLOAT=hard \
    N64_USERLAND_ENDIAN=big N64_USERLAND_EXEC_FORMAT=aout \
    N64_MINIMAL_ROOTFS=1 N64_MINIMAL_PCC_SMOKE=1 \
    N64_MINIMAL_ROOTFS_KBYTES=7168 N64_ROOTFS_NATIVE_PCC=1 \
    N64_ZSWAP=1 all
```

`N64_MINIMAL_UART_ONLY=1` selects the same minimal-rootfs machinery and also
forces the UART-only console/debug drivers for boot isolation.  The minimal
`/etc/rc` only formats and mounts the volatile `/var`; it deliberately does not
mount `/cart`, because ROMFS and peripheral tests belong to a later full-rootfs
image.

`N64_BUILD_CONFIG=usbnetmin-pcc-gcc` selects a 3072 KiB CDC ECM diagnostic
rootfs with a PCC kernel and GCC userland.  It keeps the production polling
UART transport and USB network stack while omitting video, controller input,
ROMFS, native PCC, and the compiler stress suite.  The image contains
`dhclient`, `ifconfig`, `route`, `ping`, `netstat`,
`/root/usbn-smoke.sh`, and `/root/usbn-dhcp-smoke.sh`:

```sh
make -C sys/mips BOARD=n64 O=/work/rebsd-hw/n64-usbnet-min \
    N64_BUILD_CONFIG=usbnetmin-pcc-gcc all
```

The printed boot sizes therefore differ by installed RDRAM:

```
4 MiB: swap size = 768 kbytes with zswap, 384 kbytes raw
8 MiB: swap size = 4096 kbytes with zswap, 2048 kbytes raw
```

The root filesystem stays read-only. `/tmp` is a symlink to `/var/tmp` in the
ROM rootfs. `/etc/rc` formats the volatile RAM device at each boot with
`mkfs -i 4096`, mounts `/dev/ram0` on `/var`, sets `/var/tmp` sticky, and
creates `/var/db`, `/var/log`, `/var/run`, `/var/tmp`, and `/var/lock`. It
also creates volatile `/var/run/utmp` and `/var/log/wtmp` so the normal
login/accounting tools have writable files after multi-user boot. The same
script mounts the fixed n64cart ROMFS at `/cart` through the `/etc/fstab`
entry for `/dev/cartflash0`.

The kernel pipe implementation allocates temporary pipe inodes on `pipedev`.
The N64 attach code sets `pipedev` to `/dev/ram0`; after `/etc/rc` mounts
`/var`, shell pipelines use the writable RAM-backed UFS instead of the
read-only cartridge root.

## Character devices and tty

Current character devices, verified in the generated ROM rootfs:

```
/dev/console  c 0,0
/dev/null     c 1,2
/dev/zero     c 1,3
/dev/tty      c 2,0
/dev/ttyS0    c 3,0
/dev/rgbled0  c 4,0
/dev/fb0      c 5,0
/dev/joypad0  c 6,0
/dev/mouse0   c 7,0
/dev/ttyp0    c 8,0
/dev/ptyp0    c 9,0
/dev/kbd0     c 10,0
```

`/dev/console` is a normal RetroBSD tty endpoint backed by the common
`sys/kernel/cons.c` driver through N64 console hooks. It uses
`sys/mips/n64/video_console.c` for VI framebuffer output. The framebuffer
console does not consume n64cart UART input; serial login input belongs to
`/dev/ttyS0`. Console output is not mirrored to the UART, so keyboard echo and
shell output on `/dev/console` stay separate from serial logins.

The text console draws inside a 5% safe area to keep characters away from CRT
or capture-device overscan. This margin applies only to `video_console.c`;
`/dev/fb0` still exposes the full framebuffer.

The framebuffer console renders the cell buffer owned by the common
`sys/console/vtconsole.c` core into the VI framebuffer. This is intentionally
separate from `/dev/fb0` graphics access: the console is a tty renderer, while
`/dev/fb0` remains the raw framebuffer device. N64, Ci20 and i686 share the
same parser for the basic VT100 output used by shells and pagers: printable
ASCII, CR/LF/TAB/BS, ESC save/restore/reset, CSI cursor movement, erase
line/display, insert/delete character and line, SGR bold/underline/reverse
attributes, OSC skipping, and `CSI ?25h/?25l` cursor visibility. Backspace
only moves the console cursor left; BSD tty erase echo still performs the
visible erase through the normal `BS SPACE BS` sequence.

`/dev/console` reports its current text geometry through `TIOCGWINSZ`. The
reported character rows and columns are derived from the framebuffer console
safe-area geometry, so 640x480 and 320x240 modes naturally report different
sizes. `/dev/ttyS0` falls back to 80x24 when no explicit winsize has been set
by userland. This keeps `more`, `man`, smux, and curses-style tools from
falling back to zero-sized terminals after `login` clears the tty winsize.

`/dev/tty` is implemented through the standard `tty_tty` cdev entry and
therefore resolves to the controlling tty for the shell.

`/dev/ttyS0` is a normal tty line for the n64cart serial UART. It has its own
`struct tty`, line discipline, cdev entry, and `/etc/ttys` login line. The
default build uses the original polling registers: the cartridge UART hardware
interrupt is disabled and the CP0 timer path feeds received bytes to
`ttyinput()`. This is the supported transport and keeps compatibility with the
stable stock cartridge firmware.

`/dev/rgbled0` is a n64cart-specific character device. It is not a tty and is
not driven by `led_control()`. Userland controls it through:

```
N64RGBLEDIOC_SET    unsigned 0x00RRGGBB
N64RGBLEDIOC_GET    unsigned 0x00RRGGBB
```

The ROM rootfs includes `/bin/rgbled` as the user-facing control utility:

```
rgbled             # print current red green blue values
rgbled 255 0 0     # red
rgbled 0 255 0     # green
rgbled 0 0 255     # blue
rgbled 0 0 0       # off
```

`/dev/fb0` is backed by the architecture-independent ReBSD DRM framebuffer
core. It exposes the current RGBA5551 or RGBA8888 framebuffer through read/write,
Linux fbdev ioctls, common ReBSD mode ioctls, and a controlled uncached
`MAP_SHARED` mapping. The Linux-compatible subset is:

```
FBIOGET_FSCREENINFO  struct fb_fix_screeninfo
FBIOGET_VSCREENINFO  struct fb_var_screeninfo
FBIOPUT_VSCREENINFO  struct fb_var_screeninfo
FBIOPAN_DISPLAY      zero offsets only
FBIOBLANK            unblank/blank/DPMS levels
```

RGBA5551 is described as truecolor with red, green, blue, and transparency
bitfields at 11:5, 6:5, 1:5, and 0:1. RGBA8888 uses 24:8, 16:8, 8:8, and
0:8. ReBSD extensions provide explicit mode enumeration:

```
DRMFBIOC_GETINFO   struct drmfb_info
DRMFBIOC_GETMAP    struct drmfb_map
DRMFBIOC_GETMODE   struct drmfb_mode
DRMFBIOC_SETMODE   struct drmfb_mode
```

The `DRMFBIOC_*` requests are ReBSD's common extension ABI. They are not Linux
DRM/KMS or BSD `wsdisplay`; Linux compatibility is provided through the fbdev
requests in `<linux/fb.h>`.

`DRMFBIOC_GETMAP` returns `vaddr`, `bytes`, and `reserved_bytes`. `vaddr` is an
optional address hint for `mmap(2)`, not an installed mapping. `bytes` is the
current usable framebuffer length for the selected mode; `reserved_bytes` is
the page-rounded physically owned range. Programs map `bytes` from offset zero and use
the address returned by `mmap`, which may differ from the hint.

The default framebuffer mode is 320x240x16 on all N64 memory configurations.
Both 16-bit RGBA5551 and 32-bit RGBA8888 modes are available. Base 4 MiB
systems expose 320x240x16/32; Expansion Pak systems additionally expose
640x480x16/32 interlaced modes. A switch first allocates a contiguous wired
VM run for the new mode, programs VI, revokes stale userspace mappings of the
old buffer, and returns the old pages to VM. If the allocation fails, the
active mode is left unchanged:

```
fbset             # print current framebuffer mode
fbset -l          # list resolution/depth combinations
fbset 320x240x32  # select progressive 32-bit RGBA
fbset 640 480 16  # select interlaced RGBA5551, Expansion Pak only
fbset fill 0xff0000ff  # fill current RGBA8888 buffer with red
```

The former fixed framebuffer mapping and `fbset fill` path were hardware
smoke-tested on an 8 MiB N64 on 2026-06-12. The replacement process-local
`mmap` path still requires a new hardware smoke test.

`/bin/fbview` is a simple framebuffer JPEG viewer for graphics smoke tests:

```
/sbin/mount -t romfs /dev/cartflash0 /cart
fbview /cart/background.jpg
fbview /cart/moon.jpg
```

It opens `/dev/fb0` through the common DRM ABI, reads the active mode and
mapping hint, calls `mmap`, and streams baseline JPEG MCU blocks through
TJpgDec directly into the mapped framebuffer. It preserves the image aspect
ratio and uses 1/2, 1/4, or 1/8 JPEG downscaling before the final integer
nearest-neighbor mapping. Memory use is therefore independent of the full
decoded image size. The utility writes RGBA5551 on N64 and XRGB8888 on Ci20.
Progressive and lossless JPEG files are rejected with a diagnostic.

The VI setup reads the IPL TV type byte at `0xa4000009` and chooses PAL, NTSC,
or MPAL timing. PAL uses the PAL timing registers with a centered 640x480
active area, so the framebuffer size stays 320x240 or 640x480 rather than
becoming 640x576.

## Joybus, SI, And Input Devices

The low-level SI transport is `sys/mips/n64/si.c`. It performs one synchronous
64-byte Joybus/PIF exchange by DMA-writing a block to PIF RAM at `0x1fc007c0`
and DMA-reading the 64-byte reply back. The first implementation polls the SI
status bits with a timeout instead of enabling SI interrupts; this keeps the
bring-up path simple and independent from VI/timer interrupt routing.

`sys/mips/n64/joybus.c` builds the first supported Joybus commands using the same
layouts used by libdragon and the local test ROMs:

- identify: send length 1, receive length 3, command `0x00`
- N64 controller read: send length 1, receive length 4, command `0x01`
- N64 mouse read: same `0x01` command as controller, distinguished by
  identifier `0x0200`
- RandNET keyboard read: send length 2, receive length 7, command `0x13`,
  with the second sent byte carrying the keyboard LED state

N64 assigns local character majors for the first snapshot drivers:

```
joypad: /dev/joypad0..3  c 6,0..3
mouse:  /dev/mouse0..3   c 7,0..3
kbd:    /dev/kbd0..3     c 10,0..3
```

All three device families infer the controller port from the minor number.
`read(2)` returns the current binary snapshot structure; userland can also use
ioctls from `<machine/joybus.h>`:

```
N64JOYBUSIOC_IDENTIFY   struct n64joybus_port
N64JOYPADIOC_GETSTATE   struct n64joypad_state
N64MOUSEIOC_GETSTATE    struct n64mouse_state
N64KBDIOC_GETSTATE      struct n64keyboard_state
N64KBDIOC_SETLED        unsigned LED byte
```

The ROM rootfs includes `/usr/bin/n64input` for hardware smoke-testing:

```
n64input list
n64input joypad 0
n64input mouse 0
n64input kbd 0
n64input kbd-led 0 0x04
```

Hardware smoke-test passed on a real N64 on 2026-06-12 for Joybus identify,
N64 controller snapshots, N64 mouse snapshots, and RandNET keyboard snapshots.
Observed identifiers were `0x0500` for controller, `0x0200` for mouse, and
`0x0002` for keyboard. Keyboard LED ioctl smoke-test is still pending. The
drivers are intentionally synchronous snapshot devices for this first step.
The next step is feeding RandNET keyboard input into `/dev/console`; after
that, the input devices can grow event/blocking semantics.

N64 exposes `/dev/mem` and `/dev/kmem` for the historical BSD diagnostics
that still read kernel memory directly, including `w`, `ps`, `vmstat`, and
`pstat`. Character major 1 minors 0 and 1 accept reads from valid user or
kernel address ranges; `/dev/null` and `/dev/zero` remain minors 2 and 3.
`kmemdev()` returns `/dev/kmem`, and `iskmemdev()` marks minors 0 and 1 so
securelevel still blocks write opens. New N64-specific userland should prefer
sysctl/ioctl interfaces, but these devices keep the stock BSD monitoring tools
usable.

The kernel symbol lookup for those tools goes through the shared MIPS
`machdep.nlist` sysctl (`knlist(3)`), not a `/vmunix` a.out namelist file.
N64 and Malta therefore use the same `sys/mips/common/sysctl.c` symbol export
table for `_proc`, `_nproc`, `_inode`, `_file`, `_cp_time`, `_sum`, and related
diagnostic variables. Malta QEMU smoke-testing should include `w`, `ps ax`,
`vmstat`, `vmstat -f`, `/usr/sbin/pstat -T`, and `/usr/sbin/pstat -p` before trying the
same ROM on real N64 hardware.

The shared rootfs also includes `/root/runtime-stress.sh` for multi-hour
runtime checks on Malta and N64.  Run `/root/runtime-stress.sh quick` for a
short QEMU sanity pass, or `/root/runtime-stress.sh` with no arguments for an
open-ended run stopped by `Ctrl-C`.  The stress writes only to `/var/tmp` and
exercises repeated `date`/`sleep`, fork/exec, pipes, shell child commands, and
the kmem-reading diagnostics.

The console tty settings are initialized with echo, CR/LF mapping, erase,
kill, and control-character echo behavior. `/etc/gettytab` sets `cb`, `ce`,
and `ck` for N64 login lines before `login` runs. The stock `login` program
then clears local tty modes with `TIOCLSET 0`, so the N64 `/etc/profile` runs
`stty crt` after login to restore `CRTBS`, `CRTERA`, `CRTKIL`, and `CTLECH`
for shell input. The N64 console and n64cart UART drivers normalize both `BS`
and `DEL` input to the RetroBSD default erase character before calling
`ttyinput()`. The n64cart UART input path also folds the common terminal
Delete sequence `ESC [ 3 ~` into erase. Ctrl-C is handled by the tty line
discipline after input is fed into `ttyinput()`.

Pseudo terminals use the existing RetroBSD `sys/kernel/tty_pty.c` driver.
The N64 board config enables them the same way as PIC32, as a kconfig service:

```
service         pty     4
```

That generates `PTY_ENABLED` and `PTY_NUNITS=4`, compiles `tty_pty.o`, and
adds `ptyattach` to `conf_service_init`. The pty driver is not a hardware
`device` and must not appear in `conf_device_init` as `ptydriver`.

N64 assigns pty slave and master character majors locally:

```
slave:  /dev/ttyp0..3   c 8,0..3
master: /dev/ptyp0..3   c 9,0..3
```

The nodes are generated by `sys/mips/n64/devnodes.awk` from `PTY_NUNITS` and
`sys/mips/n64/devmajors.h`, so changing the configured pty count or major numbers
does not require editing a static rootfs manifest. Pty-dependent userland was
kept out of the ROM manifest until pty open/read/write passed on hardware.

The ROM rootfs includes `/bin/ptytest` for that smoke test:

```
ptytest       # test /dev/ptyp0 <-> /dev/ttyp0
ptytest 1     # test /dev/ptyp1 <-> /dev/ttyp1
```

`ptytest` opens the selected master/slave pair, puts the slave in raw mode, and
checks data transfer in both directions. It is intentionally separate from
`smux`; `smux` is enabled only after this minimal pty path passes on hardware.

Hardware smoke-test status on N64:

```
ptytest
ptytest 1
ptytest 2
```

All three commands completed with `/dev/ptypN <-> /dev/ttypN ok`.

`/bin/smux` is built from `src/cmd/smux/retro` and uses `/dev/ptypN` masters
to expose multiplexed sessions over the current serial link. The top-level
`src/cmd/smux` directory also contains a host-side `linux` peer; the N64 build
passes `SMUX_SUBDIRS=retro` so the cartridge rootfs build does not build the
host helper with the N64 cross compiler.

`more` and other job-control aware programs compare `TIOCGPGRP` with
`getpgrp()`. The kernel still implements the historical BSD `getpgrp(pid)`
entry, where pid 0 means the current process, while the installed public
header exposes POSIX `getpgrp(void)`. The shared MIPS libc therefore provides
an explicit `getpgrp.S` wrapper that clears `$a0` before `SYS_getpgrp`. Without
that wrapper the autogenerated raw syscall stub passed a stale register as the
pid, and `more` could receive `ESRCH` after a successful `TIOCGPGRP`.

## Console, Serial, And N64cart Hardware

The current n64cart register block provides the serial UART and RGB LED:

- config identifier: `device "n64cart"`
- compile define: `N64CART_ENABLED`
- physical register base: `0x1fd01000`
- uncached register base: `0xbfd01000`
- control register offset: `0x00`
- RX/TX register offset: `0x04`
- LED control register offset: `0x08`
- RX available bit: `0x01`
- TX free bit: `0x02`
- LED RGB write mask: `N64CART_LED_RGB = 0x00ffffff`

The serial tty driver is `sys/mips/n64/n64cart_uart.c`.
The RGB LED ioctl driver is `sys/mips/n64/n64cart_rgbled.c`.
The stage0 backend is `sys/mips/n64/stage0_n64cart_uart.c`.

`sys/mips/n64/n64pi.c` synchronously serializes PI clients after early boot.
ROM-disk DMA, polling UART, direct USB-controller accesses, and flash accesses
all wait for exclusive ownership of the single PI bus. No cartridge UART
interrupt protocol or firmware change is required.

PI ownership masks interrupts with `splhigh()` and restores the saved CP0
status when the owner leaves.  A PI client must therefore change persistent
CPU interrupt-mask bits only after `n64pi_bus_leave()`.  In particular, the
USB controller is programmed and its pull-up is enabled while PI is owned,
then CART/IP3 is enabled after releasing PI; enabling IP3 inside the critical
section would be undone by the saved-status restore.

Both USB GDB and USB networking require CART/IP3 in every base-priority CP0
status assembled by `mips_intr_enable()` and `mips_user_enter`.  Ordinary USB
networking is masked by `splhigh()` and restored by `splx()`; only the
emergency USB GDB path is allowed to remain live inside kernel critical
sections.

A comparable minimal polling-UART ROM can be built without changing firmware:

```sh
make -C sys/mips BOARD=n64 O=/work/n64-uart-poll \
    N64_BUILD_CONFIG=uartmin-gcc kernel.z64
```

If the board config does not include `n64cart_uart.o`, stage0 links
`stage0_console_null.o` and the kernel can link the weak null console backend.
This allows non-n64cart cartridge support to be added without pretending that
the n64cart registers exist.

Future cartridge-specific hardware must use a separate Config device name and
separate driver files. Do not put new cartridge register assumptions behind
the existing `n64cart` identifier unless the hardware is actually compatible
with this UART/register block.

The n64cart RGB LED register matches the `N64CART_LED_CTRL` register used by
n64cart-manager:

```
physical  0x1fd01008
uncached  0xbfd01008
value     0x00RRGGBB
```

The generic `led_control(mask, on)` hook remains a no-op on N64. The RGB LED is
explicitly controlled through `/dev/rgbled0` so serial activity does not
implicitly change LED state.

`/dev/console` consumes RandNET keyboard input through the N64 SI/Joybus path.
Keep `/dev/ttyS0` enabled as a separate serial login for cartridge access and
debugging.

## Interrupts and serial input

The default serial console input path is timer-polled:

1. `clkstart()` programs CP0 Compare from CP0 Count.
2. CP0 timer interrupts arrive on IP7.
3. The exception handler reprimes Compare.
4. The exception handler polls the n64cart serial tty with
   `n64cart_uart_intr()`.
5. The exception handler polls RandNET keyboard input for the system console.
6. The exception handler calls `cnintr()` for the system console backend.
7. The exception handler calls `hardclock()`.

Polling is the sole supported cartridge UART path. It uses the stock firmware
register block and shares PI through the synchronous `n64pi` owner lock.

Timer-driven kernel callouts also depend on this path.  Functions such as
`sleep(1)` use libc `sleep(3)`, which waits through `select(2)` with a timeout;
that timeout is completed by the kernel callout queue.  On N64, `BASEPRI(ps)`
is defined from the saved CP0 `ST_IE` bit because the VR4300 status register has
interrupt mask bits but no PIC32-style IPL field.  This lets `hardclock()` run
`softclock()` for timer ticks taken at base priority, so `select(2)` timeouts
and other callout users wake without needing a signal.

MI interrupts are scaffolded separately:

- IP2 is enabled in CP0 Status.
- `n64_interrupt_init()` disables all MI interrupt sources initially.
- `n64_video_intr_enable()` enables the VI interrupt only when 640x480
  interlaced mode is active.
- `n64_interrupt_handle_mi()` lets `n64_video_intr()` update interlaced VI
  field registers on the VI interrupt boundary, then acknowledges pending MI
  interrupt bits.
- MI register addresses, interrupt source bits, and write-mask bits live in
  `sys/mips/n64/n64int.h`; `sys/mips/n64/n64int.c` contains the enable/disable/ack
  logic.

At this stage, MI handling is present for future N64 hardware drivers, but
the console does not depend on an MI interrupt source.

## Exception handling and syscalls

The shared MIPS exception vector is copied to the normal MIPS exception vector
locations in low physical memory. All vectors branch to `mips_exception_entry`.

The assembly entry code:

- switches to the kernel `u` area stack for user exceptions
- saves general registers, HI/LO, status, and EPC into a frame
- calls `exception(frame)`
- restores the frame
- returns with `eret`

Syscalls are decoded from the MIPS `syscall` instruction code field. The
first four arguments are taken from `a0`..`a3`; arguments 5 and 6 are read
from the user stack after bounds checking.

On syscall return:

- success returns `u.u_rval` in `v0`
- normal errors return `-1` in `v0` and the errno in `t0`
- `ERESTART` rewinds PC to the original syscall instruction
- `EJUSTRETURN` leaves the user frame as modified by the kernel

N64 keeps a local `ct_ticks` counter on CP0 timer interrupts. The `msec()`
syscall returns `ct_ticks * (1000 / HZ)`, matching the existing PIC32 behavior
instead of returning the old N64 bring-up stub value of zero.

The `nosys()` path also follows PIC32 now: nonexistent syscalls deliver
`SIGSYS`, with `EINVAL` used only when that signal is ignored or held.

User profiling no longer has an empty N64 `addupc()` stub. When `profil(2)` is
enabled, exception return increments the selected user profiling bucket with
the same arithmetic used by PIC32.

## FPU support

Userland is built hard-float. The kernel itself is built soft-float except
for the FPU assembly helpers.

The FPU path is:

1. User code executes an FPU instruction with CP1 disabled.
2. VR4300 raises Coprocessor Unusable for CP1.
3. The exception handler sets `ST_CU1` in the saved user status.
4. On return to user mode, CP1 is enabled.
5. If a later exception enters the kernel with `ST_CU1` set, the kernel saves
   all 32 FPU registers plus FCSR into `u.u_fpu`.
6. Before returning to user mode, the kernel restores FPU state if `ST_CU1`
   is set in the user frame.

`sys/mips/common/fpu.S` contains the shared Malta/N64 FPU helpers:

- `mips_fpu_save`
- `mips_fpu_restore`
- `mips_fpu_clear`

N64 forces CP0 `Status.FR=0` on kernel entry, lazy CP1 enable, and user-mode
return.  That keeps VR4300 in the o32-compatible 32-bit FPR mode expected by
the shared FPU save/restore code and by hard-float N64 userland.

N64 userland uses the MIPS o32 hard-float ABI. The shared MIPS libc
`setjmp`, `_setjmp`, `longjmp`, and `_longjmp` paths therefore save and
restore the FPU register file and FCSR when compiled with
`__mips_hard_float`. The soft-float PIC32 build keeps the smaller historical
`jmp_buf` and does not emit FPU instructions in these routines.

The previous bootstrap `/sbin/init` performed a simple FPU smoke test and
printed:

```
fpu ok
```

After switching to the normal `src/cmd/init`, FPU support remains enabled in
the kernel and N64 userland build flags, but the smoke test should live in a
separate user command instead of inside init.

## Userland build

N64 userland is built from common RetroBSD sources, but with the N64 target
settings:

```
TARGET_PLATFORM=n64
```

The N64 board makefile rebuilds:

- `src/crt0.o` from `src/startup-mips`
- `src/libc.a` and the selected library archive set
- the selected command subset through `src/cmd/Makefile`

The native `/usr/lib/crt0.o` staged for target-side `cc`/`pcc` is separate from
`src/crt0.o`: it is assembled by the N64 native `as` from
`lib/startup/crt0.s`, so target-side links do not depend on a GCC-generated
start object.

The selected source tree subset is controlled by `sys/mips/n64/Makefile.kconf`
using shared makefile filters:

```
SRC_ONLY_LIBS
SRC_ONLY_SUBDIR
CMD_ONLY_SUBDIR
CMD_ONLY_STD
CMD_ONLY_SCRIPT
CMD_ONLY_NSTD
CMD_ONLY_SETUID
CMD_ONLY_OPERATOR
CMD_ONLY_KMEM
CMD_ONLY_TTY
SMUX_SUBDIRS
CMD_BUILD_STRIP
```

PIC32 defaults stay unchanged because empty filter variables select the full
existing lists. N64 passes
`SRC_ONLY_LIBS="startup-mips libc libm libutil libtermlib libcurses libvmf
libreadline libtcl"` and `SRC_ONLY_SUBDIR="cmd"` at the `src/Makefile` level,
then passes explicit `CMD_ONLY_*` values to the command makefile and disables
the host-side `strip` command build with `CMD_BUILD_STRIP=`. For `smux`, N64
passes `SMUX_SUBDIRS=retro` because only the RetroBSD target side belongs in
the cartridge rootfs.

The N64 command subset now starts from the normal RetroBSD command groups and
enables them in safe batches. The first broad batch includes the simple `STD`
commands that build with the N64 target, `egrep`/`expr`, `df`, scripts
`false`/`nohup`/`true`, and selected portable subdirectory commands such as
`awk`, `date`, `diff`, `find`, `fold`, `md5`, `printf`, `sed`, `sysctl`,
`xargs`, `compress`, `chroot`, `mknod`, `mkpasswd`, and `shutdown`. The next
batch pulls in the existing terminal and interpreter libraries needed by
portable user commands: `libcurses`, `libvmf`, `libreadline`, and `libtcl`.
Those libraries enable `emg`, `med`, `pdc`, `setty`, `sl`, `tcl`, and related
interactive tools without adding N64-specific source forks. PIC32/peripheral
tools and commands that need missing runtime support remain excluded. `awk`
pulls in the historical `-lm` dependency, so N64 includes `libm` in the
userland library build.
The manifest includes the extra files produced by those selected install
rules, such as the PCC `cc`/`cpp` aliases, `find` helpers `bigram` and `code`,
and the `updatedb` script, so the cartridge image matches the staged BSD
rootfs instead of silently dropping installed helpers.

`cpp` and `calendar` are included. `cpp` needs libgcc-compatible integer
runtime helpers emitted by GCC for 32-bit MIPS userland, so libc runtime now
provides 64-bit shift helpers and clz/ctz helpers in `src/libc/runtime`.
`calendar` keeps its normal historical behavior: it invokes `/bin/cpp` at
runtime and reads the installed data files from `/usr/share/calendar`.

After building the selected commands, the board makefile invokes the shared
install target with:

```
TARGET_PLATFORM=n64
DESTDIR=sys/mips/n64/rootfs.stage
N64_USER_LDSCRIPT=sys/mips/n64/n64-user.ld
SRC_ONLY_LIBS="startup-mips libc libm libutil libtermlib libcurses libvmf libreadline libtcl"
SRC_ONLY_SUBDIR="cmd"
```

`libm` is included because the historical `awk` build links with `-lm`.
`libutil` is included because the standard `login` binary links against its
utmp/wtmp helpers. On the read-only N64 rootfs those helpers skip accounting
writes when the accounting files cannot be opened for writing.

The installed files are then picked up by the generated rootfs manifest when `fsutil`
creates `rootfs.img`. The command list is intentionally an N64 subset of the
normal `src/cmd` tree; each selected subdirectory command is still installed
by its own existing makefile, and simple one-file commands use the shared
`src/cmd/Makefile` rule.

Those common source directories should not carry N64-only hacks. N64-specific
linking is controlled by `target-n64.mk` and the generated
`sys/mips/n64/n64-user.ld` linker script.

The N64 kernel build also sets the generic hardware sysctl identity:

```
HW_MACHINE_NAME="mips"
HW_MODEL_NAME="NEC VR4300"
```

`HW_MACHINE_NAME` is the architecture class used by `uname -m`; the concrete
N64 CPU model remains in `hw.model`. `uname -a` should therefore end with
`mips`, and `uname -m` should print:

```
mips
```

User executables are linked as static ELF32.  Legacy a.out execution remains
available in the kernel, but the current N64 rootfs installs ELF binaries.

## Linker scripts

The source kernel linker script is:

```
sys/mips/n64/n64.ld.S
```

It includes `layout.h` and is preprocessed into:

```
sys/mips/n64/n64.ld.S
```

The source user linker script is:

```
sys/mips/n64/user/user.ld.S
```

It includes `layout.h` and is preprocessed into:

```
sys/mips/n64/n64-user.ld
```

The rootfs installs this user script as
`/usr/lib/ldscripts/elf32-bigmips.ld`.  The old flat
`/usr/lib/elf32-mips.ld` path is not installed.

The preprocessing rule uses `-undef` so the compiler's predefined `mips`
macro cannot corrupt `OUTPUT_ARCH(mips)`.

`target-n64.mk` requires `N64_USER_LDSCRIPT` to be provided by the board
build. This prevents accidental direct builds from using a stale static
linker script.

## Tracing and diagnostics

The N64 build supports optional syscall tracing:

```
make -C sys/mips BOARD=n64 N64_TRACE=1 kernel.z64
```

The trace currently prints a limited number of syscall entry/exit messages
from the exception handler. It is intended for bring-up debugging only.

### Native PCC long-run fault diagnostics

The 2026-07-29 real-hardware investigation added diagnostic coverage for a
rare native PCC smoke failure on N64.  The first complete fault capture showed
that `/usr/bin/ld` reached `malloc_insert_free()` through `free()` with the
invalid pointer `0x25`, then raised an address exception while reading
`0x21`.  The recorded executable PTE, live TLB entry, physical instruction
address, and cached/uncached instruction words all agreed.  VM validation
passed, no page had been swapped, and zswap reported no error.  Repeated
post-fault hashes of `pcc`, `cpp`, and `ccom` also matched the build artifacts.
This localizes the next investigation to corruption of runtime state; it does
not yet identify where the bad pointer originated.

The following diagnostic facilities are present:

- The PCC minimal rootfs installs `/usr/bin/md5`, so the compiler driver and
  helper binaries can be checked before and immediately after a fault.
- A user fault temporarily mirrors the primary console output to the n64cart
  debug UART.  A VI-console build therefore records the complete
  `N64_USER_FAULT` report in the serial log without changing the configured
  console.
- The report includes all saved GPR halves, EPC, Cause, Status, BadVAddr,
  process layout, VM/pmap/zswap counters, the queried PTE, the matching live
  TLB entry, and cached/uncached physical instruction words.
- The report also reads the current user stack through the active pmap and
  prints bounded stack words plus the frame-pointer chain.  Each entry includes
  its physical address and cached/uncached values, allowing the saved caller
  return address and a possible cache-coherency discrepancy to be identified
  on the next occurrence.

These changes are diagnostic only.  They do not retry an operation, validate
or suppress `free()`, alter allocator policy, change VM or pmap behavior, or
modify PCC-generated code.  GCC- and PCC-built N64 kernels pass their build
gates, and the exact N64 rootfs passes native PCC smoke and process-reaping
checks under Malta64/QEMU.

At the time of this update, continued real-hardware runs on both N64 and
Creator Ci20 are proceeding normally without another failure.  This is a
current validation observation, not a declaration that the rare N64 fault is
resolved; a future occurrence must retain the full `N64_USER_FAULT stack` and
`N64_USER_FAULT frame` output.

A later N64 capture localized a native `ccom` failure more tightly.  The
fault occurred in `yyparse` at `0x00403d9c`, while loading through `a0`.
The exact linked instruction stream is:

```
00403d8c  sll    a0,v0,1
00403d90  lui    at,0x48
00403d94  addiu  at,at,-8968
00403d98  addu   a0,at,a0
00403d9c  lh     a2,0(a0)
```

The saved registers were `v0=0x20`, `at=0x0047dcf8`, and `a0=0x40`.
`0x0047dcf8` is `yycheck`, so sequential execution requires the load address
to be `0x0047dd38`.  No control transfer in the linked binary targets
`0x00403d9c`; the observed register state therefore cannot result from
retiring the preceding `addu`.  The executable PTE and live TLB entry still
agreed, the instruction at EPC agreed through cached and uncached physical
aliases, VM validation passed, and neither swap nor zswap had been used.
This rules out an ordinary parser-table bounds failure and localizes the next
gate to exception return versus stale instruction-cache state on physical
VR4300 hardware.

The common MIPS exception diagnostics now retain the immediately preceding
full exception, including EPC, Cause, `at`, `v0`, and `a0`.  A user fault also
dumps a nine-word instruction window around EPC.  Fast-refill diagnostics are
published only after a resident PTE has actually been installed, so a slow VM
fault no longer overwrites the last successful refill immediately before the
failure.  These remain observation-only changes: they do not retry the
instruction, alter scheduling, change cache policy, or modify PCC output.

## Disabled board-call ABI

PIC32 implements `ucall`, `ufetch`, and `ustore` as privileged board/autoconfig
helpers for direct kernel routine calls and PIC32 flash/peripheral register
access. N64 does not reuse that ABI. The N64 implementations currently return
`ENOSYS`.

N64 cartridge and console hardware should be exposed through named Config
devices plus narrow driver interfaces, such as `/dev/ttyS0`,
`/dev/rgbled0`, block devices, ioctls, or sysctl nodes. If a future debugger or
loader really needs raw memory or MMIO access, add a separate N64-specific
interface behind an explicit config option instead of enabling the PIC32 calls
unchanged.

Useful boot diagnostics:

- `ReBSD N64 stage0`: stage0 is running.
- `kernel blob size=...`: embedded kernel ELF size.
- `jump kernel entry=0x80001000`: stage0 validated and loaded the kernel.
- `ReBSD N64 kernel entry`: kernel `startup()` is running.
- `rdram size=...`: final kernel RDRAM detection.
- `n64romdisk: rootfs offset=... size=... magic=...`: rootfs TOC entry found.

If the rootfs TOC entry is missing, the romdisk driver prints:

```
n64romdisk: rootfs.img not found in ROM TOC
```

and root mount fails.

## Files most likely touched during N64 bring-up

Platform core:

- `sys/mips/common/startup.S`
- `sys/mips/common/exception_entry.S`
- `sys/mips/common/exception.c`
- `sys/mips/common/fpu.S`
- `sys/mips/n64/layout.h`
- `sys/mips/n64/machparam.h`
- `sys/mips/n64/machdep.c`

Boot and ROM:

- `sys/mips/n64/stage0.S`
- `sys/mips/n64/stage0_loader.c`
- `sys/mips/n64/stage0_kernel_blob.S`
- `sys/mips/n64/stage0_linker.ld`
- `sys/mips/n64/rompak.c`
- `sys/mips/n64/romdisk_machdep.c`

Console and interrupts:

- `sys/kernel/cons.c`
- `sys/mips/n64/console.h`
- `sys/mips/n64/n64cart_uart.c`
- `sys/mips/n64/n64cart_rgbled.c`
- `sys/mips/n64/n64cart_rgbled.h`
- `sys/mips/n64/console_null.c`
- `sys/mips/n64/n64int.c`
- `sys/mips/n64/si.c`
- `sys/mips/n64/si.h`
- `sys/mips/n64/joybus.c`
- `sys/mips/n64/joybus.h`
- `sys/mips/n64/clock.c`

N64 userland additions:

- `src/cmd/fbset/`
- `src/cmd/n64input/`
- `src/cmd/ptytest/`
- `src/cmd/rgbled/`
- `src/cmd/smux/retro`

Build and generated data:

- `sys/mips/n64/Makefile`
- `sys/mips/n64/Makefile.kconf`
- `sys/mips/n64/Config`
- `sys/mips/rootfs.manifest`
- `sys/mips/n64/devnodes.awk`
- `target-n64.mk`

## Current limitations

- Rootfs is intentionally read-only.
- The kernel and user ABI remain 32-bit o32; a 64-bit kernel/userland ABI is
  outside the current low-memory N64 target.
- Swap is RAM-backed, not persistent storage.
- Reboot is a software restart through the resident stage0 image, not a full
  hardware reset.
- `/dev/mem`, `/dev/kmem`, `ucall`, `ufetch`, and `ustore` are intentionally
  disabled on N64.
- Console input and cartridge UART input are timer-polled; the stock cartridge
  firmware remains unchanged.
- The n64cart UART backend only works on cartridges with the matching
  register block.
- Joybus keyboard, mouse, and joypad drivers are built and exposed through
  `/dev`, but their hardware smoke-test is still pending.
- The ROM rootfs now contains a broader first batch of normal BSD `/bin` and
  `/sbin` tools, `man`/`more`, generated `/dev` nodes, and static config
  files.
- Writable N64cart ROMFS is available through `/dev/cartflash0` and
  `mount -t romfs`; SD and other cartridge storage drivers are not implemented
  yet.

## Bring-up rules

- Keep N64-only changes in `sys/mips/n64` or N64 target/build files when possible.
- Do not patch common `src/cmd`, `src/libc`, or `sys/kernel` code for a bug
  that is actually caused by N64 machine code.
- If a file is generated, update the generator or source manifest, not the
  generated file.
- Keep cartridge hardware behind explicit Config device identifiers.
- Rebuild `kernel.z64` after layout, rootfs, device, or linker script changes.
- Test on hardware before committing risky platform changes.
