#include <sys/param.h>
#include <sys/buf.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/disk.h>
#include <machine/n64.h>
#include <machine/ramswap.h>

static unsigned ramswap_base;
static unsigned ramswap_bytes;

static void
ramswap_configure(void)
{
    unsigned memsize;

    if (ramswap_bytes != 0)
        return;

    memsize = n64_rdram_size();
    if (memsize >= N64_RDRAM_SIZE_8M) {
        ramswap_base = N64_EXPANSION_SWAP_PHYS_START;
        ramswap_bytes = memsize - N64_EXPANSION_SWAP_PHYS_START;
    } else {
        ramswap_base = N64_BASE_SWAP_PHYS_START;
        ramswap_bytes = N64_BASE_SWAP_BYTES;
    }
}

int
n64ramswap_open(dev_t dev, int flag, int mode)
{
    if (minor(dev) != N64_RAMSWAP_MINOR)
        return ENXIO;

    ramswap_configure();
    return ramswap_bytes != 0 ? 0 : ENXIO;
}

int
n64ramswap_close(dev_t dev, int flag, int mode)
{
    return 0;
}

daddr_t
n64ramswap_size(dev_t dev)
{
    if (minor(dev) != N64_RAMSWAP_MINOR)
        return 0;

    ramswap_configure();
    return ramswap_bytes >> 10;
}

static void
ramswap_done_error(struct buf *bp, int error)
{
    bp->b_error = error;
    bp->b_flags |= B_ERROR;
    biodone(bp);
}

void
n64ramswap_strategy(struct buf *bp)
{
    volatile unsigned char *store;
    char *data;
    unsigned offset;
    unsigned nbytes;
    unsigned i;

    ramswap_configure();
    if (minor(bp->b_dev) != N64_RAMSWAP_MINOR || ramswap_bytes == 0) {
        ramswap_done_error(bp, ENXIO);
        return;
    }
    if (bp->b_blkno < 0) {
        ramswap_done_error(bp, EINVAL);
        return;
    }
    if (bp->b_blkno > (daddr_t)(ramswap_bytes >> DEV_BSHIFT)) {
        ramswap_done_error(bp, EINVAL);
        return;
    }

    offset = (unsigned)bp->b_blkno << DEV_BSHIFT;
    if (offset >= ramswap_bytes) {
        if (offset == ramswap_bytes) {
            bp->b_resid = bp->b_bcount;
            biodone(bp);
        } else {
            ramswap_done_error(bp, EINVAL);
        }
        return;
    }

    nbytes = bp->b_bcount;
    bp->b_resid = 0;
    if (nbytes > ramswap_bytes - offset) {
        bp->b_resid = nbytes - (ramswap_bytes - offset);
        nbytes = ramswap_bytes - offset;
        bp->b_bcount = nbytes;
    }

    store = (volatile unsigned char *)N64_PHYS_TO_KSEG1(ramswap_base + offset);
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
n64ramswap_ioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
    switch (cmd) {
    case DIOCGETMEDIASIZE:
        *(int *)addr = n64ramswap_size(dev);
        return 0;
    default:
        return EINVAL;
    }
}
