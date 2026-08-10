#ifndef REBSD_CMD_ENDIANIO_H
#define REBSD_CMD_ENDIANIO_H

#include <stdio.h>

void endianio_set_big_endian(int big);
int endianio_is_big_endian(void);
unsigned endianio_get16(FILE *f);
unsigned endianio_get24(FILE *f);
unsigned endianio_get32(FILE *f);
void endianio_put16(unsigned value, FILE *f);
void endianio_put24(unsigned value, FILE *f);
void endianio_put32(unsigned value, FILE *f);

#endif
