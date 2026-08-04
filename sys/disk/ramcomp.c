/* Shared compressed backing store for RAM block devices. */

#include <sys/errno.h>
#include <disk/ramcomp.h>

#define RAMCOMP_UNITS_PER_BLOCK \
    (RAMCOMP_BLOCK_BYTES / RAMCOMP_UNIT_BYTES)
#define RAMCOMP_HOWMANY(value, unit) (((value) + (unit) - 1) / (unit))

#define RAMCOMP_F_VALID         0x0001
#define RAMCOMP_F_ZERO          0x0002
#define RAMCOMP_F_RAW           0x0004
#define RAMCOMP_F_MASK          0x00ff
#define RAMCOMP_CHECKSUM_SHIFT  8
#define RAMCOMP_CHECKSUM_MASK   0xff00

static void
ramcomp_zero(void *address, unsigned bytes)
{
    u_char *data;

    data = address;
    while (bytes-- != 0)
        *data++ = 0;
}

int
ramcomp_metadata_bytes(unsigned store_bytes, unsigned logical_bytes,
    unsigned *bytesp)
{
    unsigned blocks;
    unsigned phys_units;
    unsigned entry_bytes;
    unsigned bitmap_bytes;

    if (bytesp == 0 || store_bytes < RAMCOMP_BLOCK_BYTES ||
        logical_bytes == 0 ||
        (logical_bytes & RAMCOMP_BLOCK_MASK) != 0)
        return EINVAL;
    blocks = logical_bytes >> RAMCOMP_BLOCK_SHIFT;
    phys_units = store_bytes / RAMCOMP_UNIT_BYTES;
    if (blocks > ((unsigned)-1) / sizeof(struct ramcomp_entry) ||
        phys_units > (unsigned)-1 - 7)
        return EOVERFLOW;
    entry_bytes = blocks * sizeof(struct ramcomp_entry);
    bitmap_bytes = (phys_units + 7) >> 3;
    if (entry_bytes > (unsigned)-1 - bitmap_bytes)
        return EOVERFLOW;
    *bytesp = entry_bytes + bitmap_bytes;
    return 0;
}

int
ramcomp_init(struct ramcomp *ramcomp, volatile void *store,
    unsigned store_bytes, unsigned logical_bytes, void *metadata,
    unsigned metadata_bytes)
{
    unsigned needed;
    unsigned blocks;
    unsigned phys_units;

    if (ramcomp == 0 || store == 0 || metadata == 0)
        return EINVAL;
    if (ramcomp_metadata_bytes(store_bytes, logical_bytes, &needed) != 0)
        return EINVAL;
    if (metadata_bytes < needed)
        return ENOSPC;
    blocks = logical_bytes >> RAMCOMP_BLOCK_SHIFT;
    phys_units = store_bytes / RAMCOMP_UNIT_BYTES;
    if (blocks == 0 || phys_units == 0)
        return EINVAL;

    ramcomp_zero(ramcomp, sizeof(*ramcomp));
    ramcomp_zero(metadata, needed);
    ramcomp->rc_entry = metadata;
    ramcomp->rc_units = (u_char *)metadata +
        blocks * sizeof(struct ramcomp_entry);
    ramcomp->rc_metadata = metadata;
    ramcomp->rc_metadata_bytes = needed;
    ramcomp->rc_store = store;
    ramcomp->rc_store_bytes = store_bytes;
    ramcomp->rc_logical_bytes = logical_bytes;
    ramcomp->rc_blocks = blocks;
    ramcomp->rc_phys_units = phys_units;
    ramcomp->rc_initialized = 1;
    return 0;
}

static int
ramcomp_unit_used(const struct ramcomp *ramcomp, unsigned unit)
{
    return (ramcomp->rc_units[unit >> 3] & (1 << (unit & 7))) != 0;
}

static void
ramcomp_set_unit(struct ramcomp *ramcomp, unsigned unit, int used)
{
    if (used)
        ramcomp->rc_units[unit >> 3] |= 1 << (unit & 7);
    else
        ramcomp->rc_units[unit >> 3] &= ~(1 << (unit & 7));
}

static int
ramcomp_scan_units(struct ramcomp *ramcomp, unsigned units,
    unsigned first, unsigned last, unsigned *unitp)
{
    unsigned run;
    unsigned start;
    unsigned unit;
    unsigned i;

    run = 0;
    start = first;
    for (unit = first; unit < last; ++unit) {
        if (ramcomp_unit_used(ramcomp, unit)) {
            run = 0;
            continue;
        }
        if (run == 0)
            start = unit;
        if (++run == units) {
            for (i = 0; i < units; ++i)
                ramcomp_set_unit(ramcomp, start + i, 1);
            *unitp = start;
            ramcomp->rc_alloc_hint = start + units;
            if (ramcomp->rc_alloc_hint >= ramcomp->rc_phys_units)
                ramcomp->rc_alloc_hint = 0;
            return 0;
        }
    }
    return ENOSPC;
}

static int
ramcomp_alloc_units(struct ramcomp *ramcomp, unsigned units,
    unsigned *unitp)
{
    unsigned hint;

    if (units == 0 || units > ramcomp->rc_phys_units || unitp == 0)
        return ENOSPC;
    hint = ramcomp->rc_alloc_hint;
    if (hint >= ramcomp->rc_phys_units)
        hint = 0;
    if (ramcomp_scan_units(ramcomp, units, hint,
        ramcomp->rc_phys_units, unitp) == 0)
        return 0;
    if (hint != 0 &&
        ramcomp_scan_units(ramcomp, units, 0, hint, unitp) == 0)
        return 0;
    return ENOSPC;
}

static void
ramcomp_free_units(struct ramcomp *ramcomp, unsigned unit,
    unsigned units)
{
    unsigned i;

    for (i = 0; i < units && unit + i < ramcomp->rc_phys_units; ++i)
        ramcomp_set_unit(ramcomp, unit + i, 0);
    if (unit < ramcomp->rc_alloc_hint)
        ramcomp->rc_alloc_hint = unit;
}

static void
ramcomp_free_entry(struct ramcomp *ramcomp, unsigned block)
{
    struct ramcomp_entry *entry;

    if (block >= ramcomp->rc_blocks)
        return;
    entry = &ramcomp->rc_entry[block];
    if ((entry->flags & RAMCOMP_F_VALID) == 0)
        return;
    if ((entry->flags & RAMCOMP_F_ZERO) == 0)
        ramcomp_free_units(ramcomp, entry->unit, entry->units);
    entry->unit = 0;
    entry->units = 0;
    entry->length = 0;
    entry->flags = 0;
}

void
ramcomp_discard(struct ramcomp *ramcomp, size_t blkno,
    size_t nblocks)
{
    size_t block;

    if (ramcomp == 0 || !ramcomp->rc_initialized)
        return;
    for (block = 0; block < nblocks; ++block) {
        if (blkno + block >= ramcomp->rc_blocks)
            break;
        ramcomp_free_entry(ramcomp, (unsigned)(blkno + block));
    }
}

static int
ramcomp_is_zero(const u_char *src)
{
    unsigned i;

    for (i = 0; i < RAMCOMP_BLOCK_BYTES; ++i)
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
ramcomp_checksum(const u_char *src)
{
    unsigned checksum;
    unsigned i;

    checksum = 0xa5;
    for (i = 0; i < RAMCOMP_BLOCK_BYTES; ++i) {
        checksum = ((checksum << 5) | (checksum >> 3)) & 0xff;
        checksum ^= src[i];
    }
    return checksum;
}

static int
ramcomp_emit_literals(const u_char *src, u_char *dst, unsigned *op,
    unsigned start, unsigned len)
{
    unsigned i;

    if (len == 0)
        return 0;
    if (*op + 1 + len > RAMCOMP_BLOCK_BYTES)
        return ENOSPC;
    dst[(*op)++] = (u_char)(len - 1);
    for (i = 0; i < len; ++i)
        dst[(*op)++] = src[start + i];
    return 0;
}

static unsigned
ramcomp_hash3(const u_char *src)
{
    u_int value;

    value = ((u_int)src[0] << 16) ^ ((u_int)src[1] << 8) ^ src[2];
    return ((value * 2777) >> 9) & (RAMCOMP_HASH_SIZE - 1);
}

static unsigned
ramcomp_compress(struct ramcomp *ramcomp, const u_char *src,
    u_char *dst)
{
    unsigned ip;
    unsigned op;
    unsigned lit_start;
    unsigned lit_len;
    unsigned i;

    for (i = 0; i < RAMCOMP_HASH_SIZE; ++i)
        ramcomp->rc_hash[i] = 0;

    ip = 0;
    op = 0;
    lit_start = 0;
    lit_len = 0;
    while (ip < RAMCOMP_BLOCK_BYTES) {
        if (ip + 2 < RAMCOMP_BLOCK_BYTES) {
            unsigned h;
            unsigned refpos;
            unsigned ref;
            unsigned off;

            h = ramcomp_hash3(src + ip);
            refpos = ramcomp->rc_hash[h];
            ramcomp->rc_hash[h] = (u_short)(ip + 1);
            if (refpos != 0) {
                ref = refpos - 1;
                off = ip - ref - 1;
                if (off < 8192 && src[ref] == src[ip] &&
                    src[ref + 1] == src[ip + 1] &&
                    src[ref + 2] == src[ip + 2]) {
                    unsigned max_len;
                    unsigned match_len;
                    unsigned enc_len;

                    max_len = RAMCOMP_BLOCK_BYTES - ip;
                    if (max_len > 264)
                        max_len = 264;
                    match_len = 3;
                    while (match_len < max_len &&
                        src[ref + match_len] == src[ip + match_len])
                        ++match_len;
                    if (ramcomp_emit_literals(src, dst, &op,
                        lit_start, lit_len) != 0)
                        return 0;
                    lit_len = 0;
                    enc_len = match_len - 2;
                    if (enc_len < 7) {
                        if (op + 2 > RAMCOMP_BLOCK_BYTES)
                            return 0;
                        dst[op++] = (u_char)((enc_len << 5) |
                            (off >> 8));
                        dst[op++] = (u_char)off;
                    } else {
                        if (op + 3 > RAMCOMP_BLOCK_BYTES)
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
            if (ramcomp_emit_literals(src, dst, &op, lit_start,
                lit_len) != 0)
                return 0;
            lit_len = 0;
            lit_start = ip;
        }
    }
    if (ramcomp_emit_literals(src, dst, &op, lit_start, lit_len) != 0)
        return 0;
    return op;
}

static int
ramcomp_decompress(const u_char *src, unsigned srclen, u_char *dst)
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
                op + len > RAMCOMP_BLOCK_BYTES)
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
            if (op + len > RAMCOMP_BLOCK_BYTES)
                return EIO;
            for (i = 0; i < len; ++i)
                dst[op++] = dst[ref++];
        }
    }
    return op == RAMCOMP_BLOCK_BYTES ? 0 : EIO;
}

static void
ramcomp_copy_to_store(struct ramcomp *ramcomp, unsigned unit,
    const u_char *src, unsigned len)
{
    volatile u_char *store;
    unsigned i;

    store = ramcomp->rc_store + unit * RAMCOMP_UNIT_BYTES;
    for (i = 0; i < len; ++i)
        *store++ = src[i];
}

static void
ramcomp_copy_from_store(struct ramcomp *ramcomp, unsigned unit,
    u_char *dst, unsigned len)
{
    volatile u_char *store;
    unsigned i;

    store = ramcomp->rc_store + unit * RAMCOMP_UNIT_BYTES;
    for (i = 0; i < len; ++i)
        dst[i] = *store++;
}

static int
ramcomp_read_block(struct ramcomp *ramcomp, unsigned block,
    u_char *dst)
{
    struct ramcomp_entry *entry;
    unsigned checksum;
    unsigned flags;
    unsigned i;
    int error;

    if (block >= ramcomp->rc_blocks)
        return EINVAL;
    entry = &ramcomp->rc_entry[block];
    if ((entry->flags & RAMCOMP_F_VALID) == 0) {
        for (i = 0; i < RAMCOMP_BLOCK_BYTES; ++i)
            dst[i] = 0;
        return 0;
    }
    flags = entry->flags & RAMCOMP_F_MASK;
    if (flags & RAMCOMP_F_ZERO) {
        if (flags != (RAMCOMP_F_VALID | RAMCOMP_F_ZERO) ||
            entry->unit != 0 || entry->units != 0 ||
            entry->length != 0 ||
            (entry->flags & RAMCOMP_CHECKSUM_MASK) != 0) {
            error = RAMCOMP_ERROR_METADATA;
            goto read_error;
        }
        for (i = 0; i < RAMCOMP_BLOCK_BYTES; ++i)
            dst[i] = 0;
        return 0;
    }
    if ((flags & ~(RAMCOMP_F_VALID | RAMCOMP_F_ZERO |
        RAMCOMP_F_RAW)) != 0 || entry->units == 0 ||
        entry->unit >= ramcomp->rc_phys_units ||
        entry->units > ramcomp->rc_phys_units - entry->unit ||
        entry->length == 0 ||
        entry->length > entry->units * RAMCOMP_UNIT_BYTES ||
        ((flags & RAMCOMP_F_RAW) != 0 &&
        (entry->units != RAMCOMP_UNITS_PER_BLOCK ||
        entry->length != RAMCOMP_BLOCK_BYTES))) {
        error = RAMCOMP_ERROR_METADATA;
        goto read_error;
    }
    if (flags & RAMCOMP_F_RAW) {
        ramcomp_copy_from_store(ramcomp, entry->unit, dst,
            RAMCOMP_BLOCK_BYTES);
    } else {
        ramcomp_copy_from_store(ramcomp, entry->unit, ramcomp->rc_comp,
            entry->length);
        if (ramcomp_decompress(ramcomp->rc_comp, entry->length, dst) != 0) {
            error = RAMCOMP_ERROR_DECOMPRESS;
            goto read_error;
        }
    }
    checksum = ramcomp_checksum(dst);
    if (checksum !=
        ((entry->flags & RAMCOMP_CHECKSUM_MASK) >>
        RAMCOMP_CHECKSUM_SHIFT)) {
        error = RAMCOMP_ERROR_CHECKSUM;
        goto read_error;
    }
    return 0;

read_error:
    ++ramcomp->rc_read_errors;
    ramcomp->rc_last_error = error;
    ramcomp->rc_last_error_block = block;
    ramcomp->rc_last_error_unit = entry->unit;
    ramcomp->rc_last_error_units = entry->units;
    ramcomp->rc_last_error_length = entry->length;
    ramcomp->rc_last_error_flags = entry->flags;
    return EIO;
}

static int
ramcomp_write_block(struct ramcomp *ramcomp, unsigned block,
    const u_char *src)
{
    struct ramcomp_entry *entry;
    const u_char *store_src;
    unsigned old_units;
    unsigned old_unit;
    unsigned len;
    unsigned units;
    unsigned unit;
    unsigned flags;
    unsigned clen;
    int error;

    if (block >= ramcomp->rc_blocks)
        return EINVAL;
    entry = &ramcomp->rc_entry[block];
    if (ramcomp_is_zero(src)) {
        ramcomp_free_entry(ramcomp, block);
        entry->flags = RAMCOMP_F_VALID | RAMCOMP_F_ZERO;
        return 0;
    }

    clen = ramcomp_compress(ramcomp, src, ramcomp->rc_comp);
    if (clen != 0 &&
        RAMCOMP_HOWMANY(clen, RAMCOMP_UNIT_BYTES) <
        RAMCOMP_UNITS_PER_BLOCK) {
        store_src = ramcomp->rc_comp;
        len = clen;
        units = RAMCOMP_HOWMANY(clen, RAMCOMP_UNIT_BYTES);
        flags = RAMCOMP_F_VALID;
    } else {
        store_src = src;
        len = RAMCOMP_BLOCK_BYTES;
        units = RAMCOMP_UNITS_PER_BLOCK;
        flags = RAMCOMP_F_VALID | RAMCOMP_F_RAW;
    }

    if ((entry->flags & RAMCOMP_F_VALID) != 0 &&
        (entry->flags & RAMCOMP_F_ZERO) == 0 &&
        entry->units >= units) {
        unit = entry->unit;
        old_units = entry->units;
        ramcomp_copy_to_store(ramcomp, unit, store_src, len);
        if (old_units > units)
            ramcomp_free_units(ramcomp, unit + units,
                old_units - units);
    } else {
        error = ramcomp_alloc_units(ramcomp, units, &unit);
        if (error != 0)
            return error;
        ramcomp_copy_to_store(ramcomp, unit, store_src, len);
        old_unit = entry->unit;
        old_units = entry->units;
        if ((entry->flags & RAMCOMP_F_VALID) != 0 &&
            (entry->flags & RAMCOMP_F_ZERO) == 0)
            ramcomp_free_units(ramcomp, old_unit, old_units);
    }

    entry->unit = unit;
    entry->units = (u_short)units;
    entry->length = (u_short)len;
    entry->flags = (u_short)(flags |
        (ramcomp_checksum(src) << RAMCOMP_CHECKSUM_SHIFT));
    return 0;
}

static int
ramcomp_range_valid(const struct ramcomp *ramcomp, unsigned offset,
    unsigned bytes)
{
    return ramcomp != 0 && ramcomp->rc_initialized &&
        offset <= ramcomp->rc_logical_bytes &&
        bytes <= ramcomp->rc_logical_bytes - offset;
}

int
ramcomp_read(struct ramcomp *ramcomp, unsigned offset, void *buffer,
    unsigned bytes)
{
    u_char *data;
    unsigned done;
    int error;

    if (buffer == 0 || !ramcomp_range_valid(ramcomp, offset, bytes))
        return EINVAL;
    data = buffer;
    done = 0;
    while (done < bytes) {
        unsigned block;
        unsigned boff;
        unsigned chunk;
        unsigned i;

        block = offset >> RAMCOMP_BLOCK_SHIFT;
        boff = offset & RAMCOMP_BLOCK_MASK;
        chunk = RAMCOMP_BLOCK_BYTES - boff;
        if (chunk > bytes - done)
            chunk = bytes - done;
        if (boff == 0 && chunk == RAMCOMP_BLOCK_BYTES) {
            error = ramcomp_read_block(ramcomp, block, data);
        } else {
            error = ramcomp_read_block(ramcomp, block, ramcomp->rc_block);
            if (error == 0)
                for (i = 0; i < chunk; ++i)
                    data[i] = ramcomp->rc_block[boff + i];
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
ramcomp_write(struct ramcomp *ramcomp, unsigned offset,
    const void *buffer, unsigned bytes)
{
    const u_char *data;
    unsigned done;
    int error;

    if (buffer == 0 || !ramcomp_range_valid(ramcomp, offset, bytes))
        return EINVAL;
    data = buffer;
    done = 0;
    while (done < bytes) {
        unsigned block;
        unsigned boff;
        unsigned chunk;
        unsigned i;

        block = offset >> RAMCOMP_BLOCK_SHIFT;
        boff = offset & RAMCOMP_BLOCK_MASK;
        chunk = RAMCOMP_BLOCK_BYTES - boff;
        if (chunk > bytes - done)
            chunk = bytes - done;
        if (boff == 0 && chunk == RAMCOMP_BLOCK_BYTES) {
            error = ramcomp_write_block(ramcomp, block, data);
        } else {
            error = ramcomp_read_block(ramcomp, block, ramcomp->rc_block);
            if (error == 0) {
                for (i = 0; i < chunk; ++i)
                    ramcomp->rc_block[boff + i] = data[i];
                error = ramcomp_write_block(ramcomp, block,
                    ramcomp->rc_block);
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
ramcomp_get_stats(const struct ramcomp *ramcomp,
    struct ramcomp_stats *stats)
{
    const struct ramcomp_entry *entry;
    unsigned block;

    if (ramcomp == 0 || stats == 0 || !ramcomp->rc_initialized)
        return EINVAL;
    ramcomp_zero(stats, sizeof(*stats));
    stats->rcs_logical_blocks = ramcomp->rc_blocks;
    stats->rcs_phys_units = ramcomp->rc_phys_units;
    stats->rcs_read_errors = ramcomp->rc_read_errors;
    stats->rcs_last_error = ramcomp->rc_last_error;
    stats->rcs_last_error_block = ramcomp->rc_last_error_block;
    stats->rcs_last_error_unit = ramcomp->rc_last_error_unit;
    stats->rcs_last_error_units = ramcomp->rc_last_error_units;
    stats->rcs_last_error_length = ramcomp->rc_last_error_length;
    stats->rcs_last_error_flags = ramcomp->rc_last_error_flags;
    for (block = 0; block < ramcomp->rc_blocks; ++block) {
        entry = &ramcomp->rc_entry[block];
        if ((entry->flags & RAMCOMP_F_VALID) == 0)
            continue;
        ++stats->rcs_valid_blocks;
        if (entry->flags & RAMCOMP_F_ZERO) {
            ++stats->rcs_zero_blocks;
            continue;
        }
        stats->rcs_used_units += entry->units;
        if (entry->flags & RAMCOMP_F_RAW)
            ++stats->rcs_raw_blocks;
        else
            ++stats->rcs_compressed_blocks;
    }
    return 0;
}
