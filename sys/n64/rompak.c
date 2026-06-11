#include <machine/rompak.h>

#define ROMPAK_TOC_MAGIC        0x544f4330u
#define ROMPAK_TOC_HEADER_SIZE  16u
#define ROMPAK_TOC_ENTRY_MIN    64u
#define ROMPAK_TOC_MAX_ENTRIES  256u

static unsigned char
rom_read8(unsigned offset)
{
    return *(const volatile unsigned char *)(N64_ROM_KSEG1_BASE + offset);
}

static unsigned
rom_read32be(unsigned offset)
{
    return ((unsigned)rom_read8(offset) << 24) |
        ((unsigned)rom_read8(offset + 1u) << 16) |
        ((unsigned)rom_read8(offset + 2u) << 8) |
        (unsigned)rom_read8(offset + 3u);
}

const volatile unsigned char *
n64_rompak_ptr(unsigned offset)
{
    return (const volatile unsigned char *)(N64_ROM_KSEG1_BASE + offset);
}

unsigned
n64_rompak_read32(unsigned offset)
{
    return rom_read32be(offset);
}

static int
rom_name_equals(unsigned offset, unsigned maxlen, const char *name)
{
    unsigned i;

    for (i = 0; i < maxlen; ++i) {
        unsigned char a = rom_read8(offset + i);
        unsigned char b = (unsigned char)name[i];

        if (a != b)
            return 0;
        if (a == '\0')
            return 1;
    }
    return 0;
}

static int
rompak_find_in_toc(unsigned toc_offset, const char *name,
    struct n64_rompak_entry *entry)
{
    unsigned toc_size;
    unsigned entry_info;
    unsigned entry_size;
    unsigned entry_count;
    unsigned i;

    toc_size = rom_read32be(toc_offset + 8u);
    entry_info = rom_read32be(toc_offset + 12u);
    entry_size = entry_info >> 16;
    entry_count = entry_info & 0xffffu;

    if (entry_size < ROMPAK_TOC_ENTRY_MIN ||
        entry_count == 0u ||
        entry_count > ROMPAK_TOC_MAX_ENTRIES ||
        toc_size < ROMPAK_TOC_HEADER_SIZE + entry_count * entry_size)
        return 0;

    for (i = 0; i < entry_count; ++i) {
        unsigned ent = toc_offset + ROMPAK_TOC_HEADER_SIZE + i * entry_size;

        if (rom_name_equals(ent + 8u, entry_size - 8u, name)) {
            entry->offset = rom_read32be(ent);
            entry->size = rom_read32be(ent + 4u);
            return 1;
        }
    }

    return 0;
}

int
n64_rompak_find(const char *name, struct n64_rompak_entry *entry)
{
    unsigned offset;

    for (offset = 0x1000u; offset < N64_ROM_TOC_SEARCH_SIZE; offset += 4u) {
        if (rom_read32be(offset) != ROMPAK_TOC_MAGIC)
            continue;
        if (rompak_find_in_toc(offset, name, entry))
            return 0;
    }

    entry->offset = 0u;
    entry->size = 0u;
    return -1;
}
