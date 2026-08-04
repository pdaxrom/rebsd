#ifndef _SWAP_H
#define _SWAP_H

#ifdef KERNEL
#include "ioctl.h"
#else
#include <sys/ioctl.h>
#endif

#define TFALLOC _IOWR('s',1,off_t)

#define SWAP_DEVICE_INFO_LINUX    0x01u
#define SWAP_DEVICE_INFO_DRAINING 0x02u

/* Read-only description exported by vm.swap_devices. */
struct swap_device_info {
    dev_t       sdi_dev;
    u_long      sdi_total_blocks;
    u_long      sdi_used_blocks;
    u_int       sdi_flags;
    u_int       sdi_badpages;
};

#ifdef KERNEL

struct buf;
struct uio;

struct swap_config_info {
    size_t      sci_usable_blocks;
    unsigned    sci_linux_format;
    unsigned    sci_badpages;
};

int swap_configure(dev_t, struct swap_config_info *);
int swap_unconfigure(dev_t);
size_t swap_slot_alloc(void);
void swap_slot_free(size_t);
int swap_slot_draining(size_t);
size_t swap_total_blocks(void);
size_t swap_free_blocks(void);
unsigned swap_device_count(void);
int swap_device_snapshot(unsigned, struct swap_device_info *);

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
