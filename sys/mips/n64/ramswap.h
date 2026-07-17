#ifndef _N64_RAMSWAP_H_
#define _N64_RAMSWAP_H_

#include <machine/layout.h>

#define N64_RAMSWAP_MAJOR       1
#define N64_RAMSWAP_MINOR       0
#define N64_RAMDISK_VAR_MINOR   1
#define N64_BASE_SWAP_KBYTES    (N64_BASE_SWAP_BYTES >> 10)

struct buf;

int n64ramswap_open(dev_t dev, int flag, int mode);
int n64ramswap_close(dev_t dev, int flag, int mode);
void n64ramswap_strategy(struct buf *bp);
daddr_t n64ramswap_size(dev_t dev);
int n64ramswap_ioctl(dev_t dev, u_int cmd, caddr_t addr, int flag);
#ifdef N64_ZSWAP
void n64zswap_free(size_t blkno, size_t nblocks);
#endif

#endif /* _N64_RAMSWAP_H_ */
