#
# Minimal N64 CDC ECM diagnostic image: PCC kernel and GCC userland.
#
# Keep the production cartridge firmware and polling UART transport.  This
# profile removes video, controller input, ROMFS, and native PCC workloads so
# USB Ethernet can be exercised with the smallest useful userland.
#
N64_KERNEL_COMPILER := pcc
N64_USERLAND_COMPILER := gcc
N64_USERLAND_CPU := vr4300
N64_USERLAND_FLOAT := hard
N64_USERLAND_ENDIAN := big
N64_USERLAND_EXEC_FORMAT := aout

N64_MINIMAL_USBNET_DEBUG := 1
N64_MINIMAL_ROOTFS := 1
N64_MINIMAL_PCC_SMOKE := 0
N64_MINIMAL_ROOTFS_KBYTES := 3072
N64_ROOTFS_NATIVE_PCC := 0
N64_ENABLE_ROMFS := 0

N64_USB_GDB := 0
N64_RESET_DUMP := 1

N64_STAGE0_RESERVED_BYTES := 0x00080000
