#include "boot.h"
#include "vfs_bootstrap.h"

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/dir.h>
#include <sys/errno.h>
#include <sys/fs.h>
#include <sys/inode.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/proc.h>
#include <sys/systm.h>
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

int
i386_vfs_bootstrap_mount(dev_t dev)
{
    int error;

    if (i386_vfs_mounted)
        return dev == i386_vfs_dev ? 0 : EBUSY;
    i386_vfs_init();

    error = vfs_mountroot(MOUNT_UFS, dev,
        MNT_RDONLY | MNT_NOATIME, &rootdir);
    if (error != 0) {
        i386_early_puts("vfs-root: ufs,romdisk,failed\n");
        return error;
    }
    i386_vfs_dev = dev;
    rootdev = dev;
    i386_vfs_mounted = 1;
    i386_early_puts("vfs-root: ufs,romdisk,read-only\n");

    igrab(rootdir);
    IUNLOCK(rootdir);
    u.u_cdir = rootdir;
    return 0;
}
