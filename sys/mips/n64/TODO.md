# ReBSD N64 TODO

This file tracks the next N64 porting steps. Keep the N64 application and
rootfs build aligned with the existing RetroBSD/PIC32 build flow instead of
adding a separate hand-copied application path.

## Application and Rootfs Build

- [x] Reuse the existing top-level/PIC32 model for userland:
  - build userland through `src/Makefile`
  - install userland through `make -C src install DESTDIR=...`
  - build the filesystem image from the staged tree with `fsutil` and a
    manifest
- [x] Generalize `src/Makefile` without changing PIC32 defaults:
  - introduce overridable source subdir/library lists
  - keep the current default lists equivalent to the existing PIC32 behavior
  - let N64 request only the source subdirs/libraries it can currently build
- [x] Generalize `src/cmd/Makefile` without changing PIC32 defaults:
  - keep canonical command groups in one place: `SUBDIR`, `STD`, `SCRIPT`,
    `SETUID`, `OPERATOR`, `KMEM`, and `TTY`
  - add platform filter variables such as command include/exclude lists
  - make N64 use those filters instead of maintaining a second independent
    command build list
- [x] Change the N64 board build to stage applications through the shared
  `src`/`src/cmd` install flow:
  - remove the N64-only per-command build loop from `sys/mips/n64/Makefile.kconf`
  - call the shared `src`/`src/cmd` install path with
    `TARGET_PLATFORM=n64`, `DESTDIR=rootfs.stage`, and the generated
    `N64_USER_LDSCRIPT`
  - keep `sys/mips/rootfs.manifest` as the shared source of which staged files
    are included in the MIPS ROM rootfs; board-specific manifests add devices
    and board-only entries
- [x] Move shared rootfs overlay files into `sys/mips/rootfs` so Malta and N64
  consume the same account database, shell profiles, and `/root` smoke tests.
  Board overlays now carry only board-local `/etc` policy, device manifests,
  and hardware-specific files.
- [x] Hardware-smoke the shared rootfs overlay on N64 after moving it to
  `sys/mips/rootfs`: `smoke-as-vr4300`, `matrix-as-vr4300`,
  `/root/types-smoke.sh`, `/root/ll-smoke.sh`, `/root/ll-abi-smoke.sh`,
  `/root/cc-pcc-smoke.sh`, `mount`, `df`, `w`, `ps aux`, `/sbin/pstat -T`,
  and `/root/romfs-smoke.sh` all passed.
- [x] Fix incremental N64 rootfs staging so legacy `/share` install
  directories are recreated before every userland install; repeated
  `kernel.z64` builds no longer fail after `/share` has been moved under
  `/usr/share`.

## In-tree Toolchain For VR4300

The current N64 system is still built by the external libdragon/N64 GCC
toolchain. The in-tree RetroBSD toolchain is being adapted so it can produce
and inspect N64 userland objects without assuming PIC32 little-endian MIPS32r2.

- [x] Add shared target-endian a.out object helpers for command/toolchain code.
- [x] Build N64 userland with `TARGET_BIG_ENDIAN`, matching the VR4300 and the
  big-endian a.out files produced by `tools/elf2aout`.
- [x] Convert `src/cmd/as` object output to target-endian:
  - a.out header
  - emitted words
  - relocation records
  - symbol table records
  - `-EB` and `-EL` option handling
- [x] Convert `src/cmd/ld` to target-endian a.out and archive-index I/O:
  - input and output headers
  - input and output relocation records
  - input and output symbol table records
  - `__.SYMDEF` ranlib offsets
  - `-EB` and `-EL` option handling
- [x] Convert `src/cmd/ranlib` to read target-endian a.out members and write
  target-endian `__.SYMDEF` offsets.
- [x] Convert diagnostic/object utilities used by the toolchain path:
  `nm`, `aout`, `size`, and `strip`.
- [x] Fix `libc` `nlist()` symbol-value decoding for target endian.
- [x] Add a host-side or target-side smoke test that runs the new in-tree
  `as`, `ld`, `ranlib`, `nm`, `size`, and `strip` against a tiny relocatable
  object and verifies the generated big-endian a.out bytes.
- [x] Extend the host a.out toolchain smoke with a `.data` relocation check
  (`.word start`) so the big-endian `as`/`ld` path verifies data-section
  relocation bytes, not only text and archive handling.
- [x] Set the in-tree `ld` default text base to `0x00400000` for
  `TARGET_VR4300`; the old `0x7f008000` default is PIC32-specific and produces
  N64 executables that fault immediately on `exec`.
- [x] Add a VR4300 assembler mode:
  - keep MIPS I/II/III instructions needed by the N64 port
  - accept 32-bit VR4300 system/cache instructions used by the kernel:
    `cache`, `tlbp`, `tlbr`, `tlbwi`, and `tlbwr`
  - reject MIPS32r2-only instructions such as `clz`, `clo`,
    `ext`, `ins`, `seb`, `seh`, `wsbh`, `rdhwr`, `di`, `ei`, `ehb`, `mul`,
    `madd`, `msub`, `movn`, and `movz`
  - keep PIC32/default behavior unchanged
- [x] Smoke-test VR4300 assembler gating by running the in-tree `as` and
  verifying that normal VR4300 instructions assemble while MIPS32r2-only
  mnemonics fail in `-march=vr4300` mode; this is now a permanent host-side
  target: `make -C sys/mips BOARD=n64 smoke-as-vr4300`.
- [x] Add the first COP1/FPU assembly support needed by hard-float N64
  userland and current GCC VR4300 smoke output:
  - `$f0`..`$f31` register parsing
  - `lwc1`, `swc1`, `ldc1`, `sdc1`, and the compiler aliases
    `l.s`, `s.s`, `l.d`, and `s.d`
  - `mtc1`, `mfc1`, `ctc1`, `cfc1`
  - single/double arithmetic, compare, convert, round/trunc/ceil/floor, and
    `bc1*` branch instructions used by GCC output
- [x] Add COP1/FPU condition-code forms emitted by GCC/PCC hard-float paths.
- [x] Smoke-test COP1/FPU assembly by running the in-tree `as` on N64 or a
  host-compatible build with GCC-generated hard-float VR4300 assembly.
- [x] Build the first in-tree C compiler path for N64. This was originally
  prototyped through the legacy `src/cmd/ccom`; the active compiler source is
  now the imported PCC under `src/dev/pcc/pcc`, and the legacy compiler sources
  have been removed:
  - big-endian MIPS code generation
  - no `.abicalls`, `.cpload`, or `.cprestore` output for N64
  - legacy PCC tentative globals handled with `-fcommon`
  - strict-aliasing warnings disabled for the old PCC IR type-punning code
- [x] Fix N64 `ccom` output for the native assembler path so 64-bit integer
  initializers do not use GAS-only `.dword`; the current native path emits
  `.word` pairs accepted by the in-tree `as`.
- [x] Smoke-test the in-tree C compiler path on N64:
  - o32 calling convention compatibility
  - no MIPS32r2-only instruction emission in VR4300 mode
  - generated FPU instructions accepted by the in-tree assembler
  - failed compiler processes do not corrupt kernel state; N64 disables core
    dumps by default because `/var` is a small RAM disk, but after explicitly
    enabling core dumps the failure path must still return a filesystem error
    cleanly and must not panic the inode cache
  Hardware coverage now includes `/root/cc-pcc-smoke.sh`,
  `/root/types-smoke.sh`, `/root/ll-smoke.sh`, `/root/ll-abi-smoke.sh`,
  `smoke-as-vr4300`, `matrix-as-vr4300`, `/root/lang-smoke.sh`, and
  `/root/secondary-cc-smoke.sh`, all verified on N64 by 2026-06-27.
- [x] Add the in-tree toolchain smoke kit to the N64 rootfs under `/usr`:
  `/usr/bin/pcc`, `/usr/libexec/ccom`, `/usr/bin/as`, `/usr/bin/ld`,
  `/usr/bin/ar`, `/usr/bin/ranlib`, `/usr/bin/nm`, `/usr/bin/aout`,
  `/usr/bin/strip`, and no-header smoke C sources.
- [x] Add `/root/pcc-smoke.sh` to run the first target compiler smoke from
  writable `/var/tmp`: `pcc -S`, `as`, `ld -r`, full executable link/run,
  and FPU compile/link/run.
- [x] Add `/root/cc-pcc-smoke.sh` to verify that both driver names, `cc` and
  `pcc`, resolve from `/usr/bin`, use the in-tree PCC path on N64, and can
  find target headers/start files/libs through the default `/` sysroot; keep
  one explicit `--sysroot /` check.
- [x] Increase the N64 `u`/`u0` areas to 8 KiB so the kernel stack has enough
  headroom for nested `exec`/`namei`/FPU paths during the compiler smoke.
- [x] Build a.out-format `/usr/lib/crt0.o` and `/usr/lib/libc.a` for the in-tree
  `pcc`/`ld` path. `/usr/lib/crt0.o` is assembled by the N64 native `as` from
  `lib/startup/crt0.s`; do not stage ELF objects from the external GCC
  toolchain as compiler runtime files, because in-tree `ld` reports those as
  `bad magic`.
- [x] Generate the target `/usr/include` tree and `/usr/lib` compiler
  runtime/archive set into the N64 rootfs from the shared build outputs,
  instead of hand-listing static headers or libraries in the shared rootfs
  manifest.
- [x] Run the N64 native `ranlib` again after copying `libc.a` and `libm.a`
  into `rootfs.stage/usr/lib`, so the staged archive mtimes match `__.SYMDEF`
  and target `ld` does not warn that `/usr/lib/libc.a` is out of date.
- [x] Hardware-smoke the `/usr` rootfs split on N64: PATH includes
  `/usr/bin`, toolchain binaries live under `/usr/bin`, compiler runtime
  files live under `/usr/lib`, PCC `ccom` lives under `/usr/libexec/pcc`, and
  `/bin/cpp` resolves.
- [x] Hardware-smoke the expanded `/root/pcc-smoke.sh` on N64 and confirm
  both executable link/run paths complete without filesystem or inode-cache
  panics.
- [x] Hardware-smoke `/root/cc-pcc-smoke.sh` on N64 and confirm both
  `/usr/bin/cc` and `/usr/bin/pcc` can compile, assemble, link, and run integer
  and FPU smoke programs through the default `/` sysroot.
- [x] Add `/root/ll-smoke.sh` and `/root/ll-smoke.c` to exercise the first
  native `long long` runtime cases through both `/usr/bin/cc` and `/usr/bin/pcc`:
  global initializers, signed/unsigned shifts, arithmetic, compares, mixed
  register arguments, returns, and struct layout.
- [x] Make `ccom` emit big-endian integer 64-bit pairs through the normal o32
  high/low physical register ABI while keeping its internal pair bookkeeping
  low/high. This also makes `__divdi3`, `__udivdi3`, `__moddi3`, `__umoddi3`,
  and shift helper calls use the ABI order without an ad-hoc swap wrapper.
- [x] Hardware-smoke `/root/ll-smoke.sh` on N64.
- [x] Add `/root/types-smoke.sh` and `/root/types-smoke.c` as a broad
  target-side type implementation smoke for both `/usr/bin/cc` and `/usr/bin/pcc`.
  It covers scalar sizes/returns/arguments, stack-passed scalar and FPU
  arguments, pointer/function-pointer behavior, `float`, `double`,
  `long double`, static/global/local initialization, `const` objects and
  pointers, struct layout, bitfields, and big-endian union byte order.
- [x] Hardware-smoke `/root/types-smoke.sh` on N64 and confirm the broad
  backend/FPU ABI type matrix passes.
- [x] Add `/root/lang-smoke.sh` as the shared MIPS language/interpreter smoke:
  shell failure and command-substitution regressions, `awk`, `pdc`, classic
  `forth`, `retroforth`, `picoc`, and `tcl`.
- [x] QEMU-smoke `/root/lang-smoke.sh` on Malta.
- [x] Hardware-smoke `/root/lang-smoke.sh` on N64, including the `basic`
  interpreter step, verified 2026-06-27.
- [x] Audit and fix true o32 big-endian `long long` ABI behavior in `ccom`:
  register arguments, stack arguments, returns, struct layout/alignment,
  external object layout, and helper calls.
- [x] QEMU-smoke `/root/ll-abi-smoke.sh` on Malta and confirm both
  `/usr/bin/cc` and `/usr/bin/pcc` pass the focused C/assembly `long long` ABI
  matrix.
- [x] Hardware-smoke `/root/ll-abi-smoke.sh` on N64. This is the focused
  C/assembly ABI check for the `long long` fixes above.
- [x] Fix `ccom` stack `FUNARG` generation for `long long`/`double` arguments
  after the four o32 argument registers are exhausted; the old MIPS backend has
  the relevant `FUNARG` table entries disabled under `#if 0`. The host-side
  check now builds `/root/ll-smoke.c` through a big-endian host `ccom`, then
  assembles and links the generated a.out relocatable with the N64 native
  `as`/`ld -r`; `kernel.z64` also builds cleanly with `fsutil --check`.
- [x] Fix VR4300 text alignment in the in-tree a.out `as`/`ld` path. The
  stack-argument long-long smoke exposed a hard-float `ldc1` from an address
  shifted to 4-byte alignment after linking with a 4-byte-only start object.
  N64 `as` now finishes text sections on an 8-byte boundary, and N64 `ld`
  inserts real zero padding between input text segments so in-text double
  literals remain 8-byte aligned after final link.
- [x] Hardware-smoke `/root/ll-smoke.sh` v3 on N64 and confirm the new stack
  argument cases pass through both `/usr/bin/cc` and `/usr/bin/pcc`.
- [x] Remove obsolete secondary compiler paths after the imported PCC became
  the active compiler implementation. The shared MIPS rootfs now exposes
  `/usr/bin/cc`, `/usr/bin/pcc`, `/usr/bin/cpp`, `/usr/libexec/pcc/cpp`, and
  `/usr/libexec/pcc/ccom`; old `lcc`, `lccom`, `smallc`, `smlrc`, and legacy
  `src/cmd/cc`/`src/cmd/cpp`/`src/cmd/ccom` sources are no longer installed.
- [x] Add PCC MIPS CPU and float ABI selectors for the shared rootfs:
  `vr4300` keeps the old N64/Malta64 8-byte alignment policy, `mips32r2` uses
  the Malta 4-byte o32 layout, and `hard`/`soft` keep separate runtime,
  native PCC, rootfs, and smoke build directories. The QEMU matrix passed on
  2026-07-05 for `malta64/vr4300 hard`, `malta64/vr4300 soft`,
  `malta/mips32r2 hard`, and `malta/mips32r2 soft` with
  `/root/pcc-smoke-all.sh` and `linpack-pcc`.
- [x] Boot-isolate the updated N64 kernel/rootfs path on real hardware with
  UART-only minimal ROMs: PCC/raw swap, PCC/zswap, GCC/raw swap, and GCC/zswap
  all reached login on 2026-07-06.  The minimal rootfs now includes
  `/bin/login`, so `getty` can exec the login program instead of respawning the
  prompt after a username is entered.
- [x] Boot the updated hard-float PCC full zswap N64 rootfs on real hardware:
  root login works, the ROM rootfs layout is visible, and `uptime` runs.
- [ ] Hardware-smoke the updated PCC hard-float N64 rootfs on real hardware
  with `/root/pcc-smoke-all.sh`; `uname -a` currently panics with a kernel
  `TLB load/fetch` after login, so full runtime validation is still open.
- [ ] Hardware-smoke the updated PCC soft-float N64 rootfs on real hardware.

## N64 Command Filtering

Exclude PIC32/peripheral-specific commands until compatible N64 devices exist:

- [x] `adc-demo`: PIC32 ADC devices, `/dev/adc*`, `machine/adc.h`
- [x] `glcdtest`: GLCD device, `/dev/glcd0`, `glcd.h`
- [x] `portio`: GPIO devices, `/dev/porta`, `sys/gpio.h`
- [x] `pwm`: PWM devices, `/dev/pwm*`, `pwm.h`
- [x] `wiznet`: WIZnet stack and GPIO-dependent examples
- [x] `smux`: enabled after N64 pty support and hardware pty smoke-test;
  N64 builds only the `retro` side through `SMUX_SUBDIRS=retro`
- [x] `talloc`: depends on `/dev/tempX`; enable only if N64 gets temp devices
- [x] `devupdate`: depends on `/dev/kmem`; N64 currently generates `/dev`
  nodes at build time

Also exclude or defer libraries that only serve unavailable peripherals:

- [x] `libwiznet`
- [x] `libgpanel`

## Rootfs Contents

Add shared files to the ROM rootfs by updating `sys/mips/rootfs.manifest`, not
by copying binaries manually. Board-only device nodes and additions belong in
the board-specific generated/appended manifest.

- [x] Add basic `/bin` utilities after the shared install flow is in place:
  `cat`, `echo`, `pwd`, `cp`, `mv`, `rm`, `mkdir`, `rmdir`, `chmod`, `chown`,
  `sleep`, `kill`, `stty`, `uname`, `hostname`, `id`, `test`, and `env`
- [x] Add `man`, `more`, selected installed cat pages, and `/etc/fstab`
- [x] Add login profile defaults for `PATH=/bin:/sbin:/usr/bin:/usr/sbin` and
  `PAGER=/bin/cat`
- [x] Add selected `/sbin` utilities when the kernel side supports them:
  `reboot`, `mount`, `umount`, and `fsck`
- [x] Add `/usr/bin/n64input` as the minimal Joybus input smoke-test utility for
  `/dev/joypadN`, `/dev/mouseN`, and `/dev/kbdN`
- [x] Keep generated device nodes derived from kernel definitions through
  `sys/mips/n64/devnodes.awk`
- [x] Keep cartridge root read-only until a writable filesystem target exists
- [x] Expand the N64 rootfs toward the normal RetroBSD command set by enabling
  the first broad batch from `src/cmd/Makefile`: the full simple `STD` group
  that builds under N64, `egrep`/`expr`, `df`, shell scripts
  `false`/`nohup`/`true`, and selected portable subdirectory commands such as
  `awk`, `date`, `diff`, `find`, `fold`, `md5`, `printf`, `sed`, `sysctl`,
  `xargs`, `compress`, `chroot`, `mknod`, `mkpasswd`, and `shutdown`.
- [x] Build `libm` for N64 userland so historical tools such as `awk` can link
  their normal `-lm` dependency.
- [x] Build the existing terminal and interpreter libraries needed by the
  expanded userland: `libcurses`, `libvmf`, `libreadline`, and `libtcl`.
- [x] Add the first library-backed command batch to the N64 rootfs:
  `emg`, `forth`, `med`, `pdc`, `picoc`, `retroforth`, `setty`, `sl`, and
  `tcl`.
- [x] Include all files installed by the selected shared command makefiles that
  are needed for the normal rootfs rather than leaving staged artifacts out of
  `rootfs.img`: PCC `cc`/`cpp` aliases, `/usr/bin/sysctl`, `/sbin/updatedb`,
  `/usr/libexec/bigram`, `/usr/libexec/code`, generated `/usr/include`
  headers, `/usr/lib` compiler runtime archives, `/bin/cpp`, and
  `/usr/libexec/pcc/ccom`.
- [x] Increase the default N64 rootfs size once the command set grows; the
  image remains ROM-backed and demand-read through the romdisk block driver,
  not copied wholesale into RDRAM.
- [x] Enable `basic` on N64 with the same no-low-level-I/O behavior used by
  cross builds: `INP()` returns zero and `OUT` is ignored because the PIC32
  `ufetch`/`ustore` hooks are intentionally not provided on N64.
- [ ] Enable `pforth` only after its dictionary generation is endian-safe for
  big-endian MIPS. The bundled `pfdicdat.h`/`pforth.dic` are little-endian;
  the pForth generator keeps pointers in 32-bit `cell_t` and does not run on a
  64-bit macOS host without a low-address allocator or user-mode MIPS runner.
- [x] Add the missing libgcc-compatible runtime helpers to libc for 32-bit
  MIPS userland: 64-bit shifts, clz/ctz/ffs helpers, and the first 64-bit
  integer/double conversion helpers required by `ccom`. These are compiler ABI
  routines, so they live in `src/libc/runtime`, not N64 platform code.
- [x] Enable `/bin/cpp` compatibility and `/usr/bin/calendar` in the N64 rootfs
  now that the runtime helpers are available; include the installed calendar
  data under `/usr/share/calendar`.
- [x] Add shared `/root/cpp-calendar-smoke.sh` coverage for `/bin/cpp` and
  `/usr/bin/calendar` using writable `/var/tmp` for a temporary calendar file.
- [x] QEMU-smoke `/root/cpp-calendar-smoke.sh` on Malta after the shared MIPS
  rootfs rebuild.
- [x] Hardware-smoke `/root/cpp-calendar-smoke.sh` from the generated N64
  rootfs, verified 2026-06-27.

## Volatile Writable Filesystems

- [x] Add a tmpfs-like writable target for `/var` so the normal multiuser
  rootfs can keep `/` read-only while commands still have writable scratch and
  runtime state; `/tmp` is a symlink to `/var/tmp`.
- [x] Implement the first N64 version with the existing UFS stack on
  RAM-backed block devices rather than inventing a separate inode filesystem:
  this keeps mount, namei, read/write, directory, and fsck behavior aligned
  with the current kernel.
- [x] Split N64 volatile RAM storage from swap instead of reusing `/dev/swap`
  directly:
  - reserve one small RAM disk minor, `/dev/ram0`, for `/var`
  - size it from detected RDRAM, with conservative 4 MiB defaults and larger
    8 MiB defaults
  - subtract the reserved RAM disk bytes from the swap region so the areas do
    not overlap
- [x] Generate `/dev/ram0` from kernel device definitions, not by hand-editing
  the staged rootfs.
- [x] Teach the N64 boot scripts to create volatile filesystems at startup:
  `mkfs` the RAM disk, mount `/var`, then create required runtime directories
  such as `/var/run`, `/var/log`, `/var/tmp`, and `/var/lock`.
- [x] Route `pipe(2)` temporary inodes to the writable `/dev/ram0` filesystem
  through N64 `pipedev` setup, so shell pipelines work after `/var` is mounted.
- [x] Keep `/tmp` and `/var` volatile for the first version; later ROMFS or
  another writable block device can provide persistent upper storage.
- [x] Hardware smoke-test volatile mounts:
  - boot reaches login with `swap size = 3584 kbytes` on default 8 MiB
    zswap hardware builds, or `1792 kbytes` with `N64_ZSWAP=0`
  - `/dev/ram0` exists as a block device
  - `mount` shows `/var` mounted read/write
  - `ls -l /tmp` shows a symlink to `/var/tmp`
  - `echo ok >/tmp/test`, `cat /tmp/test`, `rm /tmp/test` work
  - `/var/run`, `/var/log`, `/var/tmp`, and `/var/lock` exist after boot
  - a failed root-owned process may try to write a core file in `/var/tmp`;
    filling `/dev/ram0` should fail cleanly and must not panic the inode cache

## Cartridge ROMFS And Writable Overlays

- [x] Add an N64cart flash command device, `/dev/cartflash0`, generated from
  kernel device definitions and guarded by the `n64cart` board option. Keep it
  as a character-device flash command interface, not a generic block device:
  ROMFS owns flash erase/program/map/list semantics directly.
- [x] Add kernel-callable N64cart flash helpers for ROMFS backend use:
  `n64cart_flash_getinfo`, `n64cart_flash_read_raw`,
  `n64cart_flash_write_sector_raw`, and `n64cart_flash_erase_sector_raw`.
- [x] Protect the cartridge firmware area in the raw flash driver by rejecting
  sector write/erase requests below `romfs_offset`.
- [x] Mirror N64cart-manager flash sessions in the kernel flash driver:
  disable the cartridge interrupt while command mode is active, switch to SPI
  only for the transaction, and restore quad-ROM mode before releasing the lock.
- [x] Add `/usr/bin/romfsctl` as a first hardware diagnostic for the writable
  N64cart ROMFS map/list implementation before wiring ROMFS into kernel
  pathname and mount code.
- [ ] Hardware smoke-test `/dev/cartflash0` and `/usr/bin/romfsctl` on real
  n64cart hardware:
  - [x] `romfsctl info`
  - [x] `romfsctl free`
  - [x] `romfsctl list /`
  - [ ] `ls -l /dev/cartflash0`
  - [x] `romfsctl list /roms`
  - [x] `romfsctl list -h /roms`
  - [x] `romfsctl cat /roms/kernel.z64 >/dev/null`
  - [x] `romfsctl cat /roms/kernel.z64 | wc`
  - [x] `romfsctl mkdir /retrobsd-test`
  - [x] `romfsctl write /retrobsd-test/hello.txt hello from retrobsd`
  - [x] `romfsctl cat /retrobsd-test/hello.txt`
  - [x] `romfsctl cat /retrobsd-test/hello.txt | wc`
  - [x] `romfsctl rename /retrobsd-test/hello.txt /retrobsd-test/renamed.txt`
  - [x] `romfsctl rm /retrobsd-test/renamed.txt`
  - [x] `romfsctl rmdir /retrobsd-test`
- [x] Add support for the N64cart cartridge ROMFS format used by
  `/Users/sash/Work/N64/N64cart/fw/romfs`, mounted from cartridge flash with
  read/write support. Keep this separate from the current UFS `rootfs.img`
  romdisk: UFS remains the system root until the new filesystem path is stable.
- [x] Extend kernel mount plumbing beyond the current UFS-only `mountfs()`
  path:
  - keep the existing `mount(2)`/UFS behavior working for PIC32 and current
    N64 root
  - add an explicit filesystem type path for `romfs` and later `overlay`
  - report the filesystem type through `statfs`
- [x] Add a userland mount entry point for cartridge ROMFS, so
  `mount -t romfs ... /cart` reaches the typed kernel mount path rather than
  relying on private N64 test tools.
- [x] Add `/dev/cartflash0` to the N64 `/etc/fstab` and mount `/cart`
  automatically read/write from `/etc/rc`; the n64cart flash is fixed cartridge
  hardware, not removable media.
- [x] On reboot/halt, force the n64cart flash interface back to idle quad-ROM
  mode with chip-select high before jumping to stage0 or stopping the CPU.
- [x] Add the first kernel ROMFS VFS backend:
  - reuse the same ROMFS core source as `/usr/bin/romfsctl`
  - keep `/dev/cartflash0` as the mount source and accept a character device in
    the ROMFS mount path
  - use the kernel-callable N64cart flash helpers instead of calling the
    `/dev/cartflash0` ioctl path from inside the kernel
  - load ROMFS map/list tables from cartridge flash at mount time
  - support synthetic inode lookup, `stat`, `open`, `read`, `lseek`, directory
    iteration, and `statfs`
- [x] Hardware smoke-test kernel ROMFS mount/read path on real N64cart
  hardware, 2026-06-14:
  - [ ] boot reaches login with `/dev/cartflash0` already mounted on `/cart`
  - [x] `mount -t romfs /dev/cartflash0 /cart`
  - [x] `/sbin/mount` shows `/dev/cartflash0 on /cart`
  - [x] `ls -l /cart`
  - [x] `ls -l /cart/roms`
  - [x] `cat /cart/roms/kernel.z64 >/dev/null`
  - [x] `cat /cart/roms/kernel.z64 | wc`
  - [x] `/sbin/umount /cart`
  - [x] `df -T /cart` reports `romfs`
- [x] Implement first ROMFS VFS write support:
  - [x] `write`
  - [x] create, append, truncate, unlink, rename, mkdir, and rmdir through the
    cartridge ROMFS flash map/list implementation
  - [x] support `ro` mounts by rejecting mutating operations with `EROFS`
  - [x] flush metadata/data through cartridge sector erase/write paths
  - [x] preserve directory entries and file sizes from the cartridge ROMFS entry
    table
  - [x] support `rename` over an existing compatible destination; non-empty
    destination directories still fail through the ROMFS delete path
- [x] Protect ROMFS system/read-only entries from mutation:
  `firmware`, `flashlist`, `flashmap`, and entries marked read-only, system, or
  reserved cannot be written, truncated, unlinked, renamed, or removed.
- [x] Validate ROMFS system entries at kernel mount time before enabling
  writable `/cart`, so a bad map/list start offset or corrupted system entry
  fails the mount instead of exposing writes to the wrong flash area.
- [x] Make ROMFS metadata updates more power-loss safe for the writable `/cart`
  default:
  - [x] keep a small opt-in journal in ROMFS files created by `romfsctl format`;
    existing cartridges without journal files keep working without journal
  - [x] add sequence/CRC recovery rules for journaled metadata commits
  - [x] recover from an interrupted metadata flush when a valid journal commit
    is present
  - [x] add a host-side synthetic power-cut test for journaled metadata
    updates, using an emulated NOR flash backend instead of `/dev/cartflash0`
  - [x] run Malta/QEMU ROMFS smoke after the journal recovery change, so the
    shared kernel/VFS backend still mounts, writes, unmounts, and remounts
  - [x] mirror N64cart-manager SPI sessions: disable the cartridge interrupt
    while command mode is active, switch to SPI only for the transaction, and
    restore quad-ROM mode before releasing the lock
  - [x] make ROMFS `sync`, `umount`, and N64 reboot wait until pending flash
    write/erase operations finish before restoring quad-ROM mode
  - [ ] run a deliberate power-cut test during metadata update on real
    n64cart hardware
- [x] Hardware smoke-test kernel ROMFS write path on real N64cart hardware,
  2026-06-14:
  - [x] `mkdir /cart/retrobsd-vfs-test`
  - [x] `echo hello >/cart/retrobsd-vfs-test/hello.txt`
  - [x] `cat /cart/retrobsd-vfs-test/hello.txt`
  - [x] `echo again >>/cart/retrobsd-vfs-test/hello.txt`
  - [x] `cat /cart/retrobsd-vfs-test/hello.txt`
  - [x] `echo reset >/cart/retrobsd-vfs-test/hello.txt`
  - [x] `mv /cart/retrobsd-vfs-test/hello.txt /cart/retrobsd-vfs-test/renamed.txt`
  - [x] `cat /cart/retrobsd-vfs-test/renamed.txt`
  - [x] `echo old >/cart/rename-test/a.txt`,
    `echo new >/cart/rename-test/b.txt`,
    `mv /cart/rename-test/a.txt /cart/rename-test/b.txt`, and
    `cat /cart/rename-test/b.txt` reports `old`
  - [x] `rm /cart/rename-test/b.txt` and `rmdir /cart/rename-test`
  - [x] `rm /cart/retrobsd-vfs-test/renamed.txt`
  - [x] `echo $?` reports `0`
  - [x] `ls -l /cart/retrobsd-vfs-test/` shows `total 0`
  - [x] `rmdir /cart/retrobsd-vfs-test`
  - [x] reboot, mount `/cart`, and verify deleted test entries stay deleted
- [ ] Optimize cartridge ROMFS I/O speed. Baseline from `diskspeed` in `/cart`
  on real N64cart hardware with 4 KiB blocks:
  - write: 8 MiB in 923.380 seconds, about 8 KiB/s
  - read: 8 MiB in 24.980 seconds, about 327 KiB/s
  - [x] cache N64 flash info probing so every read/write/erase does not re-read
    JEDEC and firmware metadata
  - [x] defer ROMFS file metadata flushes from every VFS `write(2)` to
    `sync(2)`/`umount(8)`, while keeping data-sector writes synchronous
  - [x] raise the ROMFS VFS write staging buffer from 512 bytes to one 4 KiB
    flash sector
  - [x] add host regression coverage for deferred metadata and failed data
    sector writes, so a failed backend write cannot commit stale metadata
  - [x] run the synthetic Malta `/cart` smoke with `diskspeed -m 1`, remount,
    and a second `/root/romfs-smoke.sh`
  - [x] run the same performance/safety checks on real N64cart hardware:
    `/root/romfs-smoke.sh`, `/cart` `diskspeed`, `sync`, `umount`, remount, and
    a second `/root/romfs-smoke.sh` passed
  - current real N64cart numbers after deferred metadata flush:
    - write: 8 MiB in 101.210 seconds, about 80 KiB/s
    - read: 8 MiB in 24.960 seconds, about 328 KiB/s
  - [x] add a 32 KiB read-ahead cache in the N64 n64cart flash driver for
    sequential physical flash reads; invalidate it on sector write/erase
  - [x] build `make -C sys/mips BOARD=n64 kernel.z64` after the read-ahead
    change
  - [x] run Malta/QEMU `/root/romfs-smoke.sh` and `diskspeed -m 1` on `/cart`
    after the read-ahead change
  - [x] run real N64cart hardware performance smoke after the read-ahead change:
    `/root/romfs-smoke.sh`, `/cart` `diskspeed`, `sync`, `umount`, remount, and
    a second `/root/romfs-smoke.sh` passed
  - current real N64cart numbers after 32 KiB read-ahead:
    - write: 8 MiB in 101.740 seconds, about 80 KiB/s
    - read: 8 MiB in 5.750 seconds, about 1424 KiB/s
  - [ ] Design a safe n64cart firmware-assisted fast read path before using the
    PI ROM window/ROM lookup table from the kernel. The current firmware owns
    the lookup table for the selected boot ROM, so kernel-side temporary lookup
    rewrites are not safe enough without an explicit firmware protocol or
    reserved scratch window.
- [ ] Add an overlay filesystem plan after ROMFS can be mounted:
  - lower layer is read-only UFS or ROMFS
  - upper layer is initially RAM-backed, ROMFS, or another writable block
    filesystem
  - lookups prefer upper entries, then lower entries unless hidden by a
    whiteout
  - first write/truncate/chmod/chown copies a lower file into the upper layer
  - unlink/rmdir of lower entries creates upper whiteouts
  - directory reads merge upper and lower entries and hide whiteouts
- [ ] Keep overlay v1 conservative: no hard-link preservation across layers,
  no cross-layer rename magic, no writable lower layer, and no persistence when
  the upper layer is RAM-only.

## Manual Index

- [x] Fix the shared `src/cmd/man` build so `apropos` is linked from
  `apropos.c`, while preserving normal PIC32/default command behavior
- [x] Generate `/usr/share/man/whatis` from staged cat pages during the N64 rootfs
  build with `src/man/makewhatis.sed`
- [x] Include `/usr/bin/apropos`, `/usr/bin/whatis`, and `/usr/share/man/whatis` in the
  ROM manifest only after the generated database exists
- [x] Remove root-level `/share`, `/include`, `/.profile`, and `/lib/*.a`
  compatibility entries from the shared MIPS rootfs manifest; keep headers and
  compiler archives under `/usr/include` and `/usr/lib`.
- [x] Install Deco under `/usr`: `/usr/bin/deco`, `/usr/lib/deco`, and
  `/usr/share/man/cat1/deco.0`.

## Pty And Job Control

- [x] Enable the shared `sys/kernel/tty_pty.c` driver through a kconfig
  `service pty 4`, matching the PIC32 pseudo-device pattern
- [x] Add N64 cdevsw entries for pty slave/master devices on majors 8 and 9
- [x] Generate `/dev/ttyp0`..`/dev/ttyp3` and `/dev/ptyp0`..`/dev/ptyp3`
  from kernel device definitions
- [x] Add `/bin/ptytest` as the minimal pty master/slave transfer test before
  enabling pty-dependent userland
- [x] Hardware smoke-test pty open/read/write before enabling pty-dependent
  userland such as `smux`; `ptytest`, `ptytest 1`, and `ptytest 2` pass on
  N64 hardware
- [x] Fix the shared MIPS libc `getpgrp()` syscall wrapper to pass pid 0 to
  the historical kernel `getpgrp(pid)` entry. The public header declares
  POSIX `getpgrp(void)`, and the generated raw syscall stub left `$a0`
  undefined, which made `/usr/bin/more` think it was not in the foreground pgrp.
- [x] Hardware smoke-test `more /etc/ttys` after the `getpgrp()` wrapper
  fix: the file is displayed and returns to the shell prompt on N64 hardware
- [ ] Repeat the `more` smoke test on the other login line if the first
  run covered only one of `/dev/console` or `/dev/ttyS0`

## Login And Multi-User Boot

- [x] Keep the N64 `icode` boot argument compatible with PIC32 by passing
  `"-"` to `/sbin/init`, so `init` enters the multi-user path without changing
  common `src/cmd/init`
- [x] Build `libutil`, `/libexec/getty`, and `/bin/login` through the shared
  `src`/`src/cmd` install flow
- [x] Add N64 `/etc/gettytab`, `/etc/passwd`, and `/etc/group` to the ROM
  rootfs manifest
- [x] Enable `console` in `/etc/ttys` as a secure getty line
- [x] Add `/dev/ttyS0` for the n64cart serial port and enable a secure getty
  line for serial login
- [x] Hardware smoke-test boot to `login:` and root login with the first
  read-only rootfs account database

## Signals

- [x] Replace the first N64 `sendsig()`/`sigreturn()` fatal stubs with the
  MIPS signal-frame path used by PIC32
- [x] Hardware smoke-test `sleep 10` followed by `Ctrl-C`; it must interrupt
  `sleep` and return to the shell prompt without respawning `getty`

## FPU And Userland ABI

- [x] Make shared MIPS libc `setjmp`/`longjmp` save and restore FPU state when
  compiled for hard-float N64 userland, while keeping the PIC32 soft-float
  path free of FPU instructions
- [ ] Hardware smoke-test hard-float `setjmp` users on N64, including
  `/usr/bin/more`, `login` motd interrupt handling, and any future FPU test command

## Platform Stubs

- [x] Replace the N64 `msec()` syscall stub with a timer tick counter, matching
  the PIC32 `ct_ticks * (1000 / HZ)` behavior
- [x] Replace N64 `nosys()` with the PIC32-style `SIGSYS` path
- [x] Replace the empty N64 `addupc()` profiling stub with the existing MIPS
  profiling counter logic
- [x] Keep n64cart RGB LED output as a separate `/dev/rgbled0` ioctl device,
  not as duplicated UART or `led_control()` state
- [x] Add `/bin/rgbled` as the n64cart userland test/control utility for RGB
  channel brightness
- [x] Hardware smoke-test `/dev/ttyS0` serial login on real cartridge hardware:
  both `ttyS0` and `console` getty prompts appear, and root login works on the
  early read-only rootfs
- [x] Hardware smoke-test the `/dev/rgbled0` ioctl path on real cartridge
  hardware with `/bin/rgbled`
- [x] Enable `/dev/mem` and `/dev/kmem` read access for historical BSD
  diagnostics (`w`, `ps`, `vmstat`, `pstat`). Securelevel still blocks write
  opens through `iskmemdev()`, and this remains a compatibility path rather
  than a preferred new N64 ABI.
- [x] Move N64 kernel namelist export to the shared MIPS `CPU_NLIST` sysctl
  path. `knlist(3)` now works without a `/vmunix` file, and Malta QEMU smoke
  covers `w`, `ps ax`, `vmstat`, `vmstat -f`, `/sbin/pstat -T`, and
  `/sbin/pstat -p`.
- [x] Keep `ucall`, `ufetch`, and `ustore` as `ENOSYS` on N64; the PIC32
  implementation is board/autoconfig-specific and should not be reused as an
  N64 ABI
- [x] Hardware smoke-test `/sbin/reboot`; current N64 code syncs buffers,
  disables interrupt sources, and jumps back to resident stage0 at
  `0x80300000` for a software restart, then reaches both `ttyS0` and
  `console` getty login prompts again
- [x] Do not implement a true hardware reset path for now; local libdragon and
  n64cart sources handle reset button pre-NMI but do not expose a safe
  software cold-reset primitive, so N64 uses the stage0 software restart

## Video Framebuffer And System Console

- [x] Add an N64-local VI framebuffer layer instead of using n64cart UART as
  the system console backend
- [x] Reserve framebuffer memory in `sys/mips/n64/layout.h`:
  - 4 MiB systems get 320x240x16 only before the base RAM swap region
  - 8 MiB systems get a 4 MiB user window and default to a 320x240x16 reserve
    for more RAM swap; `N64_HIGHRES_FB=1` restores the 640x480x16 reserve
- [x] Add N64 compressed RAM swap (`N64_ZSWAP=1` by default) so native PCC
  smoke can use a larger logical swap map without stealing more physical RDRAM
  from user memory or the framebuffer reserve
- [x] Confirm the UART-only `N64_ZSWAP=1` and `N64_ZSWAP=0` debug ROMs boot on
  real N64 hardware with both PCC-built and GCC-built kernels.
- [x] Add `/dev/fb0` as the framebuffer device with read/write access plus
  mode ioctls
- [x] Add `/bin/fbset` through the shared `src/cmd` install flow and include
  it in the N64 ROM manifest
- [x] Add `/bin/fbview` as a simple framebuffer JPEG smoke-test viewer using
  `/dev/fb0` and `stb_image` from the local N64cart source tree
- [x] Keep `/dev/ttyS0` as the n64cart serial login/input path, separate from
  framebuffer `/dev/console` input and output
- [x] Select VI timing from the IPL TV type byte so PAL, NTSC, and MPAL
  consoles/cartridges are handled by the same backend
- [ ] Hardware smoke-test default framebuffer console output on real hardware:
  320x240 on 4 MiB systems and 640x480 on 8 MiB systems
- [x] Hardware smoke-test `fbset`, `/dev/fb0`, and 640x480 mode on an 8 MiB
  system
- [x] Hardware smoke-test `fbview /cart/background.jpg` and
  `fbview /cart/moon.jpg` after mounting cartridge ROMFS
- [ ] Hardware smoke-test PAL and MPAL timing on matching hardware or a
  trusted hardware-accurate setup
- [x] Add shared framebuffer access for `/dev/fb0`; current read/write path
  still copies bytes, and real graphics can use the fixed N64 TLB-backed
  mapping returned by `N64FBIOC_GETMAP`
- [x] Add a real N64 system-console input backend, so `/dev/console` can be
  used without the n64cart serial login path
- [ ] Hardware smoke-test `/dev/console` login and shell input from a RandNET
  keyboard
- [x] Add a visible framebuffer console cursor at the current tty output
  position
- [ ] Hardware smoke-test console cursor drawing while typing, after
  Backspace, and across newlines/scrolling
- [x] Make framebuffer console `\b` move left only, leaving BSD tty erase
  rendering to the normal `BS SPACE BS` sequence
- [x] Normalize N64 console and n64cart UART `BS`/`DEL` input to the RetroBSD
  erase character before `ttyinput()`; n64cart UART also accepts `ESC [ 3 ~`
  as erase for host terminals that send Delete sequences
- [x] Enable `cb`, `ce`, and `ck` in the N64 ROM `/etc/gettytab`, because
  `getty` replaces the kernel-open tty defaults before handing the tty to
  `login`
- [x] Run `stty crt` from the N64 ROM `/etc/profile`; stock `login` clears
  local tty modes with `TIOCLSET 0`, so shell input needs `CRTBS`/`CRTERA`
  restored after login for visual erase
- [ ] Hardware smoke-test Backspace erase echo on `/dev/console` and
  `/dev/ttyS0`
- [x] Replace the framebuffer console's pixel-only state with an N64-local text
  cell buffer, so VT100 cursor movement, erase, scroll, and cursor redraw have
  stable backing state
- [x] Add a first-pass VT100 output parser to the framebuffer console:
  CR/LF/TAB/BS, ESC save/restore/reset/index, CSI cursor movement, erase,
  insert/delete character and line, SGR bold/underline/reverse, OSC skipping,
  and `CSI ?25h/?25l` cursor visibility
- [ ] Hardware smoke-test VT100 console output with shell editing, `man`,
  `more`, clear-screen sequences, reverse-video SGR, and cursor hide/show
- [ ] Hardware smoke-test `/bin/deco` on `/dev/console` and `/dev/ttyS0`;
  use it as the main interactive stress test for VT100 cursor addressing,
  reverse video, function keys, redraw, and tty erase handling
- [x] Return framebuffer console text geometry from `/dev/console`
  `TIOCGWINSZ`; `/dev/ttyS0` supplies an 80x24 fallback when no user winsize
  has been set
- [ ] Hardware smoke-test `stty size`, `ls`, `man`, `more`, and smux window
  sizing on `/dev/console` and `/dev/ttyS0`

## Joybus, Keyboard, Mouse, And Joypad

- [x] Add an N64 SI driver that performs one synchronous 64-byte PIF-RAM
  exchange through the SI DMA registers
- [x] Add a shared N64 Joybus layer for identify, controller/mouse read, and
  RandNET keyboard read commands
- [x] Add `/dev/joypad0`..`/dev/joypad3` for N64 controller snapshots
- [x] Add `/dev/mouse0`..`/dev/mouse3` for N64 mouse snapshots; mouse uses the
  same Joybus read command as a controller and is distinguished by identifier
  `0x0200`
- [x] Add `/dev/kbd0`..`/dev/kbd3` for RandNET keyboard snapshots and LED-byte
  ioctl control
- [x] Add `/usr/bin/n64input` through the shared `src/cmd` install flow
- [x] Hardware smoke-test `n64input list` on real N64 hardware:
  controller `0x0500`, mouse `0x0200`, and RandNET keyboard `0x0002`
  are detected on controller ports
- [x] Hardware smoke-test `n64input joypad N` with an N64 controller
- [x] Hardware smoke-test `n64input mouse N` with an N64 mouse
- [x] Hardware smoke-test `n64input kbd N` with a RandNET keyboard
- [ ] Hardware smoke-test `n64input kbd-led N value` with a RandNET keyboard
- [x] Add a keyboard-to-console path so the system console can accept input
  from an N64 keyboard instead of depending on `/dev/ttyS0`
- [ ] Hardware smoke-test RandNET keyboard console input, including `root`,
  `ls`, Backspace, Caps Lock LED, and `Ctrl-C`
- [ ] Add event/blocking semantics after raw snapshots pass on hardware; the
  first version intentionally uses synchronous polling for bring-up

## Verification

- [x] Run:

  ```
  make -C sys/mips BOARD=n64 reconfig
  make -C sys/mips BOARD=n64 clean
  make -C sys/mips BOARD=n64 kernel.z64
  make -q -C sys/mips BOARD=n64 kernel.z64
  ```

- [x] Confirm the build log uses shared `src`/`src/cmd` install rules and does
  not manually copy command binaries from `src/cmd`
- [x] Confirm `rootfs.generated.manifest` contains only N64-appropriate device
  nodes and files
- [x] Confirm the generated rootfs includes `/dev/joypad0`..`3`,
  `/dev/mouse0`..`3`, `/dev/kbd0`..`3`, and `/usr/bin/n64input`
- [x] Extract `rootfs.img` after packaging and verify manifest-controlled
  additions such as `/bin/rgbled` are present in the actual image, not only in
  `rootfs.stage`
- [x] Hardware smoke-test on N64:
  - [x] boot `kernel.z64`
  - [x] verify `rdram size=0x00800000` on Expansion Pak hardware
  - [x] verify `root size = 4096 kbytes` or the configured rootfs size
  - [x] run `ls /`, `ls /bin`, `ls /etc`, and `ls /dev`
  - [x] after adding basic tools, run `cat /etc/rc`, `pwd`, `uname`, `id`, and
    `stty`
  - [x] verify `uname -a` ends with `mips`, not `pic32`
  - [ ] Re-check `uname -a` on the current PCC full zswap ROM; the 2026-07-06
    hardware boot reaches root login and runs `uptime`, but `uname -a` now
    panics with `TLB load/fetch`
  - [x] run `sleep 1` and verify it returns by timeout without `Ctrl-C`
  - [x] verify `man uname`, `mount`, and `fsck -n /dev/romdisk` work from the
    default shell environment
  - [x] verify `apropos mount`, `apropos system`, `whatis uname`, and
    `whatis /sbin/mount`

Commit only after the generated ROM has passed the hardware smoke test.
