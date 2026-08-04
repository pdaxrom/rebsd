#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <vm/swap_linux.h>

static unsigned char header[SWAP_LINUX_PAGE_BYTES];

static void
put_u32(unsigned offset, unsigned value, unsigned byte_order)
{
    if (byte_order == SWAP_LINUX_ENDIAN_BIG) {
        header[offset] = (unsigned char)(value >> 24);
        header[offset + 1] = (unsigned char)(value >> 16);
        header[offset + 2] = (unsigned char)(value >> 8);
        header[offset + 3] = (unsigned char)value;
    } else {
        header[offset] = (unsigned char)value;
        header[offset + 1] = (unsigned char)(value >> 8);
        header[offset + 2] = (unsigned char)(value >> 16);
        header[offset + 3] = (unsigned char)(value >> 24);
    }
}

static void
make_header(unsigned byte_order)
{
    memset(header, 0, sizeof(header));
    put_u32(1024, 1, byte_order);
    put_u32(1028, 1023, byte_order);
    put_u32(1032, 2, byte_order);
    put_u32(SWAP_LINUX_BADPAGES_OFFSET, 17, byte_order);
    put_u32(SWAP_LINUX_BADPAGES_OFFSET + 4, 99, byte_order);
    memcpy(header + sizeof(header) - SWAP_LINUX_MAGIC_BYTES,
        SWAP_LINUX_MAGIC, SWAP_LINUX_MAGIC_BYTES);
}

static int
check_header(unsigned byte_order)
{
    struct swap_linux_info info;
    unsigned page;

    make_header(byte_order);
    if (swap_linux_parse(header, sizeof(header), 2048, &info) != 0 ||
        info.sli_byte_order != byte_order || info.sli_last_page != 1023 ||
        info.sli_badpages != 2 ||
        swap_linux_badpage(header, &info, 0, &page) != 0 || page != 17 ||
        swap_linux_badpage(header, &info, 1, &page) != 0 || page != 99)
        return 1;
    return 0;
}

int
main(void)
{
    struct swap_linux_info info;

    memset(header, 0, sizeof(header));
    if (swap_linux_parse(header, sizeof(header), 2048, &info) != ENOENT ||
        check_header(SWAP_LINUX_ENDIAN_LITTLE) != 0 ||
        check_header(SWAP_LINUX_ENDIAN_BIG) != 0)
        return 1;

    make_header(SWAP_LINUX_ENDIAN_LITTLE);
    put_u32(1028, 2048, SWAP_LINUX_ENDIAN_LITTLE);
    if (swap_linux_parse(header, sizeof(header), 2048, &info) != EINVAL)
        return 1;
    make_header(SWAP_LINUX_ENDIAN_LITTLE);
    put_u32(SWAP_LINUX_BADPAGES_OFFSET + 4, 17,
        SWAP_LINUX_ENDIAN_LITTLE);
    if (swap_linux_parse(header, sizeof(header), 2048, &info) != EINVAL)
        return 1;

    puts("Linux swap header tests: ok");
    return 0;
}
