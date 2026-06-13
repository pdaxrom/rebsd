/*
 * Build the shared n64cart ROMFS core inside the N64 kernel.
 *
 * The implementation lives with the romfsctl utility so the kernel mount path
 * and the diagnostic tool use the same on-flash format code.
 */
#include "../../src/cmd/romfsctl/romfs.c"
