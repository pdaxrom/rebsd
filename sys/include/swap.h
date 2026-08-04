#ifndef _SWAP_H
#define _SWAP_H

#ifdef KERNEL
#include "ioctl.h"
#else
#include <sys/ioctl.h>
#endif

#define TFALLOC _IOWR('s',1,off_t)

#ifdef KERNEL

#define SWAP_CONFIG_ALLOW_RAW   0x01

struct swap_config_info {
    size_t      sci_usable_blocks;
    unsigned    sci_linux_format;
    unsigned    sci_badpages;
};

int swap_configure(dev_t, int, struct swap_config_info *);

extern int swopen(dev_t dev, int mode, int flag);
extern int swclose(dev_t dev, int mode, int flag);
extern void swstrategy(register struct buf *bp);
extern daddr_t swsize(dev_t dev);
extern int swcread(dev_t dev, register struct uio *uio, int flag);
extern int swcwrite(dev_t dev, register struct uio *uio, int flag);
extern int swcioctl (dev_t dev, register u_int cmd, caddr_t addr, int flag);
extern int swcopen(dev_t dev, int mode, int flag);
extern int swcclose(dev_t dev, int mode, int flag);

#endif

#endif
