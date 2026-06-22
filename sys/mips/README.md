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
