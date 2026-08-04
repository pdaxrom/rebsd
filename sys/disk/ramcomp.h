#ifndef _DISK_RAMCOMP_H_
#define _DISK_RAMCOMP_H_

#include <sys/types.h>

#define RAMCOMP_BLOCK_BYTES       1024
#define RAMCOMP_BLOCK_SHIFT       10
#define RAMCOMP_BLOCK_MASK        (RAMCOMP_BLOCK_BYTES - 1)
#define RAMCOMP_UNIT_BYTES        256
#define RAMCOMP_HASH_SIZE         1024

#define RAMCOMP_ERROR_NONE        0
#define RAMCOMP_ERROR_METADATA    1
#define RAMCOMP_ERROR_DECOMPRESS  2
#define RAMCOMP_ERROR_CHECKSUM    3

struct ramcomp_entry {
    unsigned unit;
    u_short units;
    u_short length;
    u_short flags;
};

struct ramcomp_stats {
    unsigned rcs_logical_blocks;
    unsigned rcs_phys_units;
    unsigned rcs_valid_blocks;
    unsigned rcs_zero_blocks;
    unsigned rcs_raw_blocks;
    unsigned rcs_compressed_blocks;
    unsigned rcs_used_units;
    unsigned rcs_read_errors;
    unsigned rcs_last_error;
    unsigned rcs_last_error_block;
    unsigned rcs_last_error_unit;
    unsigned rcs_last_error_units;
    unsigned rcs_last_error_length;
    unsigned rcs_last_error_flags;
};

struct ramcomp {
    struct ramcomp_entry *rc_entry;
    u_char *rc_units;
    u_char rc_block[RAMCOMP_BLOCK_BYTES];
    u_char rc_comp[RAMCOMP_BLOCK_BYTES];
    u_short rc_hash[RAMCOMP_HASH_SIZE];
    void *rc_metadata;
    unsigned rc_metadata_bytes;
    volatile u_char *rc_store;
    unsigned rc_store_bytes;
    unsigned rc_logical_bytes;
    unsigned rc_blocks;
    unsigned rc_phys_units;
    unsigned rc_alloc_hint;
    unsigned rc_read_errors;
    unsigned rc_last_error;
    unsigned rc_last_error_block;
    unsigned rc_last_error_unit;
    unsigned rc_last_error_units;
    unsigned rc_last_error_length;
    unsigned rc_last_error_flags;
    int rc_initialized;
};

int ramcomp_metadata_bytes(unsigned, unsigned, unsigned *);
int ramcomp_init(struct ramcomp *, volatile void *, unsigned, unsigned,
    void *, unsigned);
void ramcomp_discard(struct ramcomp *ramcomp, size_t blkno,
    size_t nblocks);
int ramcomp_read(struct ramcomp *ramcomp, unsigned offset, void *data,
    unsigned bytes);
int ramcomp_write(struct ramcomp *ramcomp, unsigned offset,
    const void *data, unsigned bytes);
int ramcomp_get_stats(const struct ramcomp *ramcomp,
    struct ramcomp_stats *stats);

#endif /* _DISK_RAMCOMP_H_ */
