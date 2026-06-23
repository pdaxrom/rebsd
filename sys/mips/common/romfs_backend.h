#ifndef _MIPS_COMMON_ROMFS_BACKEND_H_
#define _MIPS_COMMON_ROMFS_BACKEND_H_

#include <sys/types.h>

struct mipsromfs_flash_info {
    unsigned jedec_id;
    unsigned rom_size;
    unsigned fw_size;
    unsigned romfs_offset;
    unsigned sector_size;
};

struct mipsromfs_backend {
    int (*getinfo)(struct mipsromfs_flash_info *info);
    int (*read)(unsigned offset, void *buffer, unsigned size);
    int (*write_sector)(unsigned offset, const void *buffer);
    int (*erase_sector)(unsigned offset);
};

extern const struct mipsromfs_backend mipsromfs_backend;

#endif
