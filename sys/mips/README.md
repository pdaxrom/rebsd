# RetroBSD MIPS ports

This tree is the shared home for big-endian MIPS ports.

Layout:

- `common/` - MIPS CPU, exception, FPU, TLB and userland ABI code shared by
  all MIPS boards.
- `n64/` - Nintendo 64 board support: RDRAM layout, video, SI/Joybus and
  n64cart hardware.
- `malta/` - QEMU Malta board support: 8 MB RAM, 16550 serial console,
  ROM/initrd root filesystem and RAM-backed swap.

Both Malta and N64 are built as boards under the shared `sys/mips` architecture.
Use `make -C sys/mips BOARD=n64 kernel.z64` for the N64 cartridge image and
`make -C sys/mips BOARD=malta kernel` for the QEMU Malta kernel.

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

Useful QEMU smoke checks:

```
make -C sys/mips/malta
qemu-system-mips -M malta -m 8M -nographic -serial mon:stdio \
    -no-reboot -kernel sys/mips/malta/unix.elf
```

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

The Malta sparse flash backend intentionally keeps a limited number of RAM
sectors, so use `diskspeed -m 1` for QEMU smoke runs instead of the command's
default 8 MiB test size.
