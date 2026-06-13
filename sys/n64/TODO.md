# RetroBSD N64 TODO

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
  - remove the N64-only per-command build loop from `sys/n64/Makefile.kconf`
  - call the shared `src`/`src/cmd` install path with
    `TARGET_PLATFORM=n64`, `DESTDIR=rootfs.stage`, and the generated
    `N64_USER_LDSCRIPT`
  - keep `sys/n64/rootfs.manifest` as the source of which staged files are
    included in the cartridge ROM rootfs

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

Add files to the ROM rootfs by updating `sys/n64/rootfs.manifest`, not by
copying binaries manually.

- [x] Add basic `/bin` utilities after the shared install flow is in place:
  `cat`, `echo`, `pwd`, `cp`, `mv`, `rm`, `mkdir`, `rmdir`, `chmod`, `chown`,
  `sleep`, `kill`, `stty`, `uname`, `hostname`, `id`, `test`, and `env`
- [x] Add `man`, `more`, selected installed cat pages, and `/etc/fstab`
- [x] Add login profile defaults for `PATH=/bin:/sbin` and `PAGER=/bin/cat`
- [x] Add selected `/sbin` utilities when the kernel side supports them:
  `reboot`, `mount`, `umount`, and `fsck`
- [x] Add `/bin/n64input` as the minimal Joybus input smoke-test utility for
  `/dev/joypadN`, `/dev/mouseN`, and `/dev/kbdN`
- [x] Keep generated device nodes derived from kernel definitions through
  `sys/n64/devnodes.awk`
- [x] Keep cartridge root read-only until a writable filesystem target exists
- [ ] Expand the N64 rootfs toward the normal RetroBSD command set by enabling
  command groups from `src/cmd/Makefile` in batches, while continuing to
  exclude PIC32/peripheral-specific tools and commands that require unavailable
  writable devices.
- [ ] Increase the default N64 rootfs size once the command set grows; the
  image remains ROM-backed and demand-read through the romdisk block driver,
  not copied wholesale into RDRAM.

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
- [ ] Hardware smoke-test volatile mounts:
  - boot reaches login with `swap size = 2944 kbytes` on 8 MiB hardware
  - `/dev/ram0` exists as a block device
  - `mount` shows `/var` mounted read/write
  - `ls -l /tmp` shows a symlink to `/var/tmp`
  - `echo ok >/tmp/test`, `cat /tmp/test`, `rm /tmp/test` work
  - `/var/run`, `/var/log`, `/var/tmp`, and `/var/lock` exist after boot

## Cartridge ROMFS And Writable Overlays

- [x] Add an N64cart flash command device, `/dev/cartflash0`, generated from
  kernel device definitions and guarded by the `n64cart` board option.
- [x] Add `/bin/romfsctl` as a first hardware diagnostic for the writable
  N64cart ROMFS map/list implementation before wiring ROMFS into kernel
  pathname and mount code.
- [ ] Hardware smoke-test `/dev/cartflash0` and `/bin/romfsctl` on real
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
- [ ] Add support for the N64cart cartridge ROMFS format used by
  `/Users/sash/Work/N64/N64cart/fw/romfs`, mounted from cartridge flash with
  read/write support. Keep this separate from the current UFS `rootfs.img`
  romdisk: UFS remains the system root until the new filesystem path is stable.
- [ ] Extend kernel mount plumbing beyond the current UFS-only `mountfs()`
  path:
  - keep the existing `mount(2)`/UFS behavior working for PIC32 and current
    N64 root
  - add an explicit filesystem type path for `romfs` and later `overlay`
  - report the filesystem type through `statfs`
- [ ] Add a userland mount entry point for cartridge ROMFS, so
  `mount -t romfs ... /cart` can mount the cartridge filesystem rather than
  relying on private N64 test tools.
- [ ] Implement the first ROMFS version as a real writable filesystem:
  - lookup, `stat`, `open`, `read`, `write`, `lseek`, directory iteration
  - create, append, truncate, unlink, rename, mkdir, and rmdir through the
    cartridge ROMFS flash map/list implementation
  - support `ro` mounts by rejecting mutating operations with `EROFS`
  - flush metadata/data through cartridge sector erase/write paths and make
    reboot/sync call the ROMFS flush path
  - preserve directory entries and file sizes from the cartridge ROMFS entry
    table
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
- [x] Generate `/share/man/whatis` from staged cat pages during the N64 rootfs
  build with `src/man/makewhatis.sed`
- [x] Include `/bin/apropos`, `/bin/whatis`, and `/share/man/whatis` in the
  ROM manifest only after the generated database exists

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
  undefined, which made `/bin/more` think it was not in the foreground pgrp.
- [x] Hardware smoke-test `/bin/more /etc/ttys` after the `getpgrp()` wrapper
  fix: the file is displayed and returns to the shell prompt on N64 hardware
- [ ] Repeat the `/bin/more` smoke test on the other login line if the first
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
  `/bin/more`, `login` motd interrupt handling, and any future FPU test command

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
- [x] Keep `/dev/mem` and `/dev/kmem` disabled on N64 for the first port;
  `kmemdev()`, `iskmemdev()`, and character minors 0/1 stay intentionally
  unavailable
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
- [x] Reserve framebuffer memory in `sys/n64/layout.h`:
  - 4 MiB systems get 320x240x16 only before the base RAM swap region
  - 8 MiB systems reserve enough Expansion Pak memory for 640x480x16 and can
    switch between 320x240 and 640x480
- [x] Add `/dev/fb0` as the framebuffer device with read/write access plus
  mode ioctls
- [x] Add `/bin/fbset` through the shared `src/cmd` install flow and include
  it in the N64 ROM manifest
- [x] Keep `/dev/ttyS0` as the n64cart serial login/input path, separate from
  framebuffer `/dev/console` input and output
- [x] Select VI timing from the IPL TV type byte so PAL, NTSC, and MPAL
  consoles/cartridges are handled by the same backend
- [ ] Hardware smoke-test default framebuffer console output on real hardware:
  320x240 on 4 MiB systems and 640x480 on 8 MiB systems
- [x] Hardware smoke-test `fbset`, `/dev/fb0`, and 640x480 mode on an 8 MiB
  system
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
- [x] Add `/bin/n64input` through the shared `src/cmd` install flow
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
  make -C sys/n64 reconfig
  make -C sys/n64/nintendo64 clean
  make -C sys/n64/nintendo64 kernel.z64
  make -q -C sys/n64/nintendo64 kernel.z64
  ```

- [x] Confirm the build log uses shared `src`/`src/cmd` install rules and does
  not manually copy command binaries from `src/cmd`
- [x] Confirm `rootfs.generated.manifest` contains only N64-appropriate device
  nodes and files
- [x] Confirm the generated rootfs includes `/dev/joypad0`..`3`,
  `/dev/mouse0`..`3`, `/dev/kbd0`..`3`, and `/bin/n64input`
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
  - [x] run `sleep 1` and verify it returns by timeout without `Ctrl-C`
  - [x] verify `man uname`, `mount`, and `fsck -n /dev/romdisk` work from the
    default shell environment
  - [x] verify `apropos mount`, `apropos system`, `whatis uname`, and
    `whatis /sbin/mount`

Commit only after the generated ROM has passed the hardware smoke test.
