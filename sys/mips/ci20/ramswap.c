#include <sys/param.h>
#include <sys/buf.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/disk.h>
#include <machine/layout.h>
#include <machine/ramswap.h>

static int
ramregion(dev_t dev, unsigned *base, unsigned *bytes)
{
    switch (minor(dev)) {
    case MIPS_RAMSWAP_MINOR:
        *base = CI20_RAMSWAP_PHYS_START;
        *bytes = CI20_RAMSWAP_BYTES;
        return 0;
    case MIPS_RAMDISK_VAR_MINOR:
        *base = CI20_RAMDISK_VAR_PHYS_START;
        *bytes = CI20_RAMDISK_VAR_BYTES;
        return 0;
    default:
        *base = 0;
        *bytes = 0;
        return ENXIO;
    }
}

int
mipsramswap_open(dev_t dev, int flag, int mode)
{
    unsigned base;
    unsigned bytes;
    int error = ramregion(dev, &base, &bytes);

    (void)base;
    return error == 0 && bytes != 0 ? 0 : (error != 0 ? error : ENXIO);
}

int
mipsramswap_close(dev_t dev, int flag, int mode)
{
    return 0;
}

daddr_t
mipsramswap_size(dev_t dev)
{
    unsigned base;
    unsigned bytes;

    if (ramregion(dev, &base, &bytes) != 0)
        return 0;
    return bytes >> 10;
}

static void
ramswap_done_error(struct buf *bp, int error)
{
    bp->b_error = error;
    bp->b_flags |= B_ERROR;
    biodone(bp);
}

void
mipsramswap_strategy(struct buf *bp)
{
    volatile unsigned char *store;
    char *data;
    unsigned base;
    unsigned bytes;
    unsigned offset;
    unsigned nbytes;
    unsigned i;
    int error;

    error = ramregion(bp->b_dev, &base, &bytes);
    if (error != 0 || bytes == 0) {
        ramswap_done_error(bp, error != 0 ? error : ENXIO);
        return;
    }
    if (bp->b_blkno < 0) {
        ramswap_done_error(bp, EINVAL);
        return;
    }

    offset = (unsigned)bp->b_blkno << DEV_BSHIFT;
    if (offset >= bytes) {
        if (offset == bytes) {
            bp->b_resid = bp->b_bcount;
            biodone(bp);
        } else {
            ramswap_done_error(bp, EINVAL);
        }
        return;
    }

    nbytes = bp->b_bcount;
    bp->b_resid = 0;
    if (nbytes > bytes - offset) {
        bp->b_resid = nbytes - (bytes - offset);
        nbytes = bytes - offset;
        bp->b_bcount = nbytes;
    }

    store = MIPS_PHYS_TO_KSEG1(base + offset);
    data = bp->b_addr;
    if (bp->b_flags & B_READ) {
        for (i = 0; i < nbytes; ++i)
            *data++ = *store++;
    } else {
        for (i = 0; i < nbytes; ++i)
            *store++ = *data++;
    }

    biodone(bp);
}

int
mipsramswap_ioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
    switch (cmd) {
    case DIOCGETMEDIASIZE:
        *(int *)addr = mipsramswap_size(dev);
        return 0;
    default:
        return EINVAL;
    }
}
