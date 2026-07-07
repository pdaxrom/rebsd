#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>

#include "../common/romfs_backend.h"

static int
ci20_romfs_backend_getinfo(struct mipsromfs_flash_info *info)
{
    bzero(info, sizeof(*info));
    return ENODEV;
}

static int
ci20_romfs_backend_read(unsigned offset, void *buffer, unsigned size)
{
    return ENODEV;
}

static int
ci20_romfs_backend_write_sector(unsigned offset, const void *buffer)
{
    return EROFS;
}

static int
ci20_romfs_backend_erase_sector(unsigned offset)
{
    return EROFS;
}

const struct mipsromfs_backend mipsromfs_backend = {
    ci20_romfs_backend_getinfo,
    ci20_romfs_backend_read,
    ci20_romfs_backend_write_sector,
    ci20_romfs_backend_erase_sector,
    0,
};
