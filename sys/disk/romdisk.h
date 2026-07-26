/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _DISK_ROMDISK_H_
#define _DISK_ROMDISK_H_

#include <sys/types.h>

struct buf;

/*
 * Byte-backed read-only block device.  Machine-dependent code supplies only
 * the linker-defined image bounds and the minor allocated to the device.
 */
struct romdisk {
    const unsigned char *rd_start;
    const unsigned char *rd_end;
    int rd_minor;
    unsigned rd_block_shift;
};

int romdisk_bdev_open(const struct romdisk *, dev_t, int, int);
int romdisk_bdev_close(const struct romdisk *, dev_t, int, int);
void romdisk_bdev_strategy(const struct romdisk *, struct buf *);
daddr_t romdisk_bdev_size(const struct romdisk *, dev_t);
int romdisk_bdev_ioctl(const struct romdisk *, dev_t, u_int, caddr_t, int);

#endif /* _DISK_ROMDISK_H_ */
