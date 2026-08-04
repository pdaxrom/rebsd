/* Linux swap v1 on-disk header parser shared by all ReBSD platforms. */

#include <sys/errno.h>
#include <vm/swap_linux.h>

#define SWAP_LINUX_VERSION_OFFSET   1024u
#define SWAP_LINUX_LAST_PAGE_OFFSET 1028u
#define SWAP_LINUX_BAD_COUNT_OFFSET 1032u

static unsigned
swap_linux_u32(const unsigned char *data, unsigned byte_order)
{
    if (byte_order == SWAP_LINUX_ENDIAN_BIG)
        return ((unsigned)data[0] << 24) |
            ((unsigned)data[1] << 16) |
            ((unsigned)data[2] << 8) | data[3];
    return data[0] | ((unsigned)data[1] << 8) |
        ((unsigned)data[2] << 16) | ((unsigned)data[3] << 24);
}

static int
swap_linux_magic(const unsigned char *header, size_t header_bytes)
{
    const char *magic;
    unsigned index;

    if (header == 0 || header_bytes != SWAP_LINUX_PAGE_BYTES)
        return 0;
    magic = SWAP_LINUX_MAGIC;
    for (index = 0; index < SWAP_LINUX_MAGIC_BYTES; ++index)
        if (header[header_bytes - SWAP_LINUX_MAGIC_BYTES + index] !=
            (unsigned char)magic[index])
            return 0;
    return 1;
}

int
swap_linux_badpage(const unsigned char *header,
    const struct swap_linux_info *info, unsigned index, unsigned *page)
{
    size_t offset;

    if (header == 0 || info == 0 || page == 0 ||
        index >= info->sli_badpages)
        return EINVAL;
    offset = SWAP_LINUX_BADPAGES_OFFSET + index * 4u;
    if (offset > SWAP_LINUX_PAGE_BYTES - SWAP_LINUX_MAGIC_BYTES - 4u)
        return EINVAL;
    *page = swap_linux_u32(header + offset, info->sli_byte_order);
    return 0;
}

int
swap_linux_parse(const unsigned char *header, size_t header_bytes,
    size_t device_pages, struct swap_linux_info *info)
{
    struct swap_linux_info parsed;
    unsigned previous;
    unsigned page;
    unsigned version;
    unsigned index;
    unsigned byte_order;
    size_t max_badpages;

    if (header == 0 || info == 0 || header_bytes != SWAP_LINUX_PAGE_BYTES)
        return EINVAL;
    if (!swap_linux_magic(header, header_bytes))
        return ENOENT;

    version = swap_linux_u32(header + SWAP_LINUX_VERSION_OFFSET,
        SWAP_LINUX_ENDIAN_LITTLE);
    if (version == 1u)
        byte_order = SWAP_LINUX_ENDIAN_LITTLE;
    else if (swap_linux_u32(header + SWAP_LINUX_VERSION_OFFSET,
        SWAP_LINUX_ENDIAN_BIG) == 1u)
        byte_order = SWAP_LINUX_ENDIAN_BIG;
    else
        return EINVAL;

    parsed.sli_byte_order = byte_order;
    parsed.sli_last_page = swap_linux_u32(
        header + SWAP_LINUX_LAST_PAGE_OFFSET, byte_order);
    parsed.sli_badpages = swap_linux_u32(
        header + SWAP_LINUX_BAD_COUNT_OFFSET, byte_order);
    if (parsed.sli_last_page == 0 ||
        parsed.sli_last_page >= device_pages)
        return EINVAL;
    max_badpages = (SWAP_LINUX_PAGE_BYTES - SWAP_LINUX_MAGIC_BYTES -
        SWAP_LINUX_BADPAGES_OFFSET) / 4u;
    if (parsed.sli_badpages > max_badpages)
        return EINVAL;

    previous = 0;
    for (index = 0; index < parsed.sli_badpages; ++index) {
        if (swap_linux_badpage(header, &parsed, index, &page) != 0 ||
            page == 0 || page > parsed.sli_last_page || page <= previous)
            return EINVAL;
        previous = page;
    }
    *info = parsed;
    return 0;
}
