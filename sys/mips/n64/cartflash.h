#ifndef _N64_CARTFLASH_H_
#define _N64_CARTFLASH_H_

#ifdef KERNEL
#include "../romfs_flash.h"
#else
#include <mips/romfs_flash.h>
#endif

#define N64CART_FLASH_SECTOR       MIPS_ROMFS_FLASH_SECTOR
#define N64CART_FLASH_MAX_TRANSFER MIPS_ROMFS_FLASH_MAX_TRANSFER

struct n64cart_flash_info {
    unsigned jedec_id;
    unsigned rom_size;
    unsigned fw_size;
    unsigned romfs_offset;
    unsigned sector_size;
};

struct n64cart_flash_io {
    unsigned offset;
    unsigned size;
    char *buffer;
};

#define N64CARTFLASHIOC_GETINFO _IOR('F', 1, struct n64cart_flash_info)
#define N64CARTFLASHIOC_READ    _IOW('F', 2, struct n64cart_flash_io)
#define N64CARTFLASHIOC_WRITE   _IOW('F', 3, struct n64cart_flash_io)
#define N64CARTFLASHIOC_ERASE   _IOW('F', 4, unsigned)

#endif
