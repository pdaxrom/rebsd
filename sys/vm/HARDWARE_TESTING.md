# VM Hardware Test Procedure

This procedure validates the per-process VM implementation on N64 and Creator
Ci20 after the Malta QEMU matrix passes.  Keep the complete serial capture from
reset through the last command.  Do not mark a hardware item complete from a
build, emulator run, or partial boot log.

## Reproducible images

Build only through the shared `sys/mips` entry point and give each profile its
own object root.  The N64 VM/compiler candidate uses a 32-bit VR4300/o32
hard-float PCC userland and a GCC kernel.  It builds a dependency-tracked test
rootfs containing the VM tests and everything invoked by `pcc-smoke-all.sh`,
not the full userland.  The Ci20 candidate uses a 32-bit MIPS32r2/o32
hard-float GCC kernel and userland.

```sh
make -C sys/mips BOARD=n64 O=/work/rebsd-hw/n64-vm-pcc-min \
    N64_KERNEL_COMPILER=gcc N64_USERLAND_COMPILER=pcc \
    N64_USERLAND_CPU=vr4300 N64_USERLAND_FLOAT=hard \
    N64_USERLAND_ENDIAN=big N64_USERLAND_EXEC_FORMAT=aout \
    N64_MINIMAL_ROOTFS=1 N64_MINIMAL_PCC_SMOKE=1 \
    N64_MINIMAL_ROOTFS_KBYTES=6144 N64_ROOTFS_NATIVE_PCC=1 \
    N64_ZSWAP=1 all

make -C sys/mips BOARD=ci20 O=/work/rebsd-hw/ci20-gcc \
    MIPS_KERNEL_COMPILER=gcc MIPS_ROOTFS_COMPILER=gcc \
    MIPS_ROOTFS_CPU=mips32r2 MIPS_ROOTFS_FLOAT=hard \
    MIPS_ROOTFS_ENDIAN=little MIPS_ROOTFS_EXEC_FORMAT=elf \
    MIPS_ROOTFS_KBYTES=32768 MIPS_ROOTFS_NATIVE_PCC=1 all
```

The resulting hardware files are:

```text
/work/rebsd-hw/n64-vm-pcc-min/obj/sys/mips/n64/preflight.z64
/work/rebsd-hw/n64-vm-pcc-min/obj/sys/mips/n64/kernel.z64
/work/rebsd-hw/ci20-gcc/obj/sys/mips/ci20/ci20.uImage
```

Record the source commit and image digests before deployment:

```sh
git rev-parse HEAD
shasum -a 256 \
    /work/rebsd-hw/n64-vm-pcc-min/obj/sys/mips/n64/preflight.z64 \
    /work/rebsd-hw/n64-vm-pcc-min/obj/sys/mips/n64/kernel.z64 \
    /work/rebsd-hw/ci20-gcc/obj/sys/mips/ci20/ci20.uImage
```

## N64 emulator prerequisite

Before producing the hardware image, run the host VM tests and the two
Malta64 profiles below.  `n64-8m` deliberately exposes only the N64 memory map
to the 32-bit kernel while QEMU provides backing for the emulator rootfs above
that range.

```sh
make -C sys/tests/vm test

make -C sys/mips BOARD=malta64 O=/work/rebsd-qemu/n64-8m-gcc \
    MALTA_MEMORY_PROFILE=n64-8m MALTA_QEMU_RAM=32M \
    MIPS_KERNEL_COMPILER=gcc MIPS_ROOTFS_COMPILER=gcc \
    MIPS_ROOTFS_NATIVE_PCC=0 MIPS_ROOTFS_KBYTES=16384 \
    vm-pressure-runtime

make -C sys/mips BOARD=malta64 O=/work/rebsd-qemu/n64-8m-gcc \
    MALTA_MEMORY_PROFILE=n64-8m MALTA_QEMU_RAM=32M \
    MIPS_KERNEL_COMPILER=gcc MIPS_ROOTFS_COMPILER=gcc \
    MIPS_ROOTFS_NATIVE_PCC=0 MIPS_ROOTFS_KBYTES=16384 \
    VM_PRESSURE_ARGS=-rs vm-pressure-runtime

make -C sys/mips BOARD=malta64 O=/work/rebsd-qemu/n64-8m-gcc \
    MALTA_MEMORY_PROFILE=n64-8m MALTA_QEMU_RAM=32M \
    MIPS_KERNEL_COMPILER=gcc MIPS_ROOTFS_COMPILER=gcc \
    MIPS_ROOTFS_NATIVE_PCC=0 MIPS_ROOTFS_KBYTES=16384 \
    VM_STRESS_ITERATIONS=100 vm-stress-runtime

make -C sys/mips BOARD=malta64 O=/work/rebsd-qemu/n64-8m-gcc-pcc \
    MALTA_MEMORY_PROFILE=n64-8m MALTA_QEMU_RAM=64M \
    MIPS_KERNEL_COMPILER=gcc MIPS_ROOTFS_COMPILER=pcc \
    MIPS_ROOTFS_CPU=vr4300 MIPS_ROOTFS_FLOAT=hard \
    MIPS_ROOTFS_ENDIAN=big MIPS_ROOTFS_EXEC_FORMAT=aout \
    MIPS_ROOTFS_NATIVE_PCC=1 MIPS_ROOTFS_KBYTES=32768 \
    PCC_SMOKE_ALL_CCOM_STRESS_COUNT=100 pcc-smoke-all-runtime
```

`vm-pressure-runtime` requires observable pageout and reverse-order pagein and
prints bounded progress with VM counters.  The second invocation uses
incompressible page contents and therefore covers zswap's raw-block path as
well as its compressed path.  When a valid rootfs already exists, use
`vm-pressure-smoke-runtime` to dependency-build only that utility, inject it
through `rootfs-patch-kernel`, and run the same test without rebuilding the
rest of userland.  `vm-diagnostics-pcc-runtime` may be used with
the same object root to build only PCC versions of `strace` and
`vm-pressure-smoke`, inject those two files through the normal rootfs patch
pipeline, and run them without switching the whole rootfs to PCC.  For an
interactive trace of a specific command and all descendants, run
`strace command [arguments ...]`; the kernel sends trace lines only to that
process' controlling terminal.

Require `ram size=0x00800000`, `user mem = 4096 kbytes`,
`swap size = 3584 kbytes`, successful VM self-tests, 100 clean stress
iterations, `PCC_SMOKE_ALL_FAILURES 0`, and `PCC_SMOKE_ALL_OK`.  A QEMU pass
authorizes hardware testing; it does not complete an N64 hardware checklist
item.

## MIPS little-endian emulator prerequisite

Run the same common VM implementation as a MIPS32r2 little-endian kernel and
userland before testing Ci20 hardware.  The large reserved RAM-swap area below
intentionally leaves about 5 MiB allocatable, allowing the bounded diagnostic
to force pageout and pagein without changing the normal 256 MiB Ci20 layout.

```sh
make -C sys/mips BOARD=maltael O=/work/rebsd-qemu/mipsel-vm \
    MALTA_QEMU_RAM=64M MALTA_RAM_KBYTES=65536 \
    MALTA_RAMSWAP_KBYTES=34816 \
    MIPS_KERNEL_COMPILER=gcc MIPS_ROOTFS_COMPILER=gcc \
    MIPS_ROOTFS_NATIVE_PCC=0 MIPS_ROOTFS_KBYTES=16384 \
    VM_PRESSURE_ARGS=-rs vm-pressure-smoke-runtime

make -C sys/mips BOARD=maltael O=/work/rebsd-qemu/mipsel-vm \
    MALTA_QEMU_RAM=64M MALTA_RAM_KBYTES=65536 \
    MALTA_RAMSWAP_KBYTES=34816 \
    MIPS_KERNEL_COMPILER=gcc MIPS_ROOTFS_COMPILER=gcc \
    MIPS_ROOTFS_NATIVE_PCC=0 MIPS_ROOTFS_KBYTES=16384 \
    VM_STRESS_ITERATIONS=100 vm-stress-runtime

make -C sys/mips BOARD=maltael O=/work/rebsd-qemu/mipsel-vm \
    MALTA_QEMU_RAM=64M MALTA_RAM_KBYTES=65536 \
    MALTA_RAMSWAP_KBYTES=34816 \
    MIPS_KERNEL_COMPILER=gcc MIPS_ROOTFS_COMPILER=gcc \
    MIPS_ROOTFS_NATIVE_PCC=0 MIPS_ROOTFS_KBYTES=16384 \
    vm-diagnostics-pcc-runtime
```

Require `VM_PRESSURE_RC:0`, `VM_STRESS_RC:0`, and
`VM_DIAGNOSTICS_PCC_RC:0`.  The pressure reports must contain nonzero pageout
and pagein deltas, zero swap failures, and a nonzero free-page reserve while
faulting across the 4 MiB pmap directory boundary.  The PCC target builds the
compiler and its target runtime as declared dependencies; it does not use or
copy an external historical compiler tree.

## N64

The minimal VM/compiler image requires an N64cart-compatible cartridge
interface.  Capture the n64cart serial line from reset and deploy the ROM with
the cartridge's normal uploader; deployment tools are intentionally kept
outside the kernel build.

1. Boot `preflight.z64`.  Require the stage0 and `ReBSD N64 preflight`
   banners, detected RDRAM size, and the `rootfs.img` offset, size, and magic.
   The preflight kernel deliberately loops forever after printing these lines;
   power-cycle the console after retaining them, then try the full image.
2. Boot `kernel.z64` on an 8 MiB system first.  Require stage0, the kernel
   entry banner, `rdram size=0x00800000`, init, and a `login:` prompt.  Log in
   as `root` with no password.
3. Run the bounded, non-destructive VM and compiler gate:

   ```sh
   mount
   df
   vmstat
   /root/vm-process-smoke
   VM_STRESS_ITERATIONS=8 /root/vm-stress-smoke.sh
   /usr/bin/vm-pressure-smoke -s
   /usr/bin/vm-pressure-smoke -rs
   /root/pcc-smoke-all.sh
   ```

   Every script must return to the shell with status zero;
   both pressure runs must print `VM_PRESSURE_OK` with nonzero pageout and
   pagein counts and zero swap failures.  `vm-pressure-smoke` rejects any
   observed swap failure instead of printing a false success; `-s` additionally
   requires actual pageout and pagein activity.  The `-r` run covers
   incompressible data and zswap's raw-block path.  If a later command stalls,
   rerun only that command under `/usr/bin/strace`; syscall tracing is inherited
   across fork/exec and is written to the invoking UART terminal without
   enabling global kernel tracing.  For example:

   ```sh
   /usr/bin/strace /root/pcc-smoke-all.sh
   ```

   `pcc-smoke-all.sh` must print `PCC_SMOKE_ALL_FAILURES 0` and
   `PCC_SMOKE_ALL_OK`.  After this bounded gate passes, run
   `VM_STRESS_ITERATIONS=100 /root/vm-stress-smoke.sh` on the 8 MiB system.  It
   must report that all iterations passed and that its VM counter snapshot
   returned exactly to the warmed-up, quiescent baseline.
4. Stop after the VM/compiler gate when testing the minimal image.  The
   cartridge ROMFS and USB-network gates require a separate full-rootfs image
   built without `N64_MINIMAL_ROOTFS=1`; do not build it merely to repeat the
   VM/compiler tests.  When that later image is needed, the cartridge ROMFS
   gate writes only private test names and removes them:

   ```sh
   ls -l /dev/cartflash0
   mount
   /root/romfs-smoke.sh
   sync
   /sbin/umount /cart
   /sbin/mount -t romfs /dev/cartflash0 /cart
   /root/romfs-smoke.sh
   ```

5. For CDC ECM, confirm that the host creates an Ethernet interface with MAC
   `02:64:00:00:00:01`; the guest uses `02:64:00:00:00:10`.  Configure the
   host as `10.64.0.1/24`, then run:

   ```sh
   /sbin/ifconfig usbn0 inet 10.64.0.2 netmask 255.255.255.0 up
   /usr/bin/ping -c 3 10.64.0.1
   ```

   Unplug and reconnect the USB cable while idle, wait for re-enumeration, and
   repeat the ping.  Loss of the serial console, a panic, or stale interface
   state is a failure.
6. Finally boot the same `kernel.z64` without an Expansion Pak.  Require
   `rdram size=0x00400000`, login, `/root/vm-process-smoke`, and an eight-cycle
   `vm-stress-smoke.sh`.  Treat native compiler OOM on 4 MiB separately from a
   VM fault; the full native compiler gate is required on 8 MiB.

## Creator Ci20

Capture UART4 at 115200 baud, 8 data bits, no parity, one stop bit.  Boot first
with both type-A USB ports empty.  Put `ci20.uImage` on the medium or server
used by the existing U-Boot setup.  A typical FAT/MMC load is shown below;
adjust the MMC device/partition to the board's existing environment:

```text
fatload mmc 0:1 0x88000000 ci20.uImage
bootm 0x88000000
```

An existing TFTP setup may use the same non-overlapping load address with
`tftpboot 0x88000000 ci20.uImage`, followed by `bootm 0x88000000`.

1. Require the kernel banner, DMA and disk initialization, EHCI and OHCI
   attachment, `usb0: deferred task runner uses proc0`, init, and `login:`.
   The corrected per-process VM image must print `user mem = 65536 kbytes`;
   the 4 MiB legacy TLB reserve is physical bootstrap state, not the Ci20
   process limit.  Log in as `root` with no password.
2. With J23 still empty, run the same base gate:

   ```sh
   uname -a
   mount
   df -T
   vmstat
   /root/vm-process-smoke
   VM_STRESS_ITERATIONS=100 /root/vm-stress-smoke.sh
   /root/libc-abi-smoke.sh
   /root/native-pcc-smoke.sh
   /root/build-workload-smoke.sh
   /root/net-smoke.sh
   ```

   Then run the complete compiler regression:

   ```sh
   /root/pcc-smoke-all.sh
   ```

   Require `PCC_SMOKE_ALL_FAILURES 0` and `PCC_SMOKE_ALL_OK`.

3. Connect a known USB 2.0 high-speed flash drive to the right-hand J23 port.
   J24/J8 belongs to the separate OTG block and stays empty.  Require `umass0`
   and `sd0`, then perform only short reads.  Read `sd0a` only when the attach
   log reports partition 1 and the node exists:

   ```sh
   fdisk -p /dev/sd0
   dd if=/dev/sd0 of=/dev/null bs=1024 count=16
   dd if=/dev/sd0a of=/dev/null bs=1024 count=16
   ```

4. For a legacy MBR disk containing exactly one FAT partition, the guarded
   read-only gate is:

   ```sh
   /root/gpt-media-smoke.sh preflight /dev/rsd0 /dev/sd0a /mnt
   ```

   For an already valid GPT disk, use `verify` instead of `preflight`.  Do not
   run the `migrate` or `fresh` modes during this validation: they intentionally
   write partition metadata, and `fresh` destroys the existing layout.
5. Ensure the filesystem is unmounted, unplug the idle drive, and require
   `sd0: detached` and `umass0: detached` without a panic.  Reconnect it and
   repeat the short reads and appropriate guarded gate.  On a disposable FAT
   test disk, run `/root/fat-rename-smoke.sh /dev/sd0a /mnt`; it performs its
   own mount, private-name write/rename cleanup, and unmount.
6. Separately connect a low/full-speed USB keyboard to J23.  Require EHCI to
   hand it to OHCI, type `echo usb-ok`, and check Shift, Backspace, and Ctrl-C.
   Repeat unplug/reconnect and typing at least five times.  UART must remain
   responsive throughout.

### Confirmed Ci20 VM gate

The process/VM-only part of this procedure passed on real Ci20 hardware on
2026-07-18 with image SHA-256
`f37aa1b10d49d1589c70d10eb3330b558414d2d377bab2311446dac4d9db2dfd`.
Both `/root/vm-process-smoke` and the 100-cycle `vm-stress-smoke.sh` completed,
and the final counter comparison reported no leak.  This result does not yet
cover the compiler, USB, storage, or hot-unplug steps above.

## Failure record

For every failure retain:

- the full source commit and exact image SHA-256;
- board/RAM/cartridge or USB-device details;
- the serial transcript beginning before reset;
- the first failed command, its output, and shell status;
- whether UART/serial input still works;
- for a Ci20 MMU hang, the JTAG PC, SP, RA, Cause, EPC, BadVAddr, EntryHi,
  EntryLo, Context, and Status registers.

Do not continue with writable media tests after a VM panic, unexpected reboot,
lost serial console, failed read-only check, or filesystem inconsistency.
