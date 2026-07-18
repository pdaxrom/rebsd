#include <sys/param.h>
#include <sys/buf.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/disk.h>
#include <machine/n64.h>
#include <machine/ramswap.h>
#ifdef MIPS_ZSWAP_ENABLED
#include <mips/common/zswap.h>
#if DEV_BSIZE != MIPS_ZSWAP_BLOCK_BYTES || \
    DEV_BSHIFT != MIPS_ZSWAP_BLOCK_SHIFT
#error "MIPS zswap block geometry must match the kernel device block size"
#endif
#endif

static unsigned ramswap_base;
static unsigned ramswap_bytes;
static unsigned ramswap_store_bytes;
static unsigned ramdisk_var_base;
static unsigned ramdisk_var_bytes;
#ifdef MIPS_ZSWAP_ENABLED
static struct mips_zswap ramswap_zswap;
#endif

static void
ramswap_configure(void)
{
    unsigned memsize;
    unsigned pool_base;
    unsigned pool_bytes;
    unsigned var_bytes;

    if (ramswap_bytes != 0)
        return;

    memsize = n64_rdram_size();
    if (memsize >= N64_RDRAM_SIZE_8M) {
#ifdef N64_DEBUG_USERMEM_4M
        pool_base = N64_USER_PHYS_END;
#else
        pool_base = N64_EXPANSION_SWAP_PHYS_START;
#endif
        pool_bytes = memsize - pool_base;
        var_bytes = N64_RAMDISK_8M_VAR_BYTES;
    } else {
        pool_base = N64_BASE_SWAP_PHYS_START;
        pool_bytes = N64_BASE_SWAP_BYTES;
        var_bytes = N64_RAMDISK_4M_VAR_BYTES;
    }

    if (var_bytes >= pool_bytes)
        var_bytes = 0;
    ramdisk_var_base = pool_base;
    ramdisk_var_bytes = var_bytes;
    ramswap_base = ramdisk_var_base + ramdisk_var_bytes;
    ramswap_store_bytes = pool_bytes - ramdisk_var_bytes;
    ramswap_bytes = ramswap_store_bytes;
#ifdef MIPS_ZSWAP_ENABLED
    ramswap_bytes = mips_zswap_logical_bytes(ramswap_store_bytes);
    if (mips_zswap_init(&ramswap_zswap,
        N64_PHYS_TO_KSEG1(ramswap_base), ramswap_store_bytes) != 0)
        ramswap_bytes = 0;
#endif
}

static int
ramregion(dev_t dev, unsigned *base, unsigned *bytes)
{
    ramswap_configure();

    switch (minor(dev)) {
    case N64_RAMSWAP_MINOR:
        *base = ramswap_base;
        *bytes = ramswap_bytes;
        return 0;
    case N64_RAMDISK_VAR_MINOR:
        *base = ramdisk_var_base;
        *bytes = ramdisk_var_bytes;
        return 0;
    default:
        *base = 0;
        *bytes = 0;
        return ENXIO;
    }
}

int
n64ramswap_open(dev_t dev, int flag, int mode)
{
    unsigned base;
    unsigned bytes;
    int error;

    error = ramregion(dev, &base, &bytes);
    if (error != 0)
        return error;
    return bytes != 0 ? 0 : ENXIO;
}

int
n64ramswap_close(dev_t dev, int flag, int mode)
{
    return 0;
}

daddr_t
n64ramswap_size(dev_t dev)
{
    unsigned base;
    unsigned bytes;

    if (ramregion(dev, &base, &bytes) != 0)
        return 0;
    return bytes >> DEV_BSHIFT;
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

#ifdef MIPS_ZSWAP_ENABLED
    if (minor(bp->b_dev) == N64_RAMSWAP_MINOR) {
        if (bp->b_flags & B_READ)
            error = mips_zswap_read(&ramswap_zswap, offset,
                bp->b_addr, nbytes);
        else
            error = mips_zswap_write(&ramswap_zswap, offset,
                bp->b_addr, nbytes);
        if (error != 0) {
            ramswap_done_error(bp, error);
            return;
        }
        biodone(bp);
        return;
    }
#endif

    store = N64_PHYS_TO_KSEG1(base + offset);
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

void
n64ramswap_discard(size_t blkno, size_t nblocks)
{
#ifdef MIPS_ZSWAP_ENABLED
    ramswap_configure();
    mips_zswap_discard(&ramswap_zswap, blkno, nblocks);
#else
    (void)blkno;
    (void)nblocks;
#endif
}

#ifdef MIPS_ZSWAP_ENABLED
int
n64ramswap_get_zswap_stats(struct mips_zswap_stats *stats)
{
    ramswap_configure();
    return mips_zswap_get_stats(&ramswap_zswap, stats);
}
#endif

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
