#ifndef _I386_RAMDISK_H_
#define _I386_RAMDISK_H_

#include <sys/types.h>

struct buf;

#define I386_RAMDISK_MAJOR       1
#define I386_RAMDISK_FIRST_MINOR 0

int i386_ramdisk_open(dev_t, int, int);
int i386_ramdisk_close(dev_t, int, int);
void i386_ramdisk_strategy(struct buf *);
daddr_t i386_ramdisk_size(dev_t);
int i386_ramdisk_ioctl(dev_t, u_int, caddr_t, int);

#endif /* _I386_RAMDISK_H_ */
