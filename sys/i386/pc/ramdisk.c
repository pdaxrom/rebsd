/* i686 attachment for the common runtime-configurable RAM block driver. */

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/errno.h>

#include <disk/ramdisk.h>

#include "ramdisk.h"

static unsigned char i386_var_storage[I386_RAMDISK_VAR_BYTES]
    __attribute__((aligned(4096)));
static struct ramdisk_controller i386_ramdisks;
static int i386_ramdisks_initialized;

static int
i386_ramdisk_attach(void)
{
    int unit;
    int error;

    if (i386_ramdisks_initialized)
        return 0;
    ramdisk_controller_init(&i386_ramdisks);
    error = ramdisk_controller_register_pool(&i386_ramdisks,
        I386_RAMDISK_VAR_MINOR, i386_var_storage,
        sizeof(i386_var_storage), 0);
    if (error != 0)
        return error;
    for (unit = 1; unit < RAMDISK_MAX_DEVICES; ++unit) {
        error = ramdisk_controller_register_pool(&i386_ramdisks,
            unit, 0, 0, RAMDISK_POOL_DYNAMIC);
        if (error != 0)
            return error;
    }
    i386_ramdisks_initialized = 1;
    return 0;
}

int
i386_ramdisk_open(dev_t dev, int flag, int mode)
{
    if (i386_ramdisk_attach() != 0)
        return ENXIO;
    return ramdisk_controller_open(&i386_ramdisks, dev, flag, mode);
}

int
i386_ramdisk_close(dev_t dev, int flag, int mode)
{
    if (i386_ramdisk_attach() != 0)
        return ENXIO;
    return ramdisk_controller_close(&i386_ramdisks, dev, flag, mode);
}

void
i386_ramdisk_strategy(struct buf *bp)
{
    if (i386_ramdisk_attach() == 0) {
        ramdisk_controller_strategy(&i386_ramdisks, bp);
        return;
    }
    bp->b_error = ENXIO;
    bp->b_flags |= B_ERROR;
    biodone(bp);
}

daddr_t
i386_ramdisk_size(dev_t dev)
{
    if (i386_ramdisk_attach() != 0)
        return 0;
    return ramdisk_controller_size(&i386_ramdisks, dev);
}

int
i386_ramdisk_ioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
    if (i386_ramdisk_attach() != 0)
        return ENXIO;
    return ramdisk_controller_ioctl(&i386_ramdisks, dev, cmd, addr,
        flag);
}
