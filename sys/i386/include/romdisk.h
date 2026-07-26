#ifndef _I386_ROMDISK_H_
#define _I386_ROMDISK_H_

#include <sys/types.h>

#define I386_ROMDISK_MAJOR       0
#define I386_ROMDISK_ROOT_MINOR  0

struct buf;

int i386romdisk_open(dev_t, int, int);
int i386romdisk_close(dev_t, int, int);
void i386romdisk_strategy(struct buf *);
daddr_t i386romdisk_size(dev_t);
int i386romdisk_ioctl(dev_t, u_int, caddr_t, int);

#endif /* _I386_ROMDISK_H_ */
