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

static int i386_vfs_initialized;
static int i386_vfs_mounted;
static dev_t i386_vfs_dev;

static void
i386_vfs_init(void)
{
    if (i386_vfs_initialized)
        return;
    bioinit();
    ihinit();
    nchinit();
    coutinit();
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
i386_vfs_select_root(dev_t dev)
{
    int error;

    error = vfs_mountroot(MOUNT_UFS, dev, MNT_RDONLY | MNT_NOATIME,
        &rootdir);
    if (error != 0)
        return error;
    error = i386_vfs_has_init();
    if (error != 0) {
        int unmount_error;

        unmount_error = vfs_unmountroot(&rootdir);
        return unmount_error != 0 ? unmount_error : error;
    }
    i386_vfs_dev = dev;
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
        error = i386_vfs_select_root(preferred_dev);
        if (error == 0) {
            i386_early_puts("vfs-root: ufs,ide,read-only\n");
            goto selected;
        }
        if (error == ENOENT || error == ENOEXEC)
            i386_early_puts("vfs-root: ufs,ide,no-init\n");
        else if (error == EINVAL || error == EOPNOTSUPP || error == ENXIO)
            i386_early_puts("vfs-root: ufs,ide,unavailable\n");
        else {
            i386_early_puts("vfs-root: ufs,ide,failed\n");
            return error;
        }
    }

    error = i386_vfs_select_root(fallback_dev);
    if (error != 0) {
        i386_early_puts("vfs-root: ufs,memory,failed\n");
        return error;
    }
    i386_early_puts("vfs-root: ufs,memory,read-only\n");

selected:
    igrab(rootdir);
    IUNLOCK(rootdir);
    u.u_cdir = rootdir;
    return 0;
}
