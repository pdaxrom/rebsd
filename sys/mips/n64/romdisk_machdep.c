#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <disk/romdisk.h>
#include <machine/romdisk.h>
#include <machine/rompak.h>

static struct n64_rompak_entry rootfs_entry;
static int rootfs_state;
static int rootfs_reported;

static int
romdisk_locate(void)
{
    if (rootfs_state == 0) {
        rootfs_state = n64_rompak_find("rootfs.img", &rootfs_entry) == 0 ?
            1 : -1;
    }
    if (!rootfs_reported) {
        rootfs_reported = 1;
        if (rootfs_state > 0) {
            printf("n64romdisk: rootfs offset=%x size=%x magic=%x\n",
                rootfs_entry.offset, rootfs_entry.size,
                n64_rompak_read32(rootfs_entry.offset));
        } else {
            printf("n64romdisk: rootfs.img not found in ROM TOC\n");
        }
    }

    return rootfs_state > 0 ? 0 : ENXIO;
}

static int
n64_romdisk_media(void *cookie, unsigned *size)
{
    (void)cookie;
    if (size == 0)
        return EINVAL;
    if (romdisk_locate() != 0 || rootfs_entry.size == 0)
        return ENXIO;
    *size = rootfs_entry.size;
    return 0;
}

static int
n64_romdisk_read(void *cookie, unsigned offset, void *data,
    unsigned nbytes)
{
    (void)cookie;
    n64_rompak_copy(rootfs_entry.offset + offset, data, nbytes);
    return 0;
}

static const struct romdisk_ops n64_romdisk_ops = {
    n64_romdisk_media,
    n64_romdisk_read
};

static const struct romdisk n64_romdisk = {
    0,
    0,
    N64_ROMDISK_ROOT_MINOR,
    DEV_BSHIFT,
    &n64_romdisk_ops,
    0
};

const struct romdisk *
romdisk_md_device(void)
{
    return &n64_romdisk;
}
