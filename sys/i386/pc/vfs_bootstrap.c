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

void
i386_vfs_log(char *format, ...)
{
    (void)format;
}

void
i386_vfs_syslog(int level, char *format, ...)
{
    (void)level;
    (void)format;
}

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

int
i386_vfs_bootstrap_mount(dev_t dev)
{
    struct fs *fs;
    struct mount *mp;
    struct inode *root;
    int error;

    if (i386_vfs_mounted)
        return dev == i386_vfs_dev ? 0 : EBUSY;
    i386_vfs_init();
    u.u_error = 0;
    fs = vfs_mountfs(MOUNT_FAT, dev, MNT_RDONLY | MNT_NOATIME,
        (struct inode *)0);
    if (fs == (struct fs *)0) {
        error = u.u_error != 0 ? u.u_error : EIO;
        goto failed;
    }
    mp = (struct mount *)((int)fs - offsetof(struct mount, m_filsys));
    root = iget(dev, fs, ROOTINO);
    if (root == (struct inode *)0) {
        error = u.u_error != 0 ? u.u_error : EIO;
        (void)(*mp->m_ops->vfs_unmount)(mp);
        (void)(*bdevsw[major(dev)].d_close)(dev, FREAD, S_IFBLK);
        bzero(mp, sizeof(*mp));
        goto failed_log;
    }
    IUNLOCK(root);
    rootdir = root;
    i386_vfs_dev = dev;
    i386_vfs_mounted = 1;
    i386_early_puts("vfs-root: fat,read-only\n");
    return 0;

failed:
failed_log:
    if (error == EINVAL || error == EOPNOTSUPP) {
        i386_early_puts("vfs-root: unavailable\n");
        return 0;
    }
    i386_early_puts("vfs-root: failed\n");
    return error;
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
