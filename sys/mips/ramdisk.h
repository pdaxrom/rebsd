#ifndef _MIPS_RAMDISK_H_
#define _MIPS_RAMDISK_H_

#include <machine/layout.h>

#define MIPS_RAMDISK_MAJOR       1
#define MIPS_RAMDISK_FIRST_MINOR 0

struct buf;
struct ramcomp_stats;

int mips_ramdisk_open(dev_t dev, int flag, int mode);
int mips_ramdisk_close(dev_t dev, int flag, int mode);
void mips_ramdisk_strategy(struct buf *bp);
daddr_t mips_ramdisk_size(dev_t dev);
int mips_ramdisk_ioctl(dev_t dev, u_int cmd, caddr_t addr, int flag);
int mips_ramdisk_compression_stats(struct ramcomp_stats *stats);

#endif /* _MIPS_RAMDISK_H_ */
