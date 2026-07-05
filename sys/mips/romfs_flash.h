#ifndef _MIPS_ROMFS_FLASH_H_
#define _MIPS_ROMFS_FLASH_H_

#include <sys/ioctl.h>

#define MIPS_ROMFS_FLASH_SECTOR       4096u
#define MIPS_ROMFS_FLASH_MAX_TRANSFER MIPS_ROMFS_FLASH_SECTOR

struct mipsromfs_flash_info {
    unsigned jedec_id;
    unsigned rom_size;
    unsigned fw_size;
    unsigned romfs_offset;
    unsigned sector_size;
};

struct mipsromfs_flash_io {
    unsigned offset;
    unsigned size;
    char *buffer;
};

#define MIPSROMFSFLASHIOC_GETINFO \
    _IOR('F', 1, struct mipsromfs_flash_info)
#define MIPSROMFSFLASHIOC_READ \
    _IOW('F', 2, struct mipsromfs_flash_io)
#define MIPSROMFSFLASHIOC_WRITE \
    _IOW('F', 3, struct mipsromfs_flash_io)
#define MIPSROMFSFLASHIOC_ERASE \
    _IOW('F', 4, unsigned)

#endif
