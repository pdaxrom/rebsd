/*
 * Target-endian a.out object I/O helpers.
 *
 * The RetroBSD a.out structures are used in memory, but object files must be
 * read and written in the target byte order.  Do not read or write struct
 * exec/nlist/ranlib records directly in toolchain code.
 */
#ifndef AOUTIO_H
#define AOUTIO_H

#include <stdio.h>
#include <a.out.h>
#include <nlist.h>

#define AOUT_ENDIAN_LITTLE 0
#define AOUT_ENDIAN_BIG    1

void aout_set_big_endian(int big);
int aout_is_big_endian(void);

unsigned aout_get16(FILE *f);
unsigned aout_get24(FILE *f);
unsigned aout_get32(FILE *f);
void aout_put16(unsigned value, FILE *f);
void aout_put24(unsigned value, FILE *f);
void aout_put32(unsigned value, FILE *f);

int aout_read_exec(FILE *f, struct exec *hdr);
int aout_read_exec_fd(int fd, struct exec *hdr);
void aout_write_exec(FILE *f, const struct exec *hdr);
int aout_write_exec_fd(int fd, const struct exec *hdr);

int aout_read_sym(FILE *f, struct nlist *sym, int alloc_name);
void aout_write_sym(FILE *f, const struct nlist *sym);

#endif
