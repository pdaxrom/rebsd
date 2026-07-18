# VM Hardware Test Procedure

This procedure validates the per-process VM implementation on N64 and Creator
Ci20 after the Malta QEMU matrix passes.  Keep the complete serial capture from
reset through the last command.  Do not mark a hardware item complete from a
build, emulator run, or partial boot log.

## Reproducible images

Build only through the shared `sys/mips` entry point and give each profile its
own object root.  The N64 candidate uses a 32-bit VR4300/o32 hard-float PCC
userland and a GCC kernel.  The Ci20 candidate uses a 32-bit MIPS32r2/o32
hard-float GCC kernel and userland.

```sh
make -C sys/mips BOARD=n64 O=/work/rebsd-hw/n64-gcc-pcc \
    N64_KERNEL_COMPILER=gcc N64_USERLAND_COMPILER=pcc \
    N64_USERLAND_CPU=vr4300 N64_USERLAND_FLOAT=hard \
    N64_USERLAND_ENDIAN=big N64_USERLAND_EXEC_FORMAT=aout \
    N64_ROOTFS_KBYTES=32768 N64_ZSWAP=1 all

make -C sys/mips BOARD=ci20 O=/work/rebsd-hw/ci20-gcc \
    MIPS_KERNEL_COMPILER=gcc MIPS_ROOTFS_COMPILER=gcc \
    MIPS_ROOTFS_CPU=mips32r2 MIPS_ROOTFS_FLOAT=hard \
    MIPS_ROOTFS_ENDIAN=little MIPS_ROOTFS_EXEC_FORMAT=elf \
    MIPS_ROOTFS_KBYTES=32768 MIPS_ROOTFS_NATIVE_PCC=1 all
```

The resulting hardware files are:

```text
/work/rebsd-hw/n64-gcc-pcc/obj/sys/mips/n64/preflight.z64
/work/rebsd-hw/n64-gcc-pcc/obj/sys/mips/n64/kernel.z64
/work/rebsd-hw/ci20-gcc/obj/sys/mips/ci20/ci20.uImage
```

Record the source commit and image digests before deployment:

```sh
git rev-parse HEAD
shasum -a 256 \
    /work/rebsd-hw/n64-gcc-pcc/obj/sys/mips/n64/preflight.z64 \
    /work/rebsd-hw/n64-gcc-pcc/obj/sys/mips/n64/kernel.z64 \
    /work/rebsd-hw/ci20-gcc/obj/sys/mips/ci20/ci20.uImage
```

## N64

The full image requires an N64cart-compatible cartridge interface.  Capture
the n64cart serial line from reset and deploy the ROM with the cartridge's
normal uploader; deployment tools are intentionally kept outside the kernel
build.

1. Boot `preflight.z64`.  Require the stage0 banner, detected RDRAM size, and
   a clean preflight completion before trying the full image.
2. Boot `kernel.z64` on an 8 MiB system first.  Require stage0, the kernel
   entry banner, `rdram size=0x00800000`, init, and a `login:` prompt.  Log in
   as `root` with no password.
3. Run the bounded, non-destructive VM and compiler gate:

   ```sh
   uname -a
   mount
   df -T
   vmstat
   /root/vm-process-smoke
   VM_STRESS_ITERATIONS=8 /root/vm-stress-smoke.sh
   /root/libc-abi-smoke.sh
   /root/native-pcc-smoke.sh
   /root/build-workload-smoke.sh
   /root/net-smoke.sh
   ```

   Every script must return to the shell with status zero.  After this bounded
   gate passes, run `VM_STRESS_ITERATIONS=100 /root/vm-stress-smoke.sh` on the
   8 MiB system.  It must report that all iterations passed and that its VM
   counter snapshot returned exactly to the warmed-up baseline.
4. The cartridge ROMFS gate writes only private test names and removes them.
   Run it only after the VM gate and retain the entire flash log:

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
   Log in as `root` with no password.
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
