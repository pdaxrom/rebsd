#ifndef _N64_ROMPAK_H_
#define _N64_ROMPAK_H_

#define N64_ROM_PHYS_BASE       0x10000000u
#define N64_ROM_KSEG1_BASE      0xb0000000u
#define N64_ROM_TOC_SEARCH_SIZE 0x00100000u

struct n64_rompak_entry {
    unsigned offset;
    unsigned size;
};

void n64_rompak_copy(unsigned offset, void *dst, unsigned nbytes);
unsigned n64_rompak_read32(unsigned offset);
int n64_rompak_find(const char *name, struct n64_rompak_entry *entry);

#endif /* _N64_ROMPAK_H_ */
