/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _DISK_RAMDISK_H_
#define _DISK_RAMDISK_H_

#include <sys/types.h>

struct buf;

/* Directly addressable writable memory exposed as a block device. */
struct ramdisk {
    unsigned char *rd_start;
    unsigned char *rd_end;
    int rd_minor;
    unsigned rd_block_shift;
};

int ramdisk_bdev_open(const struct ramdisk *, dev_t, int, int);
int ramdisk_bdev_close(const struct ramdisk *, dev_t, int, int);
void ramdisk_bdev_strategy(const struct ramdisk *, struct buf *);
daddr_t ramdisk_bdev_size(const struct ramdisk *, dev_t);
int ramdisk_bdev_ioctl(const struct ramdisk *, dev_t, u_int, caddr_t, int);

#endif /* _DISK_RAMDISK_H_ */
