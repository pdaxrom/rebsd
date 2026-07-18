/* Shared compressed RAM-swap store for memory-constrained MIPS boards. */

#include <sys/errno.h>
#include <mips/common/zswap.h>

#define MIPS_ZSWAP_UNITS_PER_BLOCK \
    (MIPS_ZSWAP_BLOCK_BYTES / MIPS_ZSWAP_UNIT_BYTES)
#define MIPS_ZSWAP_HOWMANY(value, unit) (((value) + (unit) - 1) / (unit))

#define MIPS_ZSWAP_F_VALID         0x0001
#define MIPS_ZSWAP_F_ZERO          0x0002
#define MIPS_ZSWAP_F_RAW           0x0004

static void
mips_zswap_zero(void *address, unsigned bytes)
{
    u_char *data;

    data = address;
    while (bytes-- != 0)
        *data++ = 0;
}

unsigned
mips_zswap_logical_bytes(unsigned store_bytes)
{
    unsigned blocks;

    blocks = (store_bytes >> MIPS_ZSWAP_BLOCK_SHIFT) * MIPS_ZSWAP_RATIO;
    if (blocks > MIPS_ZSWAP_MAX_BLOCKS)
        blocks = MIPS_ZSWAP_MAX_BLOCKS;
    return blocks << MIPS_ZSWAP_BLOCK_SHIFT;
}

int
mips_zswap_init(struct mips_zswap *zswap, volatile void *store,
    unsigned store_bytes)
{
    unsigned logical_bytes;
    unsigned phys_units;

    if (zswap == 0 || store == 0 ||
        store_bytes < MIPS_ZSWAP_BLOCK_BYTES)
        return EINVAL;
    logical_bytes = mips_zswap_logical_bytes(store_bytes);
    phys_units = store_bytes / MIPS_ZSWAP_UNIT_BYTES;
    if (logical_bytes == 0 || phys_units == 0)
        return EINVAL;
    if (phys_units > MIPS_ZSWAP_MAX_UNITS)
        phys_units = MIPS_ZSWAP_MAX_UNITS;

    mips_zswap_zero(zswap, sizeof(*zswap));
    zswap->mz_store = store;
    zswap->mz_store_bytes = store_bytes;
    zswap->mz_logical_bytes = logical_bytes;
    zswap->mz_blocks = logical_bytes >> MIPS_ZSWAP_BLOCK_SHIFT;
    zswap->mz_phys_units = phys_units;
    zswap->mz_initialized = 1;
    return 0;
}

static int
mips_zswap_unit_used(const struct mips_zswap *zswap, unsigned unit)
{
    return (zswap->mz_units[unit >> 3] & (1 << (unit & 7))) != 0;
}

static void
mips_zswap_set_unit(struct mips_zswap *zswap, unsigned unit, int used)
{
    if (used)
        zswap->mz_units[unit >> 3] |= 1 << (unit & 7);
    else
        zswap->mz_units[unit >> 3] &= ~(1 << (unit & 7));
}

static int
mips_zswap_alloc_units(struct mips_zswap *zswap, unsigned units,
    unsigned *unitp)
{
    unsigned run;
    unsigned start;
    unsigned unit;
    unsigned i;

    if (units == 0 || units > zswap->mz_phys_units || unitp == 0)
        return ENOSPC;

    run = 0;
    start = 0;
    for (unit = 0; unit < zswap->mz_phys_units; ++unit) {
        if (mips_zswap_unit_used(zswap, unit)) {
            run = 0;
            continue;
        }
        if (run == 0)
            start = unit;
        if (++run == units) {
            for (i = 0; i < units; ++i)
                mips_zswap_set_unit(zswap, start + i, 1);
            *unitp = start;
            return 0;
        }
    }
    return ENOSPC;
}

static void
mips_zswap_free_units(struct mips_zswap *zswap, unsigned unit,
    unsigned units)
{
    unsigned i;

    for (i = 0; i < units && unit + i < zswap->mz_phys_units; ++i)
        mips_zswap_set_unit(zswap, unit + i, 0);
}

static void
mips_zswap_free_entry(struct mips_zswap *zswap, unsigned block)
{
    struct mips_zswap_entry *entry;

    if (block >= zswap->mz_blocks)
        return;
    entry = &zswap->mz_entry[block];
    if ((entry->flags & MIPS_ZSWAP_F_VALID) == 0)
        return;
    if ((entry->flags & MIPS_ZSWAP_F_ZERO) == 0)
        mips_zswap_free_units(zswap, entry->unit, entry->units);
    entry->unit = 0;
    entry->units = 0;
    entry->length = 0;
    entry->flags = 0;
}

void
mips_zswap_discard(struct mips_zswap *zswap, size_t blkno,
    size_t nblocks)
{
    size_t block;

    if (zswap == 0 || !zswap->mz_initialized)
        return;
    for (block = 0; block < nblocks; ++block) {
        if (blkno + block >= zswap->mz_blocks)
            break;
        mips_zswap_free_entry(zswap, (unsigned)(blkno + block));
    }
}

static int
mips_zswap_is_zero(const u_char *src)
{
    unsigned i;

    for (i = 0; i < MIPS_ZSWAP_BLOCK_BYTES; ++i)
        if (src[i] != 0)
            return 0;
    return 1;
}

static int
mips_zswap_emit_literals(const u_char *src, u_char *dst, unsigned *op,
    unsigned start, unsigned len)
{
    unsigned i;

    if (len == 0)
        return 0;
    if (*op + 1 + len > MIPS_ZSWAP_BLOCK_BYTES)
        return ENOSPC;
    dst[(*op)++] = (u_char)(len - 1);
    for (i = 0; i < len; ++i)
        dst[(*op)++] = src[start + i];
    return 0;
}

static unsigned
mips_zswap_hash3(const u_char *src)
{
    u_int value;

    value = ((u_int)src[0] << 16) ^ ((u_int)src[1] << 8) ^ src[2];
    return ((value * 2777) >> 9) & (MIPS_ZSWAP_HASH_SIZE - 1);
}

static unsigned
mips_zswap_compress(struct mips_zswap *zswap, const u_char *src,
    u_char *dst)
{
    unsigned ip;
    unsigned op;
    unsigned lit_start;
    unsigned lit_len;
    unsigned i;

    for (i = 0; i < MIPS_ZSWAP_HASH_SIZE; ++i)
        zswap->mz_hash[i] = 0;

    ip = 0;
    op = 0;
    lit_start = 0;
    lit_len = 0;
    while (ip < MIPS_ZSWAP_BLOCK_BYTES) {
        if (ip + 2 < MIPS_ZSWAP_BLOCK_BYTES) {
            unsigned h;
            unsigned refpos;
            unsigned ref;
            unsigned off;

            h = mips_zswap_hash3(src + ip);
            refpos = zswap->mz_hash[h];
            zswap->mz_hash[h] = (u_short)(ip + 1);
            if (refpos != 0) {
                ref = refpos - 1;
                off = ip - ref - 1;
                if (off < 8192 && src[ref] == src[ip] &&
                    src[ref + 1] == src[ip + 1] &&
                    src[ref + 2] == src[ip + 2]) {
                    unsigned max_len;
                    unsigned match_len;
                    unsigned enc_len;

                    max_len = MIPS_ZSWAP_BLOCK_BYTES - ip;
                    if (max_len > 264)
                        max_len = 264;
                    match_len = 3;
                    while (match_len < max_len &&
                        src[ref + match_len] == src[ip + match_len])
                        ++match_len;
                    if (mips_zswap_emit_literals(src, dst, &op,
                        lit_start, lit_len) != 0)
                        return 0;
                    lit_len = 0;
                    enc_len = match_len - 2;
                    if (enc_len < 7) {
                        if (op + 2 > MIPS_ZSWAP_BLOCK_BYTES)
                            return 0;
                        dst[op++] = (u_char)((enc_len << 5) |
                            (off >> 8));
                        dst[op++] = (u_char)off;
                    } else {
                        if (op + 3 > MIPS_ZSWAP_BLOCK_BYTES)
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
            if (mips_zswap_emit_literals(src, dst, &op, lit_start,
                lit_len) != 0)
                return 0;
            lit_len = 0;
            lit_start = ip;
        }
    }
    if (mips_zswap_emit_literals(src, dst, &op, lit_start, lit_len) != 0)
        return 0;
    return op;
}

static int
mips_zswap_decompress(const u_char *src, unsigned srclen, u_char *dst)
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
            if (ip + len > srclen ||
                op + len > MIPS_ZSWAP_BLOCK_BYTES)
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
            if (op + len > MIPS_ZSWAP_BLOCK_BYTES)
                return EIO;
            for (i = 0; i < len; ++i)
                dst[op++] = dst[ref++];
        }
    }
    return op == MIPS_ZSWAP_BLOCK_BYTES ? 0 : EIO;
}

static void
mips_zswap_copy_to_store(struct mips_zswap *zswap, unsigned unit,
    const u_char *src, unsigned len)
{
    volatile u_char *store;
    unsigned i;

    store = zswap->mz_store + unit * MIPS_ZSWAP_UNIT_BYTES;
    for (i = 0; i < len; ++i)
        *store++ = src[i];
}

static void
mips_zswap_copy_from_store(struct mips_zswap *zswap, unsigned unit,
    u_char *dst, unsigned len)
{
    volatile u_char *store;
    unsigned i;

    store = zswap->mz_store + unit * MIPS_ZSWAP_UNIT_BYTES;
    for (i = 0; i < len; ++i)
        dst[i] = *store++;
}

static int
mips_zswap_read_block(struct mips_zswap *zswap, unsigned block,
    u_char *dst)
{
    struct mips_zswap_entry *entry;
    unsigned i;

    if (block >= zswap->mz_blocks)
        return EINVAL;
    entry = &zswap->mz_entry[block];
    if ((entry->flags & MIPS_ZSWAP_F_VALID) == 0 ||
        (entry->flags & MIPS_ZSWAP_F_ZERO)) {
        for (i = 0; i < MIPS_ZSWAP_BLOCK_BYTES; ++i)
            dst[i] = 0;
        return 0;
    }
    if (entry->flags & MIPS_ZSWAP_F_RAW) {
        mips_zswap_copy_from_store(zswap, entry->unit, dst,
            MIPS_ZSWAP_BLOCK_BYTES);
        return 0;
    }
    mips_zswap_copy_from_store(zswap, entry->unit, zswap->mz_comp,
        entry->length);
    return mips_zswap_decompress(zswap->mz_comp, entry->length, dst);
}

static int
mips_zswap_write_block(struct mips_zswap *zswap, unsigned block,
    const u_char *src)
{
    struct mips_zswap_entry *entry;
    const u_char *store_src;
    unsigned old_units;
    unsigned old_unit;
    unsigned len;
    unsigned units;
    unsigned unit;
    unsigned flags;
    unsigned clen;
    int error;

    if (block >= zswap->mz_blocks)
        return EINVAL;
    entry = &zswap->mz_entry[block];
    if (mips_zswap_is_zero(src)) {
        mips_zswap_free_entry(zswap, block);
        entry->flags = MIPS_ZSWAP_F_VALID | MIPS_ZSWAP_F_ZERO;
        return 0;
    }

    clen = mips_zswap_compress(zswap, src, zswap->mz_comp);
    if (clen != 0 &&
        MIPS_ZSWAP_HOWMANY(clen, MIPS_ZSWAP_UNIT_BYTES) <
        MIPS_ZSWAP_UNITS_PER_BLOCK) {
        store_src = zswap->mz_comp;
        len = clen;
        units = MIPS_ZSWAP_HOWMANY(clen, MIPS_ZSWAP_UNIT_BYTES);
        flags = MIPS_ZSWAP_F_VALID;
    } else {
        store_src = src;
        len = MIPS_ZSWAP_BLOCK_BYTES;
        units = MIPS_ZSWAP_UNITS_PER_BLOCK;
        flags = MIPS_ZSWAP_F_VALID | MIPS_ZSWAP_F_RAW;
    }

    if ((entry->flags & MIPS_ZSWAP_F_VALID) != 0 &&
        (entry->flags & MIPS_ZSWAP_F_ZERO) == 0 &&
        entry->units >= units) {
        unit = entry->unit;
        old_units = entry->units;
        mips_zswap_copy_to_store(zswap, unit, store_src, len);
        if (old_units > units)
            mips_zswap_free_units(zswap, unit + units,
                old_units - units);
    } else {
        error = mips_zswap_alloc_units(zswap, units, &unit);
        if (error != 0)
            return error;
        mips_zswap_copy_to_store(zswap, unit, store_src, len);
        old_unit = entry->unit;
        old_units = entry->units;
        if ((entry->flags & MIPS_ZSWAP_F_VALID) != 0 &&
            (entry->flags & MIPS_ZSWAP_F_ZERO) == 0)
            mips_zswap_free_units(zswap, old_unit, old_units);
    }

    entry->unit = (u_short)unit;
    entry->units = (u_short)units;
    entry->length = (u_short)len;
    entry->flags = (u_short)flags;
    return 0;
}

static int
mips_zswap_range_valid(const struct mips_zswap *zswap, unsigned offset,
    unsigned bytes)
{
    return zswap != 0 && zswap->mz_initialized &&
        offset <= zswap->mz_logical_bytes &&
        bytes <= zswap->mz_logical_bytes - offset;
}

int
mips_zswap_read(struct mips_zswap *zswap, unsigned offset, void *buffer,
    unsigned bytes)
{
    u_char *data;
    unsigned done;
    int error;

    if (buffer == 0 || !mips_zswap_range_valid(zswap, offset, bytes))
        return EINVAL;
    data = buffer;
    done = 0;
    while (done < bytes) {
        unsigned block;
        unsigned boff;
        unsigned chunk;
        unsigned i;

        block = offset >> MIPS_ZSWAP_BLOCK_SHIFT;
        boff = offset & MIPS_ZSWAP_BLOCK_MASK;
        chunk = MIPS_ZSWAP_BLOCK_BYTES - boff;
        if (chunk > bytes - done)
            chunk = bytes - done;
        if (boff == 0 && chunk == MIPS_ZSWAP_BLOCK_BYTES) {
            error = mips_zswap_read_block(zswap, block, data);
        } else {
            error = mips_zswap_read_block(zswap, block, zswap->mz_block);
            if (error == 0)
                for (i = 0; i < chunk; ++i)
                    data[i] = zswap->mz_block[boff + i];
        }
        if (error != 0)
            return error;
        data += chunk;
        offset += chunk;
        done += chunk;
    }
    return 0;
}

int
mips_zswap_write(struct mips_zswap *zswap, unsigned offset,
    const void *buffer, unsigned bytes)
{
    const u_char *data;
    unsigned done;
    int error;

    if (buffer == 0 || !mips_zswap_range_valid(zswap, offset, bytes))
        return EINVAL;
    data = buffer;
    done = 0;
    while (done < bytes) {
        unsigned block;
        unsigned boff;
        unsigned chunk;
        unsigned i;

        block = offset >> MIPS_ZSWAP_BLOCK_SHIFT;
        boff = offset & MIPS_ZSWAP_BLOCK_MASK;
        chunk = MIPS_ZSWAP_BLOCK_BYTES - boff;
        if (chunk > bytes - done)
            chunk = bytes - done;
        if (boff == 0 && chunk == MIPS_ZSWAP_BLOCK_BYTES) {
            error = mips_zswap_write_block(zswap, block, data);
        } else {
            error = mips_zswap_read_block(zswap, block, zswap->mz_block);
            if (error == 0) {
                for (i = 0; i < chunk; ++i)
                    zswap->mz_block[boff + i] = data[i];
                error = mips_zswap_write_block(zswap, block,
                    zswap->mz_block);
            }
        }
        if (error != 0)
            return error;
        data += chunk;
        offset += chunk;
        done += chunk;
    }
    return 0;
}
