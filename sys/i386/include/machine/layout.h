#ifndef _I386_LAYOUT_H_
#define _I386_LAYOUT_H_

/* Keep the first 4 MiB clear of NULL/low-memory and kernel bootstrap maps. */
#define I386_USER_VADDR_START  0x00400000u
#define I386_USER_VADDR_END    0x80000000u

#endif
