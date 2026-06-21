#ifndef _MIPS_ROMDISK_H_
#define _MIPS_ROMDISK_H_

#define MIPS_ROMDISK_MAJOR       0
#define MIPS_ROMDISK_ROOT_MINOR  0

struct buf;

int mipsromdisk_open(dev_t dev, int flag, int mode);
int mipsromdisk_close(dev_t dev, int flag, int mode);
void mipsromdisk_strategy(struct buf *bp);
daddr_t mipsromdisk_size(dev_t dev);
int mipsromdisk_ioctl(dev_t dev, u_int cmd, caddr_t addr, int flag);

#endif
