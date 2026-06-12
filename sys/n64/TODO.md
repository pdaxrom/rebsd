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
- [x] Keep generated device nodes derived from kernel definitions through
  `sys/n64/devnodes.awk`
- [x] Keep cartridge root read-only until a writable filesystem target exists

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
