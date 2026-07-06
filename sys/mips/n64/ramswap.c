#include <sys/param.h>
#include <sys/buf.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/disk.h>
#include <machine/n64.h>
#include <machine/ramswap.h>

static unsigned ramswap_base;
static unsigned ramswap_bytes;
static unsigned ramswap_store_bytes;
static unsigned ramdisk_var_base;
static unsigned ramdisk_var_bytes;

static void ramswap_configure(void);
static void ramswap_done_error(struct buf *bp, int error);

#ifdef N64_ZSWAP
#define N64_ZSWAP_RATIO             2
#define N64_ZSWAP_UNIT_BYTES        256
#define N64_ZSWAP_UNITS_PER_BLOCK   (DEV_BSIZE / N64_ZSWAP_UNIT_BYTES)
#define N64_ZSWAP_MAX_BLOCKS        8192
#define N64_ZSWAP_MAX_UNITS         16384
#define N64_ZSWAP_HASH_SIZE         4096

#define N64_ZSWAP_F_VALID           0x0001
#define N64_ZSWAP_F_ZERO            0x0002
#define N64_ZSWAP_F_RAW             0x0004

struct n64_zswap_entry {
    u_short unit;
    u_short units;
    u_short length;
    u_short flags;
};

static struct n64_zswap_entry zswap_entry[N64_ZSWAP_MAX_BLOCKS];
static u_char zswap_units[(N64_ZSWAP_MAX_UNITS + 7) / 8];
static u_char zswap_block[DEV_BSIZE];
static u_char zswap_comp[DEV_BSIZE];
static u_short zswap_hash[N64_ZSWAP_HASH_SIZE];
static unsigned zswap_blocks;
static unsigned zswap_phys_units;
static int zswap_configured;

static unsigned
n64zswap_logical_bytes(unsigned store_bytes)
{
    unsigned blocks;

    blocks = (store_bytes >> DEV_BSHIFT) * N64_ZSWAP_RATIO;
    if (blocks > N64_ZSWAP_MAX_BLOCKS)
        blocks = N64_ZSWAP_MAX_BLOCKS;
    return blocks << DEV_BSHIFT;
}

static int
n64zswap_unit_used(unsigned unit)
{
    return (zswap_units[unit >> 3] & (1 << (unit & 7))) != 0;
}

static void
n64zswap_set_unit(unsigned unit, int used)
{
    if (used)
        zswap_units[unit >> 3] |= 1 << (unit & 7);
    else
        zswap_units[unit >> 3] &= ~(1 << (unit & 7));
}

static int
n64zswap_alloc_units(unsigned units, unsigned *unitp)
{
    unsigned run;
    unsigned start;
    unsigned unit;
    unsigned i;

    if (units == 0 || units > zswap_phys_units)
        return ENOSPC;

    run = 0;
    start = 0;
    for (unit = 0; unit < zswap_phys_units; ++unit) {
        if (n64zswap_unit_used(unit)) {
            run = 0;
            continue;
        }
        if (run == 0)
            start = unit;
        if (++run == units) {
            for (i = 0; i < units; ++i)
                n64zswap_set_unit(start + i, 1);
            *unitp = start;
            return 0;
        }
    }
    return ENOSPC;
}

static void
n64zswap_free_units(unsigned unit, unsigned units)
{
    unsigned i;

    for (i = 0; i < units && unit + i < zswap_phys_units; ++i)
        n64zswap_set_unit(unit + i, 0);
}

static void
n64zswap_free_entry(unsigned block)
{
    struct n64_zswap_entry *entry;

    if (block >= zswap_blocks)
        return;

    entry = &zswap_entry[block];
    if ((entry->flags & N64_ZSWAP_F_VALID) == 0)
        return;

    if ((entry->flags & N64_ZSWAP_F_ZERO) == 0)
        n64zswap_free_units(entry->unit, entry->units);
    entry->unit = 0;
    entry->units = 0;
    entry->length = 0;
    entry->flags = 0;
}

static void
n64zswap_configure(void)
{
    unsigned i;

    ramswap_configure();
    if (zswap_configured)
        return;

    zswap_blocks = ramswap_bytes >> DEV_BSHIFT;
    if (zswap_blocks > N64_ZSWAP_MAX_BLOCKS)
        zswap_blocks = N64_ZSWAP_MAX_BLOCKS;
    zswap_phys_units = ramswap_store_bytes / N64_ZSWAP_UNIT_BYTES;
    if (zswap_phys_units > N64_ZSWAP_MAX_UNITS)
        zswap_phys_units = N64_ZSWAP_MAX_UNITS;

    for (i = 0; i < N64_ZSWAP_MAX_BLOCKS; ++i) {
        zswap_entry[i].unit = 0;
        zswap_entry[i].units = 0;
        zswap_entry[i].length = 0;
        zswap_entry[i].flags = 0;
    }
    for (i = 0; i < sizeof(zswap_units); ++i)
        zswap_units[i] = 0;

    zswap_configured = 1;
}

void
n64zswap_free(size_t blkno, size_t nblocks)
{
    size_t block;

    n64zswap_configure();
    for (block = 0; block < nblocks; ++block)
        n64zswap_free_entry((unsigned)blkno + block);
}

static int
n64zswap_is_zero(const u_char *src)
{
    unsigned i;

    for (i = 0; i < DEV_BSIZE; ++i)
        if (src[i] != 0)
            return 0;
    return 1;
}

static int
n64zswap_emit_literals(const u_char *src, u_char *dst, unsigned *op,
    unsigned start, unsigned len)
{
    unsigned i;

    if (len == 0)
        return 0;
    if (*op + 1 + len > DEV_BSIZE)
        return ENOSPC;
    dst[(*op)++] = (u_char)(len - 1);
    for (i = 0; i < len; ++i)
        dst[(*op)++] = src[start + i];
    return 0;
}

static unsigned
n64zswap_hash3(const u_char *src)
{
    u_int value;

    value = ((u_int)src[0] << 16) ^ ((u_int)src[1] << 8) ^ src[2];
    return ((value * 2777) >> 9) & (N64_ZSWAP_HASH_SIZE - 1);
}

static unsigned
n64zswap_compress(const u_char *src, u_char *dst)
{
    unsigned ip;
    unsigned op;
    unsigned lit_start;
    unsigned lit_len;
    unsigned i;

    for (i = 0; i < N64_ZSWAP_HASH_SIZE; ++i)
        zswap_hash[i] = 0;

    ip = 0;
    op = 0;
    lit_start = 0;
    lit_len = 0;
    while (ip < DEV_BSIZE) {
        if (ip + 2 < DEV_BSIZE) {
            unsigned h;
            unsigned refpos;
            unsigned ref;
            unsigned off;

            h = n64zswap_hash3(src + ip);
            refpos = zswap_hash[h];
            zswap_hash[h] = (u_short)(ip + 1);
            if (refpos != 0) {
                ref = refpos - 1;
                off = ip - ref - 1;
                if (off < 8192 &&
                    src[ref] == src[ip] &&
                    src[ref + 1] == src[ip + 1] &&
                    src[ref + 2] == src[ip + 2]) {
                    unsigned max_len;
                    unsigned match_len;
                    unsigned enc_len;

                    max_len = DEV_BSIZE - ip;
                    if (max_len > 264)
                        max_len = 264;
                    match_len = 3;
                    while (match_len < max_len &&
                        src[ref + match_len] == src[ip + match_len])
                        ++match_len;
                    if (n64zswap_emit_literals(src, dst, &op,
                        lit_start, lit_len) != 0)
                        return 0;
                    lit_len = 0;
                    enc_len = match_len - 2;
                    if (enc_len < 7) {
                        if (op + 2 > DEV_BSIZE)
                            return 0;
                        dst[op++] = (u_char)((enc_len << 5) |
                            (off >> 8));
                        dst[op++] = (u_char)off;
                    } else {
                        if (op + 3 > DEV_BSIZE)
                            return 0;
                        dst[op++] = (u_char)((7 << 5) | (off >> 8));
                        dst[op++] = (u_char)(enc_len - 7);
                        dst[op++] = (u_char)off;
                    }
                    ip += match_len;
                    lit_start = ip;
                    continue;
                }
            }
        }
        if (lit_len == 0)
            lit_start = ip;
        ++ip;
        if (++lit_len == 32) {
            if (n64zswap_emit_literals(src, dst, &op, lit_start,
                lit_len) != 0)
                return 0;
            lit_len = 0;
            lit_start = ip;
        }
    }
    if (n64zswap_emit_literals(src, dst, &op, lit_start, lit_len) != 0)
        return 0;
    return op;
}

static int
n64zswap_decompress(const u_char *src, unsigned srclen, u_char *dst)
{
    unsigned ip;
    unsigned op;

    ip = 0;
    op = 0;
    while (ip < srclen) {
        unsigned ctrl;

        ctrl = src[ip++];
        if (ctrl < 32) {
            unsigned len;
            unsigned i;

            len = ctrl + 1;
            if (ip + len > srclen || op + len > DEV_BSIZE)
                return EIO;
            for (i = 0; i < len; ++i)
                dst[op++] = src[ip++];
        } else {
            unsigned len;
            unsigned off;
            unsigned ref;
            unsigned i;

            len = ctrl >> 5;
            off = (ctrl & 0x1f) << 8;
            if (len == 7) {
                if (ip >= srclen)
                    return EIO;
                len += src[ip++];
            }
            if (ip >= srclen)
                return EIO;
            off |= src[ip++];
            if (off + 1 > op)
                return EIO;
            ref = op - off - 1;
            len += 2;
            if (op + len > DEV_BSIZE)
                return EIO;
            for (i = 0; i < len; ++i)
                dst[op++] = dst[ref++];
        }
    }
    return op == DEV_BSIZE ? 0 : EIO;
}

static void
n64zswap_copy_to_store(unsigned unit, const u_char *src, unsigned len)
{
    volatile u_char *store;
    unsigned i;

    store = (volatile u_char *)N64_PHYS_TO_KSEG1(ramswap_base +
        unit * N64_ZSWAP_UNIT_BYTES);
    for (i = 0; i < len; ++i)
        *store++ = src[i];
}

static void
n64zswap_copy_from_store(unsigned unit, u_char *dst, unsigned len)
{
    volatile u_char *store;
    unsigned i;

    store = (volatile u_char *)N64_PHYS_TO_KSEG1(ramswap_base +
        unit * N64_ZSWAP_UNIT_BYTES);
    for (i = 0; i < len; ++i)
        dst[i] = *store++;
}

static int
n64zswap_read_block(unsigned block, u_char *dst)
{
    struct n64_zswap_entry *entry;
    unsigned i;

    if (block >= zswap_blocks)
        return EINVAL;

    entry = &zswap_entry[block];
    if ((entry->flags & N64_ZSWAP_F_VALID) == 0 ||
        (entry->flags & N64_ZSWAP_F_ZERO)) {
        for (i = 0; i < DEV_BSIZE; ++i)
            dst[i] = 0;
        return 0;
    }

    if (entry->flags & N64_ZSWAP_F_RAW) {
        n64zswap_copy_from_store(entry->unit, dst, DEV_BSIZE);
        return 0;
    }

    n64zswap_copy_from_store(entry->unit, zswap_comp, entry->length);
    return n64zswap_decompress(zswap_comp, entry->length, dst);
}

static int
n64zswap_write_block(unsigned block, const u_char *src)
{
    struct n64_zswap_entry *entry;
    const u_char *store_src;
    unsigned len;
    unsigned units;
    unsigned unit;
    unsigned flags;
    unsigned clen;
    int error;

    if (block >= zswap_blocks)
        return EINVAL;

    entry = &zswap_entry[block];
    if (n64zswap_is_zero(src)) {
        n64zswap_free_entry(block);
        entry->unit = 0;
        entry->units = 0;
        entry->length = 0;
        entry->flags = N64_ZSWAP_F_VALID | N64_ZSWAP_F_ZERO;
        return 0;
    }

    clen = n64zswap_compress(src, zswap_comp);
    if (clen != 0 &&
        howmany(clen, N64_ZSWAP_UNIT_BYTES) < N64_ZSWAP_UNITS_PER_BLOCK) {
        store_src = zswap_comp;
        len = clen;
        units = howmany(clen, N64_ZSWAP_UNIT_BYTES);
        flags = N64_ZSWAP_F_VALID;
    } else {
        store_src = src;
        len = DEV_BSIZE;
        units = N64_ZSWAP_UNITS_PER_BLOCK;
        flags = N64_ZSWAP_F_VALID | N64_ZSWAP_F_RAW;
    }

    error = n64zswap_alloc_units(units, &unit);
    if (error != 0)
        return error;

    n64zswap_copy_to_store(unit, store_src, len);
    n64zswap_free_entry(block);
    entry->unit = (u_short)unit;
    entry->units = (u_short)units;
    entry->length = (u_short)len;
    entry->flags = (u_short)flags;
    return 0;
}

static void
n64zswap_strategy(struct buf *bp)
{
    char *data;
    unsigned bytes;
    unsigned offset;
    unsigned nbytes;
    unsigned requested;
    unsigned done;
    int error;

    n64zswap_configure();
    bytes = ramswap_bytes;
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

    requested = bp->b_bcount;
    nbytes = requested;
    bp->b_resid = 0;
    if (nbytes > bytes - offset) {
        bp->b_resid = nbytes - (bytes - offset);
        nbytes = bytes - offset;
        bp->b_bcount = nbytes;
    }

    data = bp->b_addr;
    done = 0;
    while (done < nbytes) {
        unsigned block;
        unsigned boff;
        unsigned chunk;

        block = offset >> DEV_BSHIFT;
        boff = offset & DEV_BMASK;
        chunk = DEV_BSIZE - boff;
        if (chunk > nbytes - done)
            chunk = nbytes - done;

        if (bp->b_flags & B_READ) {
            if (boff == 0 && chunk == DEV_BSIZE) {
                error = n64zswap_read_block(block, (u_char *)data);
            } else {
                unsigned i;

                error = n64zswap_read_block(block, zswap_block);
                if (error == 0)
                    for (i = 0; i < chunk; ++i)
                        data[i] = zswap_block[boff + i];
            }
        } else {
            if (boff == 0 && chunk == DEV_BSIZE) {
                error = n64zswap_write_block(block, (u_char *)data);
            } else {
                unsigned i;

                error = n64zswap_read_block(block, zswap_block);
                if (error == 0) {
                    for (i = 0; i < chunk; ++i)
                        zswap_block[boff + i] = data[i];
                    error = n64zswap_write_block(block, zswap_block);
                }
            }
        }
        if (error != 0) {
            bp->b_resid = requested - done;
            ramswap_done_error(bp, error);
            return;
        }
        data += chunk;
        offset += chunk;
        done += chunk;
    }

    biodone(bp);
}
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
#ifdef N64_ZSWAP
    ramswap_bytes = n64zswap_logical_bytes(ramswap_store_bytes);
    zswap_configured = 0;
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

#ifdef N64_ZSWAP
    if (minor(bp->b_dev) == N64_RAMSWAP_MINOR) {
        n64zswap_strategy(bp);
        return;
    }
#endif

    error = ramregion(bp->b_dev, &base, &bytes);
    if (error != 0 || bytes == 0) {
        ramswap_done_error(bp, error != 0 ? error : ENXIO);
        return;
    }
    if (bp->b_blkno < 0) {
        ramswap_done_error(bp, EINVAL);
        return;
    }
    if (bp->b_blkno > (daddr_t)(bytes >> DEV_BSHIFT)) {
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

    store = (volatile unsigned char *)N64_PHYS_TO_KSEG1(base + offset);
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
