#
# Minimal N64 8 MiB VM hardware-test image: GCC kernel and GCC userland.
#
# Keep only the polling UART console, reset dump, compressed swap, and the
# process/pressure smoke tools required by the final MMU hardware gate.
#
N64_KERNEL_COMPILER := gcc
N64_USERLAND_COMPILER := gcc
N64_USERLAND_CPU := vr4300
N64_USERLAND_FLOAT := hard
N64_USERLAND_ENDIAN := big
N64_USERLAND_EXEC_FORMAT := aout

N64_MINIMAL_UART_ONLY := 1
N64_MINIMAL_ROOTFS := 1
N64_MINIMAL_PCC_SMOKE := 0
N64_MINIMAL_ROOTFS_KBYTES := 2048
N64_ROOTFS_NATIVE_PCC := 0
N64_ZSWAP := 1

N64_USB_GDB := 0
N64_RESET_DUMP := 1

N64_STAGE0_RESERVED_BYTES := 0x00080000
