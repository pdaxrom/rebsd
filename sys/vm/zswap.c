/* Shared compressed RAM-swap store for memory-constrained systems. */

#include <sys/errno.h>
#include <vm/zswap.h>

#define ZSWAP_UNITS_PER_BLOCK \
    (ZSWAP_BLOCK_BYTES / ZSWAP_UNIT_BYTES)
#define ZSWAP_HOWMANY(value, unit) (((value) + (unit) - 1) / (unit))

#define ZSWAP_F_VALID         0x0001
#define ZSWAP_F_ZERO          0x0002
#define ZSWAP_F_RAW           0x0004
#define ZSWAP_F_MASK          0x00ff
#define ZSWAP_CHECKSUM_SHIFT  8
#define ZSWAP_CHECKSUM_MASK   0xff00

static void
zswap_zero(void *address, unsigned bytes)
{
    u_char *data;

    data = address;
    while (bytes-- != 0)
        *data++ = 0;
}

unsigned
zswap_logical_bytes(unsigned store_bytes)
{
    unsigned blocks;

    blocks = (store_bytes >> ZSWAP_BLOCK_SHIFT) * ZSWAP_RATIO;
    if (blocks > ZSWAP_MAX_BLOCKS)
        blocks = ZSWAP_MAX_BLOCKS;
    return blocks << ZSWAP_BLOCK_SHIFT;
}

int
zswap_init(struct zswap *zswap, volatile void *store,
    unsigned store_bytes)
{
    unsigned logical_bytes;
    unsigned phys_units;

    if (zswap == 0 || store == 0 ||
        store_bytes < ZSWAP_BLOCK_BYTES)
        return EINVAL;
    logical_bytes = zswap_logical_bytes(store_bytes);
    phys_units = store_bytes / ZSWAP_UNIT_BYTES;
    if (logical_bytes == 0 || phys_units == 0)
        return EINVAL;
    if (phys_units > ZSWAP_MAX_UNITS)
        phys_units = ZSWAP_MAX_UNITS;

    zswap_zero(zswap, sizeof(*zswap));
    zswap->zs_store = store;
    zswap->zs_store_bytes = store_bytes;
    zswap->zs_logical_bytes = logical_bytes;
    zswap->zs_blocks = logical_bytes >> ZSWAP_BLOCK_SHIFT;
    zswap->zs_phys_units = phys_units;
    zswap->zs_initialized = 1;
    return 0;
}

static int
zswap_unit_used(const struct zswap *zswap, unsigned unit)
{
    return (zswap->zs_units[unit >> 3] & (1 << (unit & 7))) != 0;
}

static void
zswap_set_unit(struct zswap *zswap, unsigned unit, int used)
{
    if (used)
        zswap->zs_units[unit >> 3] |= 1 << (unit & 7);
    else
        zswap->zs_units[unit >> 3] &= ~(1 << (unit & 7));
}

static int
zswap_scan_units(struct zswap *zswap, unsigned units,
    unsigned first, unsigned last, unsigned *unitp)
{
    unsigned run;
    unsigned start;
    unsigned unit;
    unsigned i;

    run = 0;
    start = first;
    for (unit = first; unit < last; ++unit) {
        if (zswap_unit_used(zswap, unit)) {
            run = 0;
            continue;
        }
        if (run == 0)
            start = unit;
        if (++run == units) {
            for (i = 0; i < units; ++i)
                zswap_set_unit(zswap, start + i, 1);
            *unitp = start;
            zswap->zs_alloc_hint = start + units;
            if (zswap->zs_alloc_hint >= zswap->zs_phys_units)
                zswap->zs_alloc_hint = 0;
            return 0;
        }
    }
    return ENOSPC;
}

static int
zswap_alloc_units(struct zswap *zswap, unsigned units,
    unsigned *unitp)
{
    unsigned hint;

    if (units == 0 || units > zswap->zs_phys_units || unitp == 0)
        return ENOSPC;
    hint = zswap->zs_alloc_hint;
    if (hint >= zswap->zs_phys_units)
        hint = 0;
    if (zswap_scan_units(zswap, units, hint,
        zswap->zs_phys_units, unitp) == 0)
        return 0;
    if (hint != 0 &&
        zswap_scan_units(zswap, units, 0, hint, unitp) == 0)
        return 0;
    return ENOSPC;
}

static void
zswap_free_units(struct zswap *zswap, unsigned unit,
    unsigned units)
{
    unsigned i;

    for (i = 0; i < units && unit + i < zswap->zs_phys_units; ++i)
        zswap_set_unit(zswap, unit + i, 0);
    if (unit < zswap->zs_alloc_hint)
        zswap->zs_alloc_hint = unit;
}

static void
zswap_free_entry(struct zswap *zswap, unsigned block)
{
    struct zswap_entry *entry;

    if (block >= zswap->zs_blocks)
        return;
    entry = &zswap->zs_entry[block];
    if ((entry->flags & ZSWAP_F_VALID) == 0)
        return;
    if ((entry->flags & ZSWAP_F_ZERO) == 0)
        zswap_free_units(zswap, entry->unit, entry->units);
    entry->unit = 0;
    entry->units = 0;
    entry->length = 0;
    entry->flags = 0;
}

void
zswap_discard(struct zswap *zswap, size_t blkno,
    size_t nblocks)
{
    size_t block;

    if (zswap == 0 || !zswap->zs_initialized)
        return;
    for (block = 0; block < nblocks; ++block) {
        if (blkno + block >= zswap->zs_blocks)
            break;
        zswap_free_entry(zswap, (unsigned)(blkno + block));
    }
}

static int
zswap_is_zero(const u_char *src)
{
    unsigned i;

    for (i = 0; i < ZSWAP_BLOCK_BYTES; ++i)
        if (src[i] != 0)
            return 0;
    return 1;
}

/*
 * Store an inexpensive integrity byte in the otherwise unused high byte of
 * entry flags.  This detects corruption of both raw and compressed backing
 * data without increasing the fixed metadata footprint on small systems.
 */
static unsigned
zswap_checksum(const u_char *src)
{
    unsigned checksum;
    unsigned i;

    checksum = 0xa5;
    for (i = 0; i < ZSWAP_BLOCK_BYTES; ++i) {
        checksum = ((checksum << 5) | (checksum >> 3)) & 0xff;
        checksum ^= src[i];
    }
    return checksum;
}

static int
zswap_emit_literals(const u_char *src, u_char *dst, unsigned *op,
    unsigned start, unsigned len)
{
    unsigned i;

    if (len == 0)
        return 0;
    if (*op + 1 + len > ZSWAP_BLOCK_BYTES)
        return ENOSPC;
    dst[(*op)++] = (u_char)(len - 1);
    for (i = 0; i < len; ++i)
        dst[(*op)++] = src[start + i];
    return 0;
}

static unsigned
zswap_hash3(const u_char *src)
{
    u_int value;

    value = ((u_int)src[0] << 16) ^ ((u_int)src[1] << 8) ^ src[2];
    return ((value * 2777) >> 9) & (ZSWAP_HASH_SIZE - 1);
}

static unsigned
zswap_compress(struct zswap *zswap, const u_char *src,
    u_char *dst)
{
    unsigned ip;
    unsigned op;
    unsigned lit_start;
    unsigned lit_len;
    unsigned i;

    for (i = 0; i < ZSWAP_HASH_SIZE; ++i)
        zswap->zs_hash[i] = 0;

    ip = 0;
    op = 0;
    lit_start = 0;
    lit_len = 0;
    while (ip < ZSWAP_BLOCK_BYTES) {
        if (ip + 2 < ZSWAP_BLOCK_BYTES) {
            unsigned h;
            unsigned refpos;
            unsigned ref;
            unsigned off;

            h = zswap_hash3(src + ip);
            refpos = zswap->zs_hash[h];
            zswap->zs_hash[h] = (u_short)(ip + 1);
            if (refpos != 0) {
                ref = refpos - 1;
                off = ip - ref - 1;
                if (off < 8192 && src[ref] == src[ip] &&
                    src[ref + 1] == src[ip + 1] &&
                    src[ref + 2] == src[ip + 2]) {
                    unsigned max_len;
                    unsigned match_len;
                    unsigned enc_len;

                    max_len = ZSWAP_BLOCK_BYTES - ip;
                    if (max_len > 264)
                        max_len = 264;
                    match_len = 3;
                    while (match_len < max_len &&
                        src[ref + match_len] == src[ip + match_len])
                        ++match_len;
                    if (zswap_emit_literals(src, dst, &op,
                        lit_start, lit_len) != 0)
                        return 0;
                    lit_len = 0;
                    enc_len = match_len - 2;
                    if (enc_len < 7) {
                        if (op + 2 > ZSWAP_BLOCK_BYTES)
                            return 0;
                        dst[op++] = (u_char)((enc_len << 5) |
                            (off >> 8));
                        dst[op++] = (u_char)off;
                    } else {
                        if (op + 3 > ZSWAP_BLOCK_BYTES)
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
            if (zswap_emit_literals(src, dst, &op, lit_start,
                lit_len) != 0)
                return 0;
            lit_len = 0;
            lit_start = ip;
        }
    }
    if (zswap_emit_literals(src, dst, &op, lit_start, lit_len) != 0)
        return 0;
    return op;
}

static int
zswap_decompress(const u_char *src, unsigned srclen, u_char *dst)
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
                op + len > ZSWAP_BLOCK_BYTES)
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
            if (op + len > ZSWAP_BLOCK_BYTES)
                return EIO;
            for (i = 0; i < len; ++i)
                dst[op++] = dst[ref++];
        }
    }
    return op == ZSWAP_BLOCK_BYTES ? 0 : EIO;
}

static void
zswap_copy_to_store(struct zswap *zswap, unsigned unit,
    const u_char *src, unsigned len)
{
    volatile u_char *store;
    unsigned i;

    store = zswap->zs_store + unit * ZSWAP_UNIT_BYTES;
    for (i = 0; i < len; ++i)
        *store++ = src[i];
}

static void
zswap_copy_from_store(struct zswap *zswap, unsigned unit,
    u_char *dst, unsigned len)
{
    volatile u_char *store;
    unsigned i;

    store = zswap->zs_store + unit * ZSWAP_UNIT_BYTES;
    for (i = 0; i < len; ++i)
        dst[i] = *store++;
}

static int
zswap_read_block(struct zswap *zswap, unsigned block,
    u_char *dst)
{
    struct zswap_entry *entry;
    unsigned checksum;
    unsigned flags;
    unsigned i;
    int error;

    if (block >= zswap->zs_blocks)
        return EINVAL;
    entry = &zswap->zs_entry[block];
    if ((entry->flags & ZSWAP_F_VALID) == 0) {
        for (i = 0; i < ZSWAP_BLOCK_BYTES; ++i)
            dst[i] = 0;
        return 0;
    }
    flags = entry->flags & ZSWAP_F_MASK;
    if (flags & ZSWAP_F_ZERO) {
        if (flags != (ZSWAP_F_VALID | ZSWAP_F_ZERO) ||
            entry->unit != 0 || entry->units != 0 ||
            entry->length != 0 ||
            (entry->flags & ZSWAP_CHECKSUM_MASK) != 0) {
            error = ZSWAP_ERROR_METADATA;
            goto read_error;
        }
        for (i = 0; i < ZSWAP_BLOCK_BYTES; ++i)
            dst[i] = 0;
        return 0;
    }
    if ((flags & ~(ZSWAP_F_VALID | ZSWAP_F_ZERO |
        ZSWAP_F_RAW)) != 0 || entry->units == 0 ||
        entry->unit >= zswap->zs_phys_units ||
        entry->units > zswap->zs_phys_units - entry->unit ||
        entry->length == 0 ||
        entry->length > entry->units * ZSWAP_UNIT_BYTES ||
        ((flags & ZSWAP_F_RAW) != 0 &&
        (entry->units != ZSWAP_UNITS_PER_BLOCK ||
        entry->length != ZSWAP_BLOCK_BYTES))) {
        error = ZSWAP_ERROR_METADATA;
        goto read_error;
    }
    if (flags & ZSWAP_F_RAW) {
        zswap_copy_from_store(zswap, entry->unit, dst,
            ZSWAP_BLOCK_BYTES);
    } else {
        zswap_copy_from_store(zswap, entry->unit, zswap->zs_comp,
            entry->length);
        if (zswap_decompress(zswap->zs_comp, entry->length, dst) != 0) {
            error = ZSWAP_ERROR_DECOMPRESS;
            goto read_error;
        }
    }
    checksum = zswap_checksum(dst);
    if (checksum !=
        ((entry->flags & ZSWAP_CHECKSUM_MASK) >>
        ZSWAP_CHECKSUM_SHIFT)) {
        error = ZSWAP_ERROR_CHECKSUM;
        goto read_error;
    }
    return 0;

read_error:
    ++zswap->zs_read_errors;
    zswap->zs_last_error = error;
    zswap->zs_last_error_block = block;
    zswap->zs_last_error_unit = entry->unit;
    zswap->zs_last_error_units = entry->units;
    zswap->zs_last_error_length = entry->length;
    zswap->zs_last_error_flags = entry->flags;
    return EIO;
}

static int
zswap_write_block(struct zswap *zswap, unsigned block,
    const u_char *src)
{
    struct zswap_entry *entry;
    const u_char *store_src;
    unsigned old_units;
    unsigned old_unit;
    unsigned len;
    unsigned units;
    unsigned unit;
    unsigned flags;
    unsigned clen;
    int error;

    if (block >= zswap->zs_blocks)
        return EINVAL;
    entry = &zswap->zs_entry[block];
    if (zswap_is_zero(src)) {
        zswap_free_entry(zswap, block);
        entry->flags = ZSWAP_F_VALID | ZSWAP_F_ZERO;
        return 0;
    }

    clen = zswap_compress(zswap, src, zswap->zs_comp);
    if (clen != 0 &&
        ZSWAP_HOWMANY(clen, ZSWAP_UNIT_BYTES) <
        ZSWAP_UNITS_PER_BLOCK) {
        store_src = zswap->zs_comp;
        len = clen;
        units = ZSWAP_HOWMANY(clen, ZSWAP_UNIT_BYTES);
        flags = ZSWAP_F_VALID;
    } else {
        store_src = src;
        len = ZSWAP_BLOCK_BYTES;
        units = ZSWAP_UNITS_PER_BLOCK;
        flags = ZSWAP_F_VALID | ZSWAP_F_RAW;
    }

    if ((entry->flags & ZSWAP_F_VALID) != 0 &&
        (entry->flags & ZSWAP_F_ZERO) == 0 &&
        entry->units >= units) {
        unit = entry->unit;
        old_units = entry->units;
        zswap_copy_to_store(zswap, unit, store_src, len);
        if (old_units > units)
            zswap_free_units(zswap, unit + units,
                old_units - units);
    } else {
        error = zswap_alloc_units(zswap, units, &unit);
        if (error != 0)
            return error;
        zswap_copy_to_store(zswap, unit, store_src, len);
        old_unit = entry->unit;
        old_units = entry->units;
        if ((entry->flags & ZSWAP_F_VALID) != 0 &&
            (entry->flags & ZSWAP_F_ZERO) == 0)
            zswap_free_units(zswap, old_unit, old_units);
    }

    entry->unit = (u_short)unit;
    entry->units = (u_short)units;
    entry->length = (u_short)len;
    entry->flags = (u_short)(flags |
        (zswap_checksum(src) << ZSWAP_CHECKSUM_SHIFT));
    return 0;
}

static int
zswap_range_valid(const struct zswap *zswap, unsigned offset,
    unsigned bytes)
{
    return zswap != 0 && zswap->zs_initialized &&
        offset <= zswap->zs_logical_bytes &&
        bytes <= zswap->zs_logical_bytes - offset;
}

int
zswap_read(struct zswap *zswap, unsigned offset, void *buffer,
    unsigned bytes)
{
    u_char *data;
    unsigned done;
    int error;

    if (buffer == 0 || !zswap_range_valid(zswap, offset, bytes))
        return EINVAL;
    data = buffer;
    done = 0;
    while (done < bytes) {
        unsigned block;
        unsigned boff;
        unsigned chunk;
        unsigned i;

        block = offset >> ZSWAP_BLOCK_SHIFT;
        boff = offset & ZSWAP_BLOCK_MASK;
        chunk = ZSWAP_BLOCK_BYTES - boff;
        if (chunk > bytes - done)
            chunk = bytes - done;
        if (boff == 0 && chunk == ZSWAP_BLOCK_BYTES) {
            error = zswap_read_block(zswap, block, data);
        } else {
            error = zswap_read_block(zswap, block, zswap->zs_block);
            if (error == 0)
                for (i = 0; i < chunk; ++i)
                    data[i] = zswap->zs_block[boff + i];
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
zswap_write(struct zswap *zswap, unsigned offset,
    const void *buffer, unsigned bytes)
{
    const u_char *data;
    unsigned done;
    int error;

    if (buffer == 0 || !zswap_range_valid(zswap, offset, bytes))
        return EINVAL;
    data = buffer;
    done = 0;
    while (done < bytes) {
        unsigned block;
        unsigned boff;
        unsigned chunk;
        unsigned i;

        block = offset >> ZSWAP_BLOCK_SHIFT;
        boff = offset & ZSWAP_BLOCK_MASK;
        chunk = ZSWAP_BLOCK_BYTES - boff;
        if (chunk > bytes - done)
            chunk = bytes - done;
        if (boff == 0 && chunk == ZSWAP_BLOCK_BYTES) {
            error = zswap_write_block(zswap, block, data);
        } else {
            error = zswap_read_block(zswap, block, zswap->zs_block);
            if (error == 0) {
                for (i = 0; i < chunk; ++i)
                    zswap->zs_block[boff + i] = data[i];
                error = zswap_write_block(zswap, block,
                    zswap->zs_block);
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

int
zswap_get_stats(const struct zswap *zswap,
    struct zswap_stats *stats)
{
    const struct zswap_entry *entry;
    unsigned block;

    if (zswap == 0 || stats == 0 || !zswap->zs_initialized)
        return EINVAL;
    zswap_zero(stats, sizeof(*stats));
    stats->zss_logical_blocks = zswap->zs_blocks;
    stats->zss_phys_units = zswap->zs_phys_units;
    stats->zss_read_errors = zswap->zs_read_errors;
    stats->zss_last_error = zswap->zs_last_error;
    stats->zss_last_error_block = zswap->zs_last_error_block;
    stats->zss_last_error_unit = zswap->zs_last_error_unit;
    stats->zss_last_error_units = zswap->zs_last_error_units;
    stats->zss_last_error_length = zswap->zs_last_error_length;
    stats->zss_last_error_flags = zswap->zs_last_error_flags;
    for (block = 0; block < zswap->zs_blocks; ++block) {
        entry = &zswap->zs_entry[block];
        if ((entry->flags & ZSWAP_F_VALID) == 0)
            continue;
        ++stats->zss_valid_blocks;
        if (entry->flags & ZSWAP_F_ZERO) {
            ++stats->zss_zero_blocks;
            continue;
        }
        stats->zss_used_units += entry->units;
        if (entry->flags & ZSWAP_F_RAW)
            ++stats->zss_raw_blocks;
        else
            ++stats->zss_compressed_blocks;
    }
    return 0;
}
