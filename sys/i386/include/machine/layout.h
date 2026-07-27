#ifndef _I386_LAYOUT_H_
#define _I386_LAYOUT_H_

/* Keep the first 4 MiB clear of NULL/low-memory and kernel bootstrap maps. */
#define I386_USER_VADDR_START  0x00400000
#define I386_USER_MAXMEM       0x04000000
#define I386_USER_VADDR_END    0x80000000

/*
 * Kernel-only mappings for PCI MMIO live above the user ABI and below the
 * permanent physical-memory direct map.
 */
#define I386_DEVICE_VADDR_START 0xb0000000
#define I386_DEVICE_VADDR_END   0xc0000000

#endif
