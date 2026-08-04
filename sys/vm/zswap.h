#ifndef _VM_ZSWAP_H_
#define _VM_ZSWAP_H_

#include <sys/types.h>

#define ZSWAP_BLOCK_BYTES       1024
#define ZSWAP_BLOCK_SHIFT       10
#define ZSWAP_BLOCK_MASK        (ZSWAP_BLOCK_BYTES - 1)
#define ZSWAP_RATIO             2
#define ZSWAP_UNIT_BYTES        256
#define ZSWAP_MAX_BLOCKS        8192
#define ZSWAP_MAX_UNITS         16384
#define ZSWAP_HASH_SIZE         1024

#define ZSWAP_ERROR_NONE        0
#define ZSWAP_ERROR_METADATA    1
#define ZSWAP_ERROR_DECOMPRESS  2
#define ZSWAP_ERROR_CHECKSUM    3

struct zswap_entry {
    u_short unit;
    u_short units;
    u_short length;
    u_short flags;
};

struct zswap_stats {
    unsigned zss_logical_blocks;
    unsigned zss_phys_units;
    unsigned zss_valid_blocks;
    unsigned zss_zero_blocks;
    unsigned zss_raw_blocks;
    unsigned zss_compressed_blocks;
    unsigned zss_used_units;
    unsigned zss_read_errors;
    unsigned zss_last_error;
    unsigned zss_last_error_block;
    unsigned zss_last_error_unit;
    unsigned zss_last_error_units;
    unsigned zss_last_error_length;
    unsigned zss_last_error_flags;
};

struct zswap {
    struct zswap_entry zs_entry[ZSWAP_MAX_BLOCKS];
    u_char zs_units[(ZSWAP_MAX_UNITS + 7) / 8];
    u_char zs_block[ZSWAP_BLOCK_BYTES];
    u_char zs_comp[ZSWAP_BLOCK_BYTES];
    u_short zs_hash[ZSWAP_HASH_SIZE];
    volatile u_char *zs_store;
    unsigned zs_store_bytes;
    unsigned zs_logical_bytes;
    unsigned zs_blocks;
    unsigned zs_phys_units;
    unsigned zs_alloc_hint;
    unsigned zs_read_errors;
    unsigned zs_last_error;
    unsigned zs_last_error_block;
    unsigned zs_last_error_unit;
    unsigned zs_last_error_units;
    unsigned zs_last_error_length;
    unsigned zs_last_error_flags;
    int zs_initialized;
};

unsigned zswap_logical_bytes(unsigned store_bytes);
int zswap_init(struct zswap *zswap, volatile void *store,
    unsigned store_bytes);
void zswap_discard(struct zswap *zswap, size_t blkno,
    size_t nblocks);
int zswap_read(struct zswap *zswap, unsigned offset, void *data,
    unsigned bytes);
int zswap_write(struct zswap *zswap, unsigned offset,
    const void *data, unsigned bytes);
int zswap_get_stats(const struct zswap *zswap,
    struct zswap_stats *stats);

#endif /* _VM_ZSWAP_H_ */
