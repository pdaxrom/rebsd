#include <sys/param.h>
#include <sys/buf.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/disk.h>
#include <disk/ramdisk.h>
#include <machine/layout.h>
#include <machine/ramswap.h>
#ifdef ZSWAP_ENABLED
#include <vm/zswap.h>
#if DEV_BSIZE != ZSWAP_BLOCK_BYTES || \
    DEV_BSHIFT != ZSWAP_BLOCK_SHIFT
#error "zswap block geometry must match the kernel device block size"
#endif

static struct zswap ramswap_zswap;
static unsigned ramswap_logical_bytes;

static void
ramswap_configure(void)
{
    if (ramswap_logical_bytes != 0)
        return;
    ramswap_logical_bytes = zswap_logical_bytes(MALTA_RAMSWAP_BYTES);
    if (zswap_init(&ramswap_zswap,
        MIPS_PHYS_TO_KSEG1(MALTA_RAMSWAP_PHYS_START),
        MALTA_RAMSWAP_BYTES) != 0)
        ramswap_logical_bytes = 0;
}
#endif

static const struct ramdisk *
var_ramdisk(void)
{
    static struct ramdisk ramdisk;

    ramdisk.rd_start = MIPS_PHYS_TO_KSEG1(MALTA_RAMDISK_VAR_PHYS_START);
    ramdisk.rd_end = ramdisk.rd_start + MALTA_RAMDISK_VAR_BYTES;
    ramdisk.rd_minor = MIPS_RAMDISK_VAR_MINOR;
    ramdisk.rd_block_shift = DEV_BSHIFT;
    return &ramdisk;
}

static int
ramregion(dev_t dev, unsigned *base, unsigned *bytes)
{
    switch (minor(dev)) {
    case MIPS_RAMSWAP_MINOR:
        *base = MALTA_RAMSWAP_PHYS_START;
#ifdef ZSWAP_ENABLED
        ramswap_configure();
        *bytes = ramswap_logical_bytes;
#else
        *bytes = MALTA_RAMSWAP_BYTES;
#endif
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
    int error;

    if (minor(dev) == MIPS_RAMDISK_VAR_MINOR)
        return ramdisk_bdev_open(var_ramdisk(), dev, flag, mode);
    error = ramregion(dev, &base, &bytes);

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

    if (minor(dev) == MIPS_RAMDISK_VAR_MINOR)
        return ramdisk_bdev_size(var_ramdisk(), dev);
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

    if (minor(bp->b_dev) == MIPS_RAMDISK_VAR_MINOR) {
        ramdisk_bdev_strategy(var_ramdisk(), bp);
        return;
    }
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

#ifdef ZSWAP_ENABLED
    if (minor(bp->b_dev) == MIPS_RAMSWAP_MINOR) {
        if (bp->b_flags & B_READ)
            error = zswap_read(&ramswap_zswap, offset,
                bp->b_addr, nbytes);
        else
            error = zswap_write(&ramswap_zswap, offset,
                bp->b_addr, nbytes);
        if (error != 0) {
            ramswap_done_error(bp, error);
            return;
        }
        biodone(bp);
        return;
    }
#endif

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
    if (minor(dev) == MIPS_RAMDISK_VAR_MINOR)
        return ramdisk_bdev_ioctl(var_ramdisk(), dev, cmd, addr, flag);
    switch (cmd) {
    case DIOCGETMEDIASIZE:
        *(int *)addr = mipsramswap_size(dev);
        return 0;
#ifdef ZSWAP_ENABLED
    case DIOCDISCARD: {
        const struct disk_discard *range;

        if (minor(dev) != MIPS_RAMSWAP_MINOR)
            return EINVAL;
        range = (const struct disk_discard *)addr;
        if ((range->dd_offset | range->dd_length) & 1u)
            return EINVAL;
        ramswap_configure();
        zswap_discard(&ramswap_zswap,
            (size_t)(range->dd_offset >> 1),
            (size_t)(range->dd_length >> 1));
        return 0;
    }
#endif
    default:
        return EINVAL;
    }
}
