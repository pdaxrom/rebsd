#ifndef _N64_ROMDISK_H_
#define _N64_ROMDISK_H_

#define N64_ROMDISK_MAJOR       0
#define N64_ROMDISK_ROOT_MINOR  0

struct buf;

int n64romdisk_open(dev_t dev, int flag, int mode);
int n64romdisk_close(dev_t dev, int flag, int mode);
void n64romdisk_strategy(struct buf *bp);
daddr_t n64romdisk_size(dev_t dev);
int n64romdisk_ioctl(dev_t dev, u_int cmd, caddr_t addr, int flag);

#endif /* _N64_ROMDISK_H_ */
