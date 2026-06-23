#include <sys/param.h>

#include <machine/n64cart_flash.h>

#include "../common/romfs_backend.h"

static int
n64romfs_getinfo(struct mipsromfs_flash_info *info)
{
    struct n64cart_flash_info n64info;
    int error;

    error = n64cart_flash_getinfo(&n64info);
    if (error)
        return error;
    info->jedec_id = n64info.jedec_id;
    info->rom_size = n64info.rom_size;
    info->fw_size = n64info.fw_size;
    info->romfs_offset = n64info.romfs_offset;
    info->sector_size = n64info.sector_size;
    return 0;
}

const struct mipsromfs_backend mipsromfs_backend = {
    n64romfs_getinfo,
    n64cart_flash_read_raw,
    n64cart_flash_write_sector_raw,
    n64cart_flash_erase_sector_raw,
};
