#include "aoutio.h"

#include <stdlib.h>
#include <unistd.h>

static int aout_big_endian;

void
aout_set_big_endian(int big)
{
    aout_big_endian = big != 0;
}

int
aout_is_big_endian(void)
{
    return aout_big_endian;
}

unsigned
aout_get16(FILE *f)
{
    int b0, b1;

    b0 = getc(f);
    b1 = getc(f);
    if (b0 == EOF || b1 == EOF)
        return 0;
    if (aout_big_endian)
        return ((unsigned)b0 << 8) | (unsigned)b1;
    return (unsigned)b0 | ((unsigned)b1 << 8);
}

unsigned
aout_get24(FILE *f)
{
    int b0, b1, b2;

    b0 = getc(f);
    b1 = getc(f);
    b2 = getc(f);
    if (b0 == EOF || b1 == EOF || b2 == EOF)
        return 0;
    if (aout_big_endian)
        return ((unsigned)b0 << 16) | ((unsigned)b1 << 8) | (unsigned)b2;
    return (unsigned)b0 | ((unsigned)b1 << 8) | ((unsigned)b2 << 16);
}

unsigned
aout_get32(FILE *f)
{
    int b0, b1, b2, b3;

    b0 = getc(f);
    b1 = getc(f);
    b2 = getc(f);
    b3 = getc(f);
    if (b0 == EOF || b1 == EOF || b2 == EOF || b3 == EOF)
        return 0;
    if (aout_big_endian)
        return ((unsigned)b0 << 24) | ((unsigned)b1 << 16) |
            ((unsigned)b2 << 8) | (unsigned)b3;
    return (unsigned)b0 | ((unsigned)b1 << 8) | ((unsigned)b2 << 16) |
        ((unsigned)b3 << 24);
}

void
aout_put16(unsigned value, FILE *f)
{
    if (aout_big_endian) {
        putc(value >> 8, f);
        putc(value, f);
    } else {
        putc(value, f);
        putc(value >> 8, f);
    }
}

void
aout_put24(unsigned value, FILE *f)
{
    if (aout_big_endian) {
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
aout_put32(unsigned value, FILE *f)
{
    if (aout_big_endian) {
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

static void
aout_store32(unsigned char *p, unsigned value)
{
    if (aout_big_endian) {
        p[0] = value >> 24;
        p[1] = value >> 16;
        p[2] = value >> 8;
        p[3] = value;
    } else {
        p[0] = value;
        p[1] = value >> 8;
        p[2] = value >> 16;
        p[3] = value >> 24;
    }
}

static void
aout_decode_exec(const unsigned char *buf, struct exec *hdr)
{
    unsigned i;
    unsigned fields[8];

    for (i = 0; i < 8; i++) {
        const unsigned char *p = buf + i * 4;
        if (aout_big_endian)
            fields[i] = ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) |
                ((unsigned)p[2] << 8) | p[3];
        else
            fields[i] = p[0] | ((unsigned)p[1] << 8) |
                ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
    }

    hdr->a_midmag = fields[0];
    hdr->a_text = fields[1];
    hdr->a_data = fields[2];
    hdr->a_bss = fields[3];
    hdr->a_reltext = fields[4];
    hdr->a_reldata = fields[5];
    hdr->a_syms = fields[6];
    hdr->a_entry = fields[7];
}

static void
aout_encode_exec(unsigned char *buf, const struct exec *hdr)
{
    aout_store32(buf + 0, hdr->a_magic);
    aout_store32(buf + 4, hdr->a_text);
    aout_store32(buf + 8, hdr->a_data);
    aout_store32(buf + 12, hdr->a_bss);
    aout_store32(buf + 16, hdr->a_reltext);
    aout_store32(buf + 20, hdr->a_reldata);
    aout_store32(buf + 24, hdr->a_syms);
    aout_store32(buf + 28, hdr->a_entry);
}

int
aout_read_exec(FILE *f, struct exec *hdr)
{
    unsigned char buf[8 * 4];

    if (fread(buf, 1, sizeof(buf), f) != sizeof(buf))
        return 0;
    aout_decode_exec(buf, hdr);
    return 1;
}

int
aout_read_exec_fd(int fd, struct exec *hdr)
{
    unsigned char buf[8 * 4];

    if (read(fd, buf, sizeof(buf)) != (int)sizeof(buf))
        return 0;
    aout_decode_exec(buf, hdr);
    return 1;
}

void
aout_write_exec(FILE *f, const struct exec *hdr)
{
    unsigned char buf[8 * 4];

    aout_encode_exec(buf, hdr);
    fwrite(buf, 1, sizeof(buf), f);
}

int
aout_write_exec_fd(int fd, const struct exec *hdr)
{
    unsigned char buf[8 * 4];

    aout_encode_exec(buf, hdr);
    return write(fd, buf, sizeof(buf)) == (int)sizeof(buf);
}

int
aout_read_sym(FILE *f, struct nlist *sym, int alloc_name)
{
    int len;
    int i;

    len = getc(f);
    if (len <= 0)
        return 0;
    sym->n_len = len;
    sym->n_type = getc(f);
    sym->n_value = aout_get32(f);
    if (alloc_name) {
        sym->n_name = malloc(sym->n_len + 1);
        if (!sym->n_name)
            return -1;
        for (i = 0; i < sym->n_len; i++)
            sym->n_name[i] = getc(f);
        sym->n_name[sym->n_len] = '\0';
    } else {
        sym->n_name = 0;
        fseek(f, sym->n_len, SEEK_CUR);
    }
    return sym->n_len + 6;
}

void
aout_write_sym(FILE *f, const struct nlist *sym)
{
    int i;

    putc(sym->n_len, f);
    putc(sym->n_type, f);
    aout_put32(sym->n_value, f);
    for (i = 0; i < sym->n_len; i++)
        putc(sym->n_name[i], f);
}
