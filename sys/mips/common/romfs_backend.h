#ifndef _MIPS_COMMON_ROMFS_BACKEND_H_
#define _MIPS_COMMON_ROMFS_BACKEND_H_

#include "../romfs_flash.h"

struct mipsromfs_backend {
    int (*getinfo)(struct mipsromfs_flash_info *info);
    int (*read)(unsigned offset, void *buffer, unsigned size);
    int (*write_sector)(unsigned offset, const void *buffer);
    int (*erase_sector)(unsigned offset);
    int (*sync)(void);
};

extern const struct mipsromfs_backend mipsromfs_backend;

#endif
