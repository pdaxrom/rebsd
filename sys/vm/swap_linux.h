#ifndef _VM_SWAP_LINUX_H_
#define _VM_SWAP_LINUX_H_

#include <sys/types.h>

#define SWAP_LINUX_PAGE_BYTES       4096u
#define SWAP_LINUX_MAGIC_BYTES      10u
#define SWAP_LINUX_BADPAGES_OFFSET  1536u
#define SWAP_LINUX_MAGIC            "SWAPSPACE2"

#define SWAP_LINUX_ENDIAN_LITTLE    1u
#define SWAP_LINUX_ENDIAN_BIG       2u

struct swap_linux_info {
    unsigned sli_byte_order;
    unsigned sli_last_page;
    unsigned sli_badpages;
};

int swap_linux_parse(const unsigned char *header, size_t header_bytes,
    size_t device_pages, struct swap_linux_info *info);
int swap_linux_badpage(const unsigned char *header,
    const struct swap_linux_info *info, unsigned index,
    unsigned *page);

#endif /* _VM_SWAP_LINUX_H_ */
