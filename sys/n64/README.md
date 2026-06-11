# RetroBSD N64 port notes

This port targets the Nintendo 64 VR4300 as a 32-bit big-endian MIPS III
machine using the o32 ABI.

The N64 toolchain is expected at:

```
/Users/sash/Library/n64-toolchain-opengl
```

The kernel C code is built without FPU use. Userland is built hard-float.
The machine layer owns CP1/FPU enable, save and restore.

Initial boot target:

1. ROM-DOS style stage0 loads an ELF32 big-endian RetroBSD kernel.
2. Kernel detects 4 MiB base RDRAM vs 8 MiB Expansion Pak at startup.
3. Kernel uses N64cart UART for early console.
4. Root is a read-only UFS romdisk.
5. Swap/temp use a RAM-backed block device.
6. N64cart ROMFS is mounted later as a separate read-only filesystem.
