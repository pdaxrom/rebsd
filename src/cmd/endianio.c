#include "endianio.h"

static int target_big_endian;

void
endianio_set_big_endian(int big)
{
    target_big_endian = big != 0;
}

int
endianio_is_big_endian(void)
{
    return target_big_endian;
}

unsigned
endianio_get16(FILE *f)
{
    int b0 = getc(f), b1 = getc(f);

    if (b0 == EOF || b1 == EOF)
        return 0;
    if (target_big_endian)
        return ((unsigned)b0 << 8) | (unsigned)b1;
    return (unsigned)b0 | ((unsigned)b1 << 8);
}

unsigned
endianio_get24(FILE *f)
{
    int b0 = getc(f), b1 = getc(f), b2 = getc(f);

    if (b0 == EOF || b1 == EOF || b2 == EOF)
        return 0;
    if (target_big_endian)
        return ((unsigned)b0 << 16) | ((unsigned)b1 << 8) | (unsigned)b2;
    return (unsigned)b0 | ((unsigned)b1 << 8) | ((unsigned)b2 << 16);
}

unsigned
endianio_get32(FILE *f)
{
    int b0 = getc(f), b1 = getc(f), b2 = getc(f), b3 = getc(f);

    if (b0 == EOF || b1 == EOF || b2 == EOF || b3 == EOF)
        return 0;
    if (target_big_endian)
        return ((unsigned)b0 << 24) | ((unsigned)b1 << 16) |
            ((unsigned)b2 << 8) | (unsigned)b3;
    return (unsigned)b0 | ((unsigned)b1 << 8) | ((unsigned)b2 << 16) |
        ((unsigned)b3 << 24);
}

void
endianio_put16(unsigned value, FILE *f)
{
    if (target_big_endian) {
        putc(value >> 8, f);
        putc(value, f);
    } else {
        putc(value, f);
        putc(value >> 8, f);
    }
}

void
endianio_put24(unsigned value, FILE *f)
{
    if (target_big_endian) {
        putc(value >> 16, f);
        putc(value >> 8, f);
        putc(value, f);
    } else {
        putc(value, f);
        putc(value >> 8, f);
        putc(value >> 16, f);
    }
}

void
endianio_put32(unsigned value, FILE *f)
{
    if (target_big_endian) {
        putc(value >> 24, f);
        putc(value >> 16, f);
        putc(value >> 8, f);
        putc(value, f);
    } else {
        putc(value, f);
        putc(value >> 8, f);
        putc(value >> 16, f);
        putc(value >> 24, f);
    }
}
