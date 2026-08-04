#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <disk/ramcomp.h>

static struct ramcomp ramcomp;
static unsigned char store[RAMCOMP_BLOCK_BYTES];
static unsigned char metadata[256];
static unsigned char first[RAMCOMP_BLOCK_BYTES];
static unsigned char second[RAMCOMP_BLOCK_BYTES];
static unsigned char result[RAMCOMP_BLOCK_BYTES];
static unsigned char span[300];
static unsigned char span_result[300];

static void
fill_noise(unsigned char *data, unsigned seed)
{
    unsigned value;
    unsigned i;

    value = seed;
    for (i = 0; i < RAMCOMP_BLOCK_BYTES; ++i) {
        value ^= value << 13;
        value ^= value >> 17;
        value ^= value << 5;
        data[i] = (unsigned char)value;
    }
}

int
main(void)
{
    struct ramcomp_stats stats;
    unsigned corrupt_unit;
    unsigned i;

    if (ramcomp_init(&ramcomp, store, sizeof(store),
        2 * RAMCOMP_BLOCK_BYTES, metadata, sizeof(metadata)) != 0)
        return 1;

    memset(result, 0xa5, sizeof(result));
    if (ramcomp_read(&ramcomp, 0, result, sizeof(result)) != 0)
        return 1;
    for (i = 0; i < sizeof(result); ++i)
        if (result[i] != 0)
            return 1;

    memset(first, 0, sizeof(first));
    if (ramcomp_write(&ramcomp, 0, first, sizeof(first)) != 0)
        return 1;
    ramcomp.rc_entry[0].flags |= 0x10;
    if (ramcomp_read(&ramcomp, 0, result, sizeof(result)) != EIO ||
        ramcomp_get_stats(&ramcomp, &stats) != 0 ||
        stats.rcs_last_error != RAMCOMP_ERROR_METADATA)
        return 1;
    if (ramcomp_init(&ramcomp, store, sizeof(store),
        2 * RAMCOMP_BLOCK_BYTES, metadata, sizeof(metadata)) != 0)
        return 1;

    fill_noise(first, 0x12345678u);
    fill_noise(second, 0x87654321u);
    if (ramcomp_write(&ramcomp, 0, first, sizeof(first)) != 0 ||
        ramcomp_read(&ramcomp, 0, result, sizeof(result)) != 0 ||
        memcmp(first, result, sizeof(first)) != 0)
        return 1;

    /* Replacing a raw block must reuse its units even when the store is full. */
    if (ramcomp_write(&ramcomp, 0, second, sizeof(second)) != 0 ||
        ramcomp_read(&ramcomp, 0, result, sizeof(result)) != 0 ||
        memcmp(second, result, sizeof(second)) != 0)
        return 1;

    if (ramcomp_write(&ramcomp, RAMCOMP_BLOCK_BYTES, first,
        sizeof(first)) != ENOSPC)
        return 1;
    ramcomp_discard(&ramcomp, 0, 1);
    if (ramcomp_write(&ramcomp, RAMCOMP_BLOCK_BYTES, first,
        sizeof(first)) != 0 ||
        ramcomp_read(&ramcomp, RAMCOMP_BLOCK_BYTES, result,
        sizeof(result)) != 0 ||
        memcmp(first, result, sizeof(first)) != 0)
        return 1;

    memset(second, 0, sizeof(second));
    for (i = 100; i < 300; ++i)
        second[i] = (unsigned char)i;
    ramcomp_discard(&ramcomp, 1, 1);
    if (ramcomp_write(&ramcomp, 37, second + 37, 400) != 0)
        return 1;
    memset(result, 0, sizeof(result));
    if (ramcomp_read(&ramcomp, 37, result + 37, 400) != 0 ||
        memcmp(second, result, sizeof(second)) != 0)
        return 1;

    ramcomp_discard(&ramcomp, 0, 2);
    memset(first, 0x5a, sizeof(first));
    if (ramcomp_write(&ramcomp, 0, first, sizeof(first)) != 0 ||
        ramcomp.rc_entry[0].units >=
        RAMCOMP_BLOCK_BYTES / RAMCOMP_UNIT_BYTES ||
        ramcomp_read(&ramcomp, 0, result, sizeof(result)) != 0 ||
        memcmp(first, result, sizeof(first)) != 0)
        return 1;

    /* Backing-store damage must be reported, never returned as valid data. */
    corrupt_unit = ramcomp.rc_entry[0].unit;
    store[corrupt_unit * RAMCOMP_UNIT_BYTES] ^= 0x80;
    if (ramcomp_read(&ramcomp, 0, result, sizeof(result)) != EIO ||
        ramcomp_get_stats(&ramcomp, &stats) != 0 ||
        stats.rcs_read_errors != 1 ||
        (stats.rcs_last_error != RAMCOMP_ERROR_DECOMPRESS &&
        stats.rcs_last_error != RAMCOMP_ERROR_CHECKSUM) ||
        stats.rcs_last_error_block != 0)
        return 1;

    /* Reinitialise after intentional corruption for the range tests below. */
    if (ramcomp_init(&ramcomp, store, sizeof(store),
        2 * RAMCOMP_BLOCK_BYTES, metadata, sizeof(metadata)) != 0)
        return 1;
    ramcomp_discard(&ramcomp, 0, 2);
    for (i = 0; i < sizeof(span); ++i)
        span[i] = (unsigned char)(i * 73 + 19);
    memset(span_result, 0, sizeof(span_result));
    if (ramcomp_write(&ramcomp, 900, span, sizeof(span)) != 0 ||
        ramcomp_read(&ramcomp, 900, span_result,
            sizeof(span_result)) != 0 ||
        memcmp(span, span_result, sizeof(span)) != 0)
        return 1;

    puts("ramcomp tests: ok");
    return 0;
}
