#include "boot.h"
#include "vfs_bootstrap.h"

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/dir.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/fs.h>
#include <sys/inode.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/proc.h>
#include <sys/stat.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/user.h>

#include <fs/fat/fat.h>

#define I386_VFS_INIT_MAX       (64u * 1024u)
static unsigned char i386_vfs_init_data[I386_VFS_INIT_MAX];
static unsigned i386_vfs_init_size;
static int i386_vfs_initialized;
static int i386_vfs_mounted;
static dev_t i386_vfs_dev;
static int i386_vfs_type;

static void
i386_vfs_init(void)
{
    if (i386_vfs_initialized)
        return;
    bioinit();
    ihinit();
    nchinit();
    coutinit();
    fatattach(0);
    i386_vfs_initialized = 1;
}

static int
i386_vfs_has_init(void)
{
    struct nameidata nd;
    struct inode *ip;
    int error;

    bzero(&nd, sizeof(nd));
    NDINIT_KERNEL(&nd, LOOKUP, FOLLOW, "/sbin/init");
    u.u_error = 0;
    ip = namei(&nd);
    if (ip == 0)
        return u.u_error != 0 ? u.u_error : ENOENT;
    error = (ip->i_mode & IFMT) == IFREG && ip->i_size > 0 ? 0 : ENOEXEC;
    iput(ip);
    return error;
}

static int
i386_vfs_select_root(int type, dev_t dev)
{
    int error;

    error = vfs_mountroot(type, dev, MNT_RDONLY | MNT_NOATIME, &rootdir);
    if (error != 0)
        return error;
    error = i386_vfs_has_init();
    if (error != 0) {
        int unmount_error;

        unmount_error = vfs_unmountroot(&rootdir);
        return unmount_error != 0 ? unmount_error : error;
    }
    i386_vfs_dev = dev;
    i386_vfs_type = type;
    rootdev = dev;
    i386_vfs_mounted = 1;
    return 0;
}

int
i386_vfs_bootstrap_mount(dev_t preferred_dev, dev_t fallback_dev)
{
    int error;

    if (i386_vfs_mounted)
        return fallback_dev == i386_vfs_dev ||
            preferred_dev == i386_vfs_dev ? 0 : EBUSY;
    i386_vfs_init();

    if (preferred_dev != NODEV) {
        error = i386_vfs_select_root(MOUNT_FAT, preferred_dev);
        if (error == 0) {
            i386_early_puts("vfs-root: fat,read-only\n");
            goto selected;
        }
        if (error == ENOENT || error == ENOEXEC)
            i386_early_puts("vfs-root: fat,no-init\n");
        else if (error == EINVAL || error == EOPNOTSUPP || error == ENXIO)
            i386_early_puts("vfs-root: fat,unavailable\n");
        else {
            i386_early_puts("vfs-root: fat,failed\n");
            return error;
        }
    }

    error = i386_vfs_select_root(MOUNT_UFS, fallback_dev);
    if (error != 0) {
        i386_early_puts("vfs-root: ufs,failed\n");
        return error;
    }
    i386_early_puts("vfs-root: ufs,read-only\n");

selected:
    igrab(rootdir);
    IUNLOCK(rootdir);
    u.u_cdir = rootdir;
    return 0;
}

int
i386_vfs_bootstrap_init_image(const void **data, unsigned *size)
{
    struct nameidata nd;
    struct inode *ip;
    int residual;
    int error;

    if (data == (const void **)0 || size == (unsigned *)0)
        return EINVAL;
    if (!i386_vfs_mounted)
        return ENOENT;
    if (rootdir == 0 || u.u_cdir != rootdir ||
        (i386_vfs_type != MOUNT_FAT && i386_vfs_type != MOUNT_UFS))
        return EIO;

    bzero(&nd, sizeof(nd));
    NDINIT_KERNEL(&nd, LOOKUP, FOLLOW, "/sbin/init");
    u.u_error = 0;
    ip = namei(&nd);
    if (ip == (struct inode *)0)
        return u.u_error != 0 ? u.u_error : ENOENT;
    i386_early_puts("vfs-namei-init: ok\n");
    if ((ip->i_mode & IFMT) != IFREG || ip->i_size <= 0 ||
        (unsigned long long)ip->i_size > sizeof(i386_vfs_init_data)) {
        iput(ip);
        return EFBIG;
    }
    residual = 0;
    error = rdwri(UIO_READ, ip, (caddr_t)i386_vfs_init_data,
        (int)ip->i_size, 0, 0, &residual);
    i386_vfs_init_size = (unsigned)ip->i_size;
    iput(ip);
    if (error != 0 || residual != 0) {
        i386_vfs_init_size = 0;
        i386_early_puts("vfs-read-init: failed\n");
        return error != 0 ? error : EIO;
    }
    i386_early_puts("vfs-read-init: ok\n");
    *data = i386_vfs_init_data;
    *size = i386_vfs_init_size;
    return 0;
}
