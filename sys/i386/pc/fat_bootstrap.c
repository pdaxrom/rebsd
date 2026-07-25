#include "boot.h"
#include "fat_bootstrap.h"

#include <sys/buf.h>
#include <sys/errno.h>

#include <disk/disk.h>
#include <fs/fat/fat.h>

#define I386_FAT_SMOKE_SIZE     700u
#define I386_FAT_INIT_MAX       (64u * 1024u)

struct i386_fat_disk {
    dev_t fd_dev;
};

static unsigned char i386_fat_smoke_data[I386_FAT_SMOKE_SIZE];
static unsigned char i386_fat_init_data[I386_FAT_INIT_MAX];
static unsigned i386_fat_init_size;
static int i386_fat_init_ready;

static void
i386_fat_zero(void *arg, unsigned length)
{
    unsigned char *data;

    data = (unsigned char *)arg;
    while (length-- != 0)
        *data++ = 0;
}

static int
i386_fat_has(const unsigned char *data, unsigned offset, const char *wanted)
{
    while (*wanted != '\0') {
        if (offset >= I386_FAT_SMOKE_SIZE ||
            data[offset] != (unsigned char)*wanted)
            return 0;
        ++offset;
        ++wanted;
    }
    return 1;
}

static int
i386_fat_read_sector(void *arg, unsigned sector, unsigned char *data)
{
    struct i386_fat_disk *disk;
    struct buf bp;

    disk = (struct i386_fat_disk *)arg;
    i386_fat_zero(&bp, sizeof(bp));
    bp.b_dev = disk->fd_dev;
    bp.b_blkno = (blkno_t)sector;
    bp.b_bcount = FAT_SECTOR_SIZE;
    bp.b_addr = (caddr_t)data;
    bp.b_flags = B_READ | B_PHYS;
    disk_bdev_strategy(&bp);
    if ((bp.b_flags & (B_DONE | B_ERROR)) != B_DONE || bp.b_resid != 0)
        return bp.b_error != 0 ? bp.b_error : EIO;
    return 0;
}

int
i386_fat_bootstrap(dev_t dev, unsigned media_sectors)
{
    struct i386_fat_disk disk;
    struct fat_ro reader;
    struct fat_ro_node node;
    struct fat_ro_node root;
    unsigned count;
    int error;

    i386_fat_init_ready = 0;
    i386_fat_init_size = 0;
    disk.fd_dev = dev;
    error = fat_ro_mount(&reader, media_sectors, i386_fat_read_sector,
        &disk);
    if (error == EINVAL || error == EOPNOTSUPP) {
        i386_early_puts("fat-root: unavailable\n");
        return 0;
    }
    if (error != 0) {
        i386_early_puts("fat-mount: failed\n");
        return error;
    }
    i386_early_puts("fat-mount: fat");
    i386_early_puts(reader.fr_volume.fv_type == FAT_TYPE_16 ?
        "16,read-only\n" : "32,read-only\n");

    error = fat_ro_lookup(&reader, "/boot/root.txt", &node);
    if (error == ENOENT) {
        i386_early_puts("fat-root-lookup: absent\n");
        goto init_lookup;
    }
    if (error != 0 || (node.fn_attr & FAT_ATTR_DIRECTORY) != 0 ||
        node.fn_size != I386_FAT_SMOKE_SIZE) {
        i386_early_puts("fat-root-lookup: failed\n");
        return error != 0 ? error : EIO;
    }
    i386_early_puts("fat-root-lookup: ok\n");

    i386_fat_zero(i386_fat_smoke_data, sizeof(i386_fat_smoke_data));
    error = fat_ro_read(&reader, &node, 0, i386_fat_smoke_data,
        sizeof(i386_fat_smoke_data), &count);
    if (error != 0 || count != sizeof(i386_fat_smoke_data)) {
        i386_early_puts("fat-root-read: failed\n");
        return error != 0 ? error : EIO;
    }
    if (!i386_fat_has(i386_fat_smoke_data, 0, "REBSD FAT ROOT\n")) {
        i386_early_puts("fat-root-read: external\n");
        return 0;
    }
    i386_early_puts("fat-root-read: ok\n");
    if (!i386_fat_has(i386_fat_smoke_data, FAT_SECTOR_SIZE,
        "REBSD FAT SECOND CLUSTER\n")) {
        i386_early_puts("fat-root-chain: failed\n");
        return EIO;
    }
    i386_early_puts("fat-root-chain: ok\n");

    count = 1;
    error = fat_ro_read(&reader, &node, node.fn_size,
        i386_fat_smoke_data, 1, &count);
    if (error != 0 || count != 0) {
        i386_early_puts("fat-root-eof: failed\n");
        return error != 0 ? error : EIO;
    }
    i386_early_puts("fat-root-eof: ok\n");

    if (fat_ro_lookup(&reader, "/boot/missing.txt", &node) != ENOENT) {
        i386_early_puts("fat-root-missing: failed\n");
        return EIO;
    }
    i386_early_puts("fat-root-missing: enoent\n");

    error = fat_ro_lookup(&reader, "/", &root);
    if (error != 0 ||
        fat_ro_read(&reader, &root, 0, i386_fat_smoke_data, 1,
        &count) != EISDIR) {
        i386_early_puts("fat-root-directory: failed\n");
        return error != 0 ? error : EIO;
    }
    i386_early_puts("fat-root-directory: eisdir\n");

init_lookup:
    error = fat_ro_lookup(&reader, "/sbin/init", &node);
    if (error == ENOENT) {
        i386_early_puts("fat-init-lookup: absent\n");
        return 0;
    }
    if (error != 0 || (node.fn_attr & FAT_ATTR_DIRECTORY) != 0 ||
        node.fn_size == 0 || node.fn_size > sizeof(i386_fat_init_data)) {
        i386_early_puts("fat-init-lookup: unavailable\n");
        return 0;
    }
    i386_early_puts("fat-init-lookup: ok\n");

    error = fat_ro_read(&reader, &node, 0, i386_fat_init_data,
        node.fn_size, &count);
    if (error != 0 || count != node.fn_size) {
        i386_early_puts("fat-init-read: failed\n");
        return error != 0 ? error : EIO;
    }
    i386_fat_init_size = node.fn_size;
    i386_fat_init_ready = 1;
    i386_early_puts("fat-init-read: ok\n");
    return 0;
}

int
i386_fat_bootstrap_init_image(const void **data, unsigned *size)
{
    if (data == (const void **)0 || size == (unsigned *)0)
        return EINVAL;
    if (!i386_fat_init_ready)
        return ENOENT;
    *data = i386_fat_init_data;
    *size = i386_fat_init_size;
    return 0;
}
