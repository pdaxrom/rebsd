/*
 * Build the shared cartridge ROMFS core inside MIPS kernels.
 *
 * The implementation lives with the romfsctl utility so the kernel mount path
 * and the diagnostic tool use the same on-flash format code.
 */
#include "../../../src/cmd/romfsctl/romfs.c"
