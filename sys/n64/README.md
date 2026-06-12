# RetroBSD N64 port notes

This document describes the current Nintendo 64 port state, how the ROM is
built, and how the early platform code works on real N64 hardware.

The port targets the Nintendo 64 VR4300 as a 32-bit big-endian MIPS III
machine using the o32 ABI. The current hardware target is a stock N64 with
4 MiB base RDRAM or 8 MiB with the Expansion Pak. The current cartridge
hardware target is the `n64cart` UART/RGB LED register block; display output
uses the N64 VI framebuffer.

## Current status

The current port boots a minimal RetroBSD system from a cartridge ROM image:

- stage0 starts from the N64 ROM and loads an ELF32 big-endian kernel blob.
- The kernel installs exception vectors and a wired TLB mapping for userland.
- RDRAM size is detected at startup and printed by the kernel.
- Root is a read-only UFS romdisk stored in the ROM image.
- Swap is a RAM-backed block device.
- `/dev/console` is a real tty-backed console with VI framebuffer output.
- `/dev/tty` is the controlling tty major.
- `/dev/ttyS0` is the n64cart serial tty.
- `/dev/rgbled0` controls the n64cart RGB LED through ioctl.
- `/dev/fb0` exposes the current 16-bit framebuffer, mode ioctls, and a
  fixed uncached user mapping.
- `/dev/romdisk`, `/dev/swap`, `/dev/null`, `/dev/zero`, `/dev/ttyS0`,
  `/dev/rgbled0`, `/dev/fb0`, and the pty nodes are generated into the root
  filesystem from kernel device definitions.
- Userland is built from the normal `src/cmd` tree as a.out binaries linked
  for the N64 user address window.
- The N64 rootfs selects `init`, `getty`, `login`, `sh`, `ls`, and a small
  basic command set through the shared `src/cmd/Makefile` install flow.
- The normal boot path runs `/etc/rc`, starts `/libexec/getty` for the enabled
  `/etc/ttys` lines, and logs in through `/bin/login`.
- Userland FPU is enabled and the kernel saves/restores FPU state.

Known hardware smoke test on a real 8 MiB system, verified 2026-06-12 before
the expanded command set:

```
RetroBSD N64 stage0
kernel blob size=0x00023020
jump kernel entry=0x80001000
RetroBSD N64 kernel entry
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

RetroBSD/N64 (Amnesiac) (console)

login: root
RetroBSD/N64 early rootfs

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

## Build entry points

Preferred top-level N64 build:

```
make -C sys/n64 all
```

Useful specific targets:

```
make -C sys/n64 kernel.z64
make -C sys/n64 preflight.z64
make -C sys/n64 reconfig
make -C sys/n64 clean
make -C sys/n64 clean-all
```

The board build directory is `sys/n64/nintendo64`.

Generated outputs:

- `sys/n64/nintendo64/unix.elf`: RetroBSD kernel ELF.
- `sys/n64/nintendo64/kernel.z64`: bootable ROM with the real kernel.
- `sys/n64/nintendo64/preflight.z64`: bootable ROM with the preflight kernel.
- `sys/n64/nintendo64/rootfs.img`: generated UFS root filesystem.
- `sys/n64/nintendo64/n64.ld`: generated kernel linker script.
- `sys/n64/nintendo64/n64-user.ld`: generated user linker script.
- `sys/n64/nintendo64/n64-userland.stamp`: local build stamp for the
  selected N64 user commands.

The N64 clean target also descends through the selected shared userland build
with the same N64 filters. Generated command artifacts such as
`src/cmd/rgbled/rgbled`, `src/cmd/ptytest/ptytest`, object files,
disassemblies, and formatted catman pages are removed by the command makefiles,
not by hand.

Do not edit generated files by hand. Edit the source files listed below and
rerun `make -C sys/n64 reconfig` or `make -C sys/n64 all`.

## Generated files and source files

The N64 makefile is generated by the existing RetroBSD kconfig tool:

- source: `sys/n64/Makefile.kconf`
- source: `sys/n64/files.kconf`
- source: `sys/n64/devices.kconf`
- source: `sys/n64/nintendo64/Config`
- generated: `sys/n64/nintendo64/Makefile`
- generated: `sys/n64/nintendo64/ioconf.c`
- generated: `sys/n64/nintendo64/swapunix.c`

The linker scripts are also generated for the board build:

- source: `sys/n64/layout.h`
- source: `sys/n64/n64.ld`
- source: `sys/n64/user/user.ld.S`
- generated: `sys/n64/nintendo64/n64.ld`
- generated: `sys/n64/nintendo64/n64-user.ld`

The root filesystem manifest is generated from a static base manifest plus
device nodes derived from the kernel headers:

- source: `sys/n64/rootfs.manifest`
- source: `sys/n64/rootfs/`
- source: `sys/n64/devnodes.awk`
- source: `sys/n64/romdisk.h`
- source: `sys/n64/ramswap.h`
- source: `sys/n64/devmajors.h`
- source: `sys/include/conf.h`
- generated: `sys/n64/nintendo64/rootfs.devnodes.manifest`
- generated: `sys/n64/nintendo64/rootfs.generated.manifest`
- generated: `sys/n64/nintendo64/rootfs.img`

This is intentional: if major/minor values change in the kernel, `/dev` is
regenerated from the same definitions instead of drifting.

## ROM image layout

The ROM image is produced by `n64tool` with a TOC:

```
n64tool --toc --title "RETROBSD N64" \
    --output kernel.z64 \
    --align 256 kernel_stage0.stripped.elf \
    --align 1024 rootfs.img
```

For `preflight.z64`, `stage0.stripped.elf` is used instead of
`kernel_stage0.stripped.elf`.

The stage0 ELF contains the kernel ELF as an embedded blob through
`stage0_kernel_blob.S`. The root filesystem is a separate TOC entry named
`rootfs.img`.

The kernel ROM reader uses uncached PI ROM space at `0xb0000000` and searches
the first MiB of ROM for a TOC. It finds the `rootfs.img` entry by name and
uses the recorded offset/size as the backing store for the romdisk block
device.

## Login path

The N64 root filesystem uses the standard RetroBSD multi-user path instead of
an N64-only shell jump:

1. the kernel starts `/sbin/init`;
2. the copied `icode` passes `"-"` as the init option string, matching the
   PIC32 bootstrap convention and not requesting `-s`;
3. `init` runs `/etc/rc`;
4. `init` reads `/etc/ttys`;
5. the enabled `console` and `ttyS0` entries start
   `/libexec/getty std.default`;
6. `getty` opens `/dev/console` or `/dev/ttyS0`, prints the login prompt, and
   execs `/bin/login`;
7. `login` authenticates against `/etc/passwd`, reads `/etc/group`, prints
   `/etc/motd`, and starts `/bin/sh` as a login shell.

This intentionally leaves `src/cmd/init` behavior unchanged. If no getty lines
are enabled, `init` can still fall back to the single-user path after the
multi-user loop has no children to supervise; N64 avoids that by enabling secure
`console` and `ttyS0` lines in `/etc/ttys`.

Hardware smoke test on real n64cart hardware shows both login paths coming up:

```
RetroBSD/N64 (Amnesiac) (ttyS0)
login:

RetroBSD/N64 (Amnesiac) (console)
login:
```

The first N64 account database is intentionally small because the cartridge
rootfs is read-only:

- `root` has an empty password and `/root` as its home directory.
- `console` and `ttyS0` are marked `secure` in `/etc/ttys`, so root login is
  allowed there.
- `/var/run/utmp`, `/var/log/wtmp`, and `/var/log/lastlog` are not writable on
  the ROM rootfs. The existing `login`/`libutil` code tolerates this by simply
  skipping accounting writes when those files cannot be opened for writing.

## Signals

N64 uses the same MIPS user signal ABI shape as PIC32. `sendsig()` builds a
user stack frame with:

- four argument words for the trampoline call area;
- a `struct sigcontext` containing the interrupted user registers, stack,
  return address, HI/LO, program counter, signal mask, and alternate-stack
  state.

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
path does not overwrite the restored frame.

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
   - prints `RetroBSD N64 kernel entry`
   - clears the fixed `u0..u_end` user-area pages that live outside ELF `.bss`
   - installs exception vectors
   - installs the wired user TLB entry
   - initializes MI interrupt masks
   - enables CP0 interrupt masks for MI and timer interrupts
   - detects and prints RDRAM size
   - forces the root filesystem read-only with `RB_RDONLY`

## Reboot Path

`boot()`/`reboot(2)` on N64 no longer only prints the reboot request and spins.
For a normal reboot, the kernel syncs pending buffers, disables N64 interrupt
sources, and jumps back to the resident stage0 entry at `0x80300000`.
Because the cartridge root filesystem is mounted read-only, the N64 reboot
path does not force the root superblock dirty before `sync()`.

This is a software restart through the ROM-loaded stage0 image, not a full
console hardware reset. `halt`/`poweroff` requests disable interrupts and stop
the CPU in a `wait` loop.

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

The first-stage memory map is centralized in `sys/n64/layout.h`.

4 MiB system:

```
0x00000000..0x000fffff  kernel, vectors, u areas
0x00100000..0x002fffff  wired kuseg user window
0x00340000..0x0037ffff  320x240x16 framebuffer reserve
0x00380000..0x003fffff  RAM swap fallback
```

8 MiB system:

```
0x00000000..0x000fffff  kernel, vectors, u areas
0x00100000..0x002fffff  wired kuseg user window
0x00400000..0x0049ffff  max 640x480x16 framebuffer reserve
0x004a0000..0x007fffff  Expansion Pak RAM swap
```

Important constants:

- `N64_KERNEL_LOAD_VADDR`: kernel link/load address, `0x80001000`.
- `N64_KERNEL_RESERVED`: first 1 MiB reserved for kernel/vectors/u areas.
- `N64_USER_VADDR_START`: user virtual base, `0x00400000`.
- `N64_USER_MAXMEM`: 2 MiB user address window.
- `N64_USER_PHYS_START`: physical backing for user memory, `0x00100000`.
- `N64_BASE_SWAP_PHYS_START`: 4 MiB fallback swap base, `0x00380000`.
- `N64_BASE_FB_PHYS_START`: 4 MiB framebuffer reserve base, `0x00340000`.
- `N64_EXPANSION_FB_PHYS_START`: 8 MiB framebuffer reserve base,
  `0x00400000`.
- `N64_EXPANSION_SWAP_PHYS_START`: 8 MiB swap base after the maximum
  framebuffer reserve, `0x004a0000`.
- `N64_FB_USER_VADDR_START`: uncached framebuffer user mapping base,
  `0x00600000`.

`machparam.h` maps the old RetroBSD platform names onto this layout:

- `KERNEL_DATA_START`
- `KERNEL_DATA_END`
- `USER_DATA_START`
- `USER_DATA_END`
- `MAXMEM`

This keeps the N64 memory model local to `sys/n64` while common kernel code
continues to use the normal RetroBSD names.

## RDRAM detection

RDRAM size detection is in `sys/n64/memory.c`.

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

The N64 kernel installs wired TLB entries for the normal user address window
and the framebuffer mapping. Entry 0 maps a 2 MiB user window:

```
virtual  0x00400000..0x005fffff
physical 0x00100000..0x002fffff
```

The entry uses two 1 MiB pages through `TLB_PAGEMASK_1M`.

The following wired entries map `/dev/fb0` at `N64_FB_USER_VADDR_START`
(`0x00600000`) with uncached 64 KiB pages. The usable byte count is reported
by `N64FBIOC_GETMAP`; the physical reserve is rounded up to the TLB pair size
so the user-visible mapping never overlaps RAM swap:

```
4 MiB: 0x00600000..0x0063ffff -> 0x00340000..0x0037ffff
8 MiB: 0x00600000..0x0069ffff -> 0x00400000..0x0049ffff
```

The kernel does not currently implement a full VM system for N64. The wired
process window is the fixed first version of the user address space. `copyin`,
`copyout`, and `baduaddr` accept normal process memory and the current usable
framebuffer byte range, but the framebuffer is not part of process heap/stack
or swap.

On successful `exec` and on process changes, the N64 machine layer flushes
the user data/instruction cache range so newly copied user code is executable
on the VR4300.

## Root filesystem

Root is a read-only UFS image stored in ROM as `rootfs.img`.

The root image is built from:

- `sys/n64/rootfs/`
- `sys/n64/rootfs.manifest`
- generated `/dev` nodes
- selected user commands installed into the staging tree through the normal
  RetroBSD `src/Makefile` install flow

This follows the same install model as the top-level PIC32 build: commands are
built from `src/cmd/*`, installed into a `DESTDIR`, and then `fsutil` creates
the filesystem from the staged tree plus a manifest. The N64 build keeps an
explicit command subset for the cartridge rootfs, but it does not copy command
binaries directly out of `src/cmd`.

The current manifest includes:

```
/bin/[
/bin/apropos
/bin/cat
/bin/chmod
/bin/cp
/bin/echo
/bin/env
/bin/fbset
/bin/groups
/bin/hostname
/bin/id
/bin/kill
/bin/login
/bin/ls
/bin/man
/bin/mkdir
/bin/more
/bin/mv
/bin/pwd
/bin/ptytest
/bin/rgbled
/bin/rm
/bin/rmdir
/bin/sh
/bin/smux
/bin/sleep
/bin/stty
/bin/test
/bin/uname
/bin/whatis
/bin/whoami
/.profile
/etc/fstab
/etc/gettytab
/etc/group
/etc/motd
/etc/passwd
/etc/profile
/etc/rc
/etc/ttys
/libexec/getty
/root/.profile
/share/misc/more.help
/share/man/whatis
/share/man/cat1/fbset.0
/share/man/cat1/groups.0
/share/man/cat1/hostname.0
/share/man/cat1/id.0
/share/man/cat1/ptytest.0
/share/man/cat1/rgbled.0
/share/man/cat1/stty.0
/share/man/cat1/test.0
/share/man/cat1/uname.0
/share/man/cat1/whoami.0
/sbin/chown
/sbin/fsck
/sbin/init
/sbin/mount
/sbin/reboot
/sbin/umount
```

The staging tree may contain extra files installed by selected command
makefiles, for example `reboot` installs `halt`, `fastboot`, `poweroff`, and
`bootloader` aliases. Those files do not enter `rootfs.img` until
`sys/n64/rootfs.manifest` lists them.

`man`, `apropos`, and `whatis` are included with the cat pages that are
installed by the selected command makefiles. The N64 build generates
`/share/man/whatis` from the staged cat pages with the existing
`src/man/makewhatis.sed` script before creating `rootfs.img`; the database is
not checked in as a static file. The N64 userland build sets `GROFF_NO_SGR=1`
so host `nroff` emits the classic overstrike format expected by the existing
manual index script.

`man` uses `/bin/more -s` as the default pager on an interactive tty, so
`/bin/more` and `/share/misc/more.help` are part of the ROM rootfs. For the
first N64 rootfs, `/etc/profile`, `/.profile`, and `/root/.profile` set
`PAGER=/bin/cat` so manual pages print directly instead of depending on the
interactive pager. The same profiles set `PATH=/bin:/sbin`, which makes the
selected `/sbin` tools visible from the shell prompt.

The rootfs size defaults to 4096 KiB. The image stays in cartridge ROM and is
not preloaded into RDRAM:

```
N64_ROOTFS_KBYTES ?= 4096
```

It can be overridden on the make command line if the root filesystem needs to
grow:

```
make -C sys/n64 N64_ROOTFS_KBYTES=8192 kernel.z64
```

The romdisk block driver is read-only. Attempts to open it for write return
`EROFS`; write strategies also fail with `EROFS`.

## Block devices

Block major 0 is the ROM-backed root disk:

```
/dev/romdisk  b 0,0
```

Block major 1 is RAM-backed swap:

```
/dev/swap     b 1,0
```

Swap sizing:

- 4 MiB system: top 512 KiB of base RDRAM.
- 8 MiB system: Expansion Pak RAM after the reserved 640x480x16 framebuffer.

The printed boot sizes therefore differ by installed RDRAM:

```
4 MiB: swap size = 512 kbytes
8 MiB: swap size = 3456 kbytes
```

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
/dev/ttyp0    c 8,0
/dev/ptyp0    c 9,0
```

`/dev/console` is a normal RetroBSD tty endpoint backed by `sys/n64/cons.c`.
It uses `sys/n64/video_console.c` for VI framebuffer output. The framebuffer
console does not consume n64cart UART input; serial login input belongs to
`/dev/ttyS0`. When `n64cart` is configured, console output is also mirrored to
the UART as a debug stream.

The text console draws inside a 5% safe area to keep characters away from CRT
or capture-device overscan. This margin applies only to `video_console.c`;
`/dev/fb0` still exposes the full framebuffer.

`/dev/tty` is implemented through the standard `tty_tty` cdev entry and
therefore resolves to the controlling tty for the shell.

`/dev/ttyS0` is a normal tty line for the n64cart serial UART. It has its own
`struct tty`, line discipline, cdev entry, and `/etc/ttys` login line. Because
the n64cart UART does not currently raise CPU interrupts, the CP0 timer path
polls it and feeds received bytes to `ttyinput()`.

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

`/dev/fb0` is the N64 framebuffer character device. It exposes the current
16-bit RGBA5551 framebuffer through read/write, mode ioctls, and a fixed
uncached user mapping:

```
N64FBIOC_GETINFO   struct n64fb_info
N64FBIOC_SETMODE   struct n64fb_mode
N64FBIOC_GETMAP    struct n64fb_map
```

`N64FBIOC_GETMAP` returns `vaddr`, `bytes`, and `reserved_bytes`. `bytes` is
the current usable framebuffer length for the selected mode; `reserved_bytes`
is the TLB-rounded reserve. Programs should write only the `bytes` range at
`vaddr`.

The default framebuffer mode is selected from detected RDRAM: 4 MiB systems
start in 320x240x16, while 8 MiB systems start in 640x480x16. On 8 MiB
systems, the Expansion Pak framebuffer reserve is large enough to switch
between 320x240 and 640x480 at runtime:

```
fbset          # print current framebuffer mode
fbset 320x240  # select progressive 320x240
fbset 640x480  # select interlaced 640x480, Expansion Pak only
fbset fill 0x001f  # fill through the fixed user framebuffer mapping
```

The fixed framebuffer mapping and `fbset fill` path were hardware
smoke-tested on an 8 MiB N64 on 2026-06-12.

The VI setup reads the IPL TV type byte at `0xa4000009` and chooses PAL, NTSC,
or MPAL timing. PAL uses the PAL timing registers with a centered 640x480
active area, so the framebuffer size stays 320x240 or 640x480 rather than
becoming 640x576.

N64 does not expose `/dev/mem` or `/dev/kmem` in the ROM rootfs. Character
major 1 is present only for `/dev/null` and `/dev/zero`; minors 0 and 1 return
`EINVAL` if opened manually. The `kmemdev()` syscall returns `NODEV`, and
`iskmemdev()` returns false for every device. This is intentional for the
first N64 port: userland should not depend on direct kernel memory access.

The console tty settings are initialized with echo, CR/LF mapping, erase,
kill, and control-character echo behavior. Ctrl-C is handled by the tty line
discipline after `cnintr()` feeds input into `ttyinput()`.

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

The nodes are generated by `sys/n64/devnodes.awk` from `PTY_NUNITS` and
`sys/n64/devmajors.h`, so changing the configured pty count or major numbers
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

The serial tty driver is `sys/n64/n64cart_uart.c`.
The RGB LED ioctl driver is `sys/n64/n64cart_rgbled.c`.
The stage0 backend is `sys/n64/stage0_n64cart_uart.c`.

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

Current transition note: `/dev/console` has N64 framebuffer output but no N64
keyboard/controller input backend yet. Keep `/dev/ttyS0` enabled for login and
interactive input until a real system-console input driver exists.

## Interrupts and timer-driven console input

The current serial console input path is timer-polled:

1. `clkstart()` programs CP0 Compare from CP0 Count.
2. CP0 timer interrupts arrive on IP7.
3. The exception handler reprimes Compare.
4. The exception handler polls the n64cart serial tty with
   `n64cart_uart_intr()`.
5. The exception handler calls `cnintr()` for the system console backend.
6. The exception handler calls `hardclock()`.

This is enough for interactive shell input on `/dev/ttyS0` even though the
n64cart UART does not currently provide a real interrupt line to the CPU.

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
  `sys/n64/n64int.h`; `sys/n64/n64int.c` contains the enable/disable/ack
  logic.

At this stage, MI handling is present for future N64 hardware drivers, but
the console does not depend on an MI interrupt source.

## Exception handling and syscalls

The N64 exception vector is copied to the normal MIPS exception vector
locations in low physical memory. All vectors branch to `n64_exception_entry`.

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

`sys/n64/fpu.S` contains:

- `n64_fpu_save`
- `n64_fpu_restore`
- `n64_fpu_clear`

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

- `src/startup-mips/crt0.o`
- `src/libc.a`
- the selected command subset through `src/cmd/Makefile`

The selected source tree subset is controlled by `sys/n64/Makefile.kconf`
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
existing lists. N64 passes `SRC_ONLY_LIBS="startup-mips libc libutil"` and
`SRC_ONLY_SUBDIR="cmd"` at the `src/Makefile` level, then passes explicit
`CMD_ONLY_*` values to the command makefile and disables the host-side `strip`
command build with `CMD_BUILD_STRIP=`. For `smux`, N64 passes
`SMUX_SUBDIRS=retro` because only the RetroBSD target side belongs in the
cartridge rootfs.

After building the selected commands, the board makefile invokes the shared
install target with:

```
TARGET_PLATFORM=n64
DESTDIR=sys/n64/nintendo64/rootfs.stage
N64_USER_LDSCRIPT=sys/n64/nintendo64/n64-user.ld
SRC_ONLY_LIBS="startup-mips libc libutil"
SRC_ONLY_SUBDIR="cmd"
```

`libutil` is included because the standard `login` binary links against its
utmp/wtmp helpers. On the read-only N64 rootfs those helpers skip accounting
writes when the accounting files cannot be opened for writing.

The installed files are then picked up by `rootfs.manifest` when `fsutil`
creates `rootfs.img`. The command list is intentionally an N64 subset of the
normal `src/cmd` tree; each selected subdirectory command is still installed
by its own existing makefile, and simple one-file commands use the shared
`src/cmd/Makefile` rule.

Those common source directories should not carry N64-only hacks. N64-specific
linking is controlled by `target-n64.mk` and the generated
`sys/n64/nintendo64/n64-user.ld` linker script.

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

User executables are linked as ELF first and then converted to a.out with
`tools/elf2aout/elf2aout`, matching the existing RetroBSD userland format.

## Linker scripts

The source kernel linker script is:

```
sys/n64/n64.ld
```

It includes `layout.h` and is preprocessed into:

```
sys/n64/nintendo64/n64.ld
```

The source user linker script is:

```
sys/n64/user/user.ld.S
```

It includes `layout.h` and is preprocessed into:

```
sys/n64/nintendo64/n64-user.ld
```

The preprocessing rule uses `-undef` so the compiler's predefined `mips`
macro cannot corrupt `OUTPUT_ARCH(mips)`.

`target-n64.mk` requires `N64_USER_LDSCRIPT` to be provided by the board
build. This prevents accidental direct builds from using a stale static
linker script.

## Tracing and diagnostics

The N64 build supports optional syscall tracing:

```
make -C sys/n64 N64_TRACE=1 kernel.z64
```

The trace currently prints a limited number of syscall entry/exit messages
from the exception handler. It is intended for bring-up debugging only.

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

- `RetroBSD N64 stage0`: stage0 is running.
- `kernel blob size=...`: embedded kernel ELF size.
- `jump kernel entry=0x80001000`: stage0 validated and loaded the kernel.
- `RetroBSD N64 kernel entry`: kernel `startup()` is running.
- `rdram size=...`: final kernel RDRAM detection.
- `n64romdisk: rootfs offset=... size=... magic=...`: rootfs TOC entry found.

If the rootfs TOC entry is missing, the romdisk driver prints:

```
n64romdisk: rootfs.img not found in ROM TOC
```

and root mount fails.

## Files most likely touched during N64 bring-up

Platform core:

- `sys/n64/layout.h`
- `sys/n64/machparam.h`
- `sys/n64/machdep.c`
- `sys/n64/exception.c`
- `sys/n64/exception_entry.S`
- `sys/n64/startup.S`
- `sys/n64/fpu.S`

Boot and ROM:

- `sys/n64/stage0.S`
- `sys/n64/stage0_loader.c`
- `sys/n64/stage0_kernel_blob.S`
- `sys/n64/stage0_linker.ld`
- `sys/n64/rompak.c`
- `sys/n64/romdisk.c`

Console and interrupts:

- `sys/n64/cons.c`
- `sys/n64/n64cart_uart.c`
- `sys/n64/n64cart_rgbled.c`
- `sys/n64/n64cart_rgbled.h`
- `sys/n64/console_null.c`
- `sys/n64/n64int.c`
- `sys/n64/clock.c`

N64 userland additions:

- `src/cmd/ptytest/`
- `src/cmd/rgbled/`
- `src/cmd/smux/retro`

Build and generated data:

- `sys/n64/Makefile`
- `sys/n64/Makefile.kconf`
- `sys/n64/nintendo64/Config`
- `sys/n64/rootfs.manifest`
- `sys/n64/devnodes.awk`
- `target-n64.mk`

## Current limitations

- Rootfs is intentionally read-only.
- The process address space is one fixed 2 MiB wired TLB mapping, plus the
  fixed uncached `/dev/fb0` mapping.
- Swap is RAM-backed, not persistent storage.
- Reboot is a software restart through the resident stage0 image, not a full
  hardware reset.
- `/dev/mem`, `/dev/kmem`, `ucall`, `ufetch`, and `ustore` are intentionally
  disabled on N64.
- Console input is timer-polled, not driven by a UART interrupt.
- The n64cart UART backend only works on cartridges with the matching
  register block.
- Only a small rootfs is present: `init`, `getty`, `login`, `sh`, `ls`,
  selected basic `/bin` tools, `man`/`more`, selected `/sbin` tools,
  generated `/dev` nodes, and static config files.
- No display, controller, filesystem-writeback, SD, or other cartridge
  storage drivers are implemented yet.

## Bring-up rules

- Keep N64-only changes in `sys/n64` or N64 target/build files when possible.
- Do not patch common `src/cmd`, `src/libc`, or `sys/kernel` code for a bug
  that is actually caused by N64 machine code.
- If a file is generated, update the generator or source manifest, not the
  generated file.
- Keep cartridge hardware behind explicit Config device identifiers.
- Rebuild `kernel.z64` after layout, rootfs, device, or linker script changes.
- Test on hardware before committing risky platform changes.
