#ifndef _MIPS_COMMON_ZSWAP_H_
#define _MIPS_COMMON_ZSWAP_H_

#include <sys/types.h>

#define MIPS_ZSWAP_BLOCK_BYTES       1024
#define MIPS_ZSWAP_BLOCK_SHIFT       10
#define MIPS_ZSWAP_BLOCK_MASK        (MIPS_ZSWAP_BLOCK_BYTES - 1)
#define MIPS_ZSWAP_RATIO             2
#define MIPS_ZSWAP_UNIT_BYTES        256
#define MIPS_ZSWAP_MAX_BLOCKS        8192
#define MIPS_ZSWAP_MAX_UNITS         16384
#define MIPS_ZSWAP_HASH_SIZE         4096

struct mips_zswap_entry {
    u_short unit;
    u_short units;
    u_short length;
    u_short flags;
};

struct mips_zswap_stats {
    unsigned mzs_logical_blocks;
    unsigned mzs_phys_units;
    unsigned mzs_valid_blocks;
    unsigned mzs_zero_blocks;
    unsigned mzs_raw_blocks;
    unsigned mzs_compressed_blocks;
    unsigned mzs_used_units;
};

struct mips_zswap {
    struct mips_zswap_entry mz_entry[MIPS_ZSWAP_MAX_BLOCKS];
    u_char mz_units[(MIPS_ZSWAP_MAX_UNITS + 7) / 8];
    u_char mz_block[MIPS_ZSWAP_BLOCK_BYTES];
    u_char mz_comp[MIPS_ZSWAP_BLOCK_BYTES];
    u_short mz_hash[MIPS_ZSWAP_HASH_SIZE];
    volatile u_char *mz_store;
    unsigned mz_store_bytes;
    unsigned mz_logical_bytes;
    unsigned mz_blocks;
    unsigned mz_phys_units;
    int mz_initialized;
};

unsigned mips_zswap_logical_bytes(unsigned store_bytes);
int mips_zswap_init(struct mips_zswap *zswap, volatile void *store,
    unsigned store_bytes);
void mips_zswap_discard(struct mips_zswap *zswap, size_t blkno,
    size_t nblocks);
int mips_zswap_read(struct mips_zswap *zswap, unsigned offset, void *data,
    unsigned bytes);
int mips_zswap_write(struct mips_zswap *zswap, unsigned offset,
    const void *data, unsigned bytes);
int mips_zswap_get_stats(const struct mips_zswap *zswap,
    struct mips_zswap_stats *stats);

#endif /* _MIPS_COMMON_ZSWAP_H_ */
