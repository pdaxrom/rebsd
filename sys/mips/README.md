# RetroBSD MIPS ports

This tree is the shared home for big-endian MIPS ports.

Planned layout:

- `common/` - MIPS CPU, exception, FPU, TLB and userland ABI code shared by
  all MIPS boards.
- `n64/` - Nintendo 64 board support: RDRAM layout, video, SI/Joybus and
  n64cart hardware.
- `malta/` - QEMU Malta board support: 8 MB RAM, 16550 serial console,
  ROM/initrd root filesystem and RAM-backed swap.

The existing `sys/n64` port remains the active N64 build while the shared MIPS
tree is brought up.  After Malta boots under `qemu-system-mips`, move the
already-working N64 files into `sys/mips/n64` in small tested steps.
