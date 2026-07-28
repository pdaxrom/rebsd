#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <mips/common/zswap.h>

static struct mips_zswap zswap;
static unsigned char store[MIPS_ZSWAP_BLOCK_BYTES];
static unsigned char first[MIPS_ZSWAP_BLOCK_BYTES];
static unsigned char second[MIPS_ZSWAP_BLOCK_BYTES];
static unsigned char result[MIPS_ZSWAP_BLOCK_BYTES];
static unsigned char span[300];
static unsigned char span_result[300];

static void
fill_noise(unsigned char *data, unsigned seed)
{
    unsigned value;
    unsigned i;

    value = seed;
    for (i = 0; i < MIPS_ZSWAP_BLOCK_BYTES; ++i) {
        value ^= value << 13;
        value ^= value >> 17;
        value ^= value << 5;
        data[i] = (unsigned char)value;
    }
}

int
main(void)
{
    struct mips_zswap_stats stats;
    unsigned corrupt_unit;
    unsigned i;

    if (mips_zswap_logical_bytes(sizeof(store)) !=
        2 * MIPS_ZSWAP_BLOCK_BYTES ||
        mips_zswap_init(&zswap, store, sizeof(store)) != 0)
        return 1;

    memset(result, 0xa5, sizeof(result));
    if (mips_zswap_read(&zswap, 0, result, sizeof(result)) != 0)
        return 1;
    for (i = 0; i < sizeof(result); ++i)
        if (result[i] != 0)
            return 1;

    memset(first, 0, sizeof(first));
    if (mips_zswap_write(&zswap, 0, first, sizeof(first)) != 0)
        return 1;
    zswap.mz_entry[0].flags |= 0x10;
    if (mips_zswap_read(&zswap, 0, result, sizeof(result)) != EIO ||
        mips_zswap_get_stats(&zswap, &stats) != 0 ||
        stats.mzs_last_error != MIPS_ZSWAP_ERROR_METADATA)
        return 1;
    if (mips_zswap_init(&zswap, store, sizeof(store)) != 0)
        return 1;

    fill_noise(first, 0x12345678u);
    fill_noise(second, 0x87654321u);
    if (mips_zswap_write(&zswap, 0, first, sizeof(first)) != 0 ||
        mips_zswap_read(&zswap, 0, result, sizeof(result)) != 0 ||
        memcmp(first, result, sizeof(first)) != 0)
        return 1;

    /* Replacing a raw block must reuse its units even when the store is full. */
    if (mips_zswap_write(&zswap, 0, second, sizeof(second)) != 0 ||
        mips_zswap_read(&zswap, 0, result, sizeof(result)) != 0 ||
        memcmp(second, result, sizeof(second)) != 0)
        return 1;

    if (mips_zswap_write(&zswap, MIPS_ZSWAP_BLOCK_BYTES, first,
        sizeof(first)) != ENOSPC)
        return 1;
    mips_zswap_discard(&zswap, 0, 1);
    if (mips_zswap_write(&zswap, MIPS_ZSWAP_BLOCK_BYTES, first,
        sizeof(first)) != 0 ||
        mips_zswap_read(&zswap, MIPS_ZSWAP_BLOCK_BYTES, result,
        sizeof(result)) != 0 ||
        memcmp(first, result, sizeof(first)) != 0)
        return 1;

    memset(second, 0, sizeof(second));
    for (i = 100; i < 300; ++i)
        second[i] = (unsigned char)i;
    mips_zswap_discard(&zswap, 1, 1);
    if (mips_zswap_write(&zswap, 37, second + 37, 400) != 0)
        return 1;
    memset(result, 0, sizeof(result));
    if (mips_zswap_read(&zswap, 37, result + 37, 400) != 0 ||
        memcmp(second, result, sizeof(second)) != 0)
        return 1;

    mips_zswap_discard(&zswap, 0, 2);
    memset(first, 0x5a, sizeof(first));
    if (mips_zswap_write(&zswap, 0, first, sizeof(first)) != 0 ||
        zswap.mz_entry[0].units >=
        MIPS_ZSWAP_BLOCK_BYTES / MIPS_ZSWAP_UNIT_BYTES ||
        mips_zswap_read(&zswap, 0, result, sizeof(result)) != 0 ||
        memcmp(first, result, sizeof(first)) != 0)
        return 1;

    /* Backing-store damage must be reported, never returned as valid data. */
    corrupt_unit = zswap.mz_entry[0].unit;
    store[corrupt_unit * MIPS_ZSWAP_UNIT_BYTES] ^= 0x80;
    if (mips_zswap_read(&zswap, 0, result, sizeof(result)) != EIO ||
        mips_zswap_get_stats(&zswap, &stats) != 0 ||
        stats.mzs_read_errors != 1 ||
        (stats.mzs_last_error != MIPS_ZSWAP_ERROR_DECOMPRESS &&
        stats.mzs_last_error != MIPS_ZSWAP_ERROR_CHECKSUM) ||
        stats.mzs_last_error_block != 0)
        return 1;

    /* Reinitialise after intentional corruption for the range tests below. */
    if (mips_zswap_init(&zswap, store, sizeof(store)) != 0)
        return 1;
    mips_zswap_discard(&zswap, 0, 2);
    for (i = 0; i < sizeof(span); ++i)
        span[i] = (unsigned char)(i * 73 + 19);
    memset(span_result, 0, sizeof(span_result));
    if (mips_zswap_write(&zswap, 900, span, sizeof(span)) != 0 ||
        mips_zswap_read(&zswap, 900, span_result,
            sizeof(span_result)) != 0 ||
        memcmp(span, span_result, sizeof(span)) != 0)
        return 1;

    puts("MIPS zswap tests: ok");
    return 0;
}
