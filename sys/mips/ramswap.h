#ifndef _MIPS_RAMSWAP_H_
#define _MIPS_RAMSWAP_H_

#include <machine/layout.h>

#define MIPS_RAMSWAP_MAJOR       1
#define MIPS_RAMSWAP_MINOR       0
#define MIPS_RAMDISK_VAR_MINOR   1

struct buf;

int mipsramswap_open(dev_t dev, int flag, int mode);
int mipsramswap_close(dev_t dev, int flag, int mode);
void mipsramswap_strategy(struct buf *bp);
daddr_t mipsramswap_size(dev_t dev);
int mipsramswap_ioctl(dev_t dev, u_int cmd, caddr_t addr, int flag);
void mipsramswap_discard(size_t blkno, size_t nblocks);

#endif
