#include <machine/rompak.h>
#include <machine/n64pi.h>

#define ROMPAK_TOC_MAGIC        0x544f4330u
#define ROMPAK_TOC_HEADER_SIZE  16u
#define ROMPAK_TOC_ENTRY_MIN    64u
#define ROMPAK_TOC_MAX_ENTRIES  256u

static char rompak_pi_owner;

static unsigned char
rom_read8(unsigned offset)
{
    unsigned word;
    unsigned shift;

    /* CPU-side PI ROM access must use aligned 32-bit reads. */
    word = *(const volatile unsigned *)(N64_ROM_KSEG1_BASE +
        (offset & ~3u));
    shift = (3u - (offset & 3u)) * 8u;
    return (unsigned char)(word >> shift);
}

static unsigned
rom_read32be(unsigned offset)
{
    if ((offset & 3u) == 0u)
        return *(const volatile unsigned *)(N64_ROM_KSEG1_BASE + offset);

    return ((unsigned)rom_read8(offset) << 24) |
        ((unsigned)rom_read8(offset + 1u) << 16) |
        ((unsigned)rom_read8(offset + 2u) << 8) |
        (unsigned)rom_read8(offset + 3u);
}

void
n64_rompak_copy(unsigned offset, void *dst, unsigned nbytes)
{
    unsigned char *out = dst;

    if (n64pi_bus_enter(&rompak_pi_owner) != 0)
        return;
    while (nbytes-- != 0u)
        *out++ = rom_read8(offset++);
    n64pi_bus_leave(&rompak_pi_owner);
}

unsigned
n64_rompak_read32(unsigned offset)
{
    unsigned value;

    if (n64pi_bus_enter(&rompak_pi_owner) != 0)
        return 0;
    value = rom_read32be(offset);
    n64pi_bus_leave(&rompak_pi_owner);
    return value;
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

    if (n64pi_bus_enter(&rompak_pi_owner) != 0)
        return -1;
    for (offset = 0x1000u; offset < N64_ROM_TOC_SEARCH_SIZE; offset += 4u) {
        if (rom_read32be(offset) != ROMPAK_TOC_MAGIC)
            continue;
        if (rompak_find_in_toc(offset, name, entry)) {
            n64pi_bus_leave(&rompak_pi_owner);
            return 0;
        }
    }

    entry->offset = 0u;
    entry->size = 0u;
    n64pi_bus_leave(&rompak_pi_owner);
    return -1;
}
