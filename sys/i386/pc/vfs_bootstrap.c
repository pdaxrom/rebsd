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
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/user.h>

#include <fs/fat/fat.h>
#include <machine/layout.h>

#define I386_VFS_INIT_MAX       (64u * 1024u)
#define I386_VFS_INOHSZ         16u
#define I386_VFS_INOHASH(d, i)  (((d) + (i)) & (I386_VFS_INOHSZ - 1u))

union i386_vfs_ihead {
    union i386_vfs_ihead *head[2];
    struct inode *chain[2];
};

static union i386_vfs_ihead i386_vfs_iheads[I386_VFS_INOHSZ];
static struct inode *i386_vfs_ifreeh;
static struct inode **i386_vfs_ifreet;
static unsigned char i386_vfs_init_data[I386_VFS_INIT_MAX];
static unsigned i386_vfs_init_size;
static int i386_vfs_initialized;
static int i386_vfs_mounted;
static dev_t i386_vfs_dev;

/*
 * i686 does not mount UFS yet.  The common inode/namei code uses the
 * address of this vector only to distinguish UFS from delegated VFS ops.
 */
struct vfsops ufs_vfsops;

void
i386_vfs_log(char *format, ...)
{
    (void)format;
}

static void
i386_vfs_bhinit(void)
{
    struct bufhd *head;
    unsigned index;

    for (head = bufhash, index = 0; index < BUFHSZ; ++index, ++head)
        head->b_forw = head->b_back = (struct buf *)head;
}

static void
i386_vfs_binit(void)
{
    struct buf *bp;
    caddr_t address;
    unsigned index;

    for (bp = bfreelist; bp < &bfreelist[BQUEUES]; ++bp)
        bp->b_forw = bp->b_back = bp->av_forw = bp->av_back = bp;
    address = bufdata;
    for (index = 0; index < NBUF; ++index, address += MAXBSIZE) {
        bp = &buf[index];
        bp->b_dev = NODEV;
        bp->b_bcount = 0;
        bp->b_addr = address;
        binshash(bp, &bfreelist[BQ_AGE]);
        bp->b_flags = B_BUSY | B_INVAL;
        brelse(bp);
    }
}

static void
i386_vfs_ihinit(void)
{
    union i386_vfs_ihead *head;
    struct inode *ip;
    unsigned index;

    for (head = i386_vfs_iheads, index = 0;
        index < I386_VFS_INOHSZ; ++index, ++head)
        head->head[0] = head->head[1] = head;
    ip = inode;
    i386_vfs_ifreeh = ip;
    i386_vfs_ifreet = &ip->i_freef;
    ip->i_freeb = &i386_vfs_ifreeh;
    ip->i_forw = ip->i_back = ip;
    for (index = 1; index < NINODE; ++index) {
        ++ip;
        ip->i_forw = ip->i_back = ip;
        *i386_vfs_ifreet = ip;
        ip->i_freeb = i386_vfs_ifreet;
        i386_vfs_ifreet = &ip->i_freef;
    }
    ip->i_freef = (struct inode *)0;
}

static void
i386_vfs_init(void)
{
    if (i386_vfs_initialized)
        return;
    i386_vfs_bhinit();
    i386_vfs_binit();
    i386_vfs_ihinit();
    nchinit();
    coutinit();
    fatattach(0);
    i386_vfs_initialized = 1;
}

struct inode *
ifind(dev_t dev, ino_t ino)
{
    union i386_vfs_ihead *head;
    struct inode *ip;

    head = &i386_vfs_iheads[I386_VFS_INOHASH(dev, ino)];
    for (ip = head->chain[0]; ip != (struct inode *)head; ip = ip->i_forw)
        if (ip->i_dev == dev && ip->i_number == ino)
            return ip;
    return (struct inode *)0;
}

void
igrab(struct inode *ip)
{
    struct inode *next;

    while ((ip->i_flag & ILOCKED) != 0) {
        ip->i_flag |= IWANT;
        sleep((caddr_t)ip, PINOD);
    }
    if (ip->i_count == 0) {
        next = ip->i_freef;
        if (next != (struct inode *)0)
            next->i_freeb = ip->i_freeb;
        else
            i386_vfs_ifreet = ip->i_freeb;
        *ip->i_freeb = next;
        ip->i_freef = (struct inode *)0;
        ip->i_freeb = (struct inode **)0;
    }
    ++ip->i_count;
    ip->i_flag |= ILOCKED;
}

struct inode *
iget(dev_t dev, struct fs *fs, ino_t ino)
{
    union i386_vfs_ihead *head;
    struct inode *ip;
    struct inode *next;
    struct mount *mp;
    int error;

retry:
    head = &i386_vfs_iheads[I386_VFS_INOHASH(dev, ino)];
    for (ip = head->chain[0]; ip != (struct inode *)head; ip = ip->i_forw) {
        if (ip->i_dev != dev || ip->i_number != ino)
            continue;
        if ((ip->i_flag & ILOCKED) != 0) {
            ip->i_flag |= IWANT;
            sleep((caddr_t)ip, PINOD);
            goto retry;
        }
        igrab(ip);
        return ip;
    }

    ip = i386_vfs_ifreeh;
    if (ip == (struct inode *)0) {
        u.u_error = ENFILE;
        return (struct inode *)0;
    }
    next = ip->i_freef;
    if (next != (struct inode *)0)
        next->i_freeb = &i386_vfs_ifreeh;
    else
        i386_vfs_ifreet = &i386_vfs_ifreeh;
    i386_vfs_ifreeh = next;
    ip->i_freef = (struct inode *)0;
    ip->i_freeb = (struct inode **)0;

    remque(ip);
    insque(ip, head);
    ip->i_dev = dev;
    ip->i_fs = fs;
    ip->i_number = ino;
    cacheinval(ip);
    ip->i_flag = ILOCKED;
    ip->i_count = 1;
    ip->i_lastr = 0;

    if (fs == (struct fs *)0) {
        error = EOPNOTSUPP;
        goto failed;
    }
    mp = (struct mount *)((int)fs - offsetof(struct mount, m_filsys));
    if (mp->m_ops == (struct vfsops *)0 ||
        mp->m_ops == &ufs_vfsops ||
        mp->m_ops->vfs_load_inode == 0) {
        error = EOPNOTSUPP;
        goto failed;
    }
    error = (*mp->m_ops->vfs_load_inode)(mp, ip);
    if (error == 0)
        return ip;

failed:
    remque(ip);
    ip->i_forw = ip->i_back = ip;
    ip->i_number = 0;
    iput(ip);
    u.u_error = error;
    return (struct inode *)0;
}

void
irele(struct inode *ip)
{
    if (ip == (struct inode *)0 || ip->i_count == 0)
        panic("i386 vfs irele");
    if (ip->i_count == 1) {
        ip->i_flag &= ~(ILOCKED | IWANT | IUPD | IACC | ICHG | IMOD);
        if (i386_vfs_ifreeh != (struct inode *)0) {
            *i386_vfs_ifreet = ip;
            ip->i_freeb = i386_vfs_ifreet;
        } else {
            i386_vfs_ifreeh = ip;
            ip->i_freeb = &i386_vfs_ifreeh;
        }
        ip->i_freef = (struct inode *)0;
        i386_vfs_ifreet = &ip->i_freef;
    }
    --ip->i_count;
}

void
iput(struct inode *ip)
{
    IUNLOCK(ip);
    irele(ip);
}

int
access(struct inode *ip, int mode)
{
    int wanted;
    gid_t *group;

    wanted = mode;
    if (wanted == IWRITE && ip->i_fs->fs_ronly != 0 &&
        (ip->i_mode & IFMT) != IFCHR && (ip->i_mode & IFMT) != IFBLK) {
        u.u_error = EROFS;
        return 1;
    }
    if (u.u_uid == 0)
        return 0;
    if (u.u_uid != ip->i_uid) {
        wanted >>= 3;
        for (group = u.u_groups;
            group < &u.u_groups[NGROUPS] && *group != NOGROUP; ++group)
            if (ip->i_gid == *group)
                goto found;
        wanted >>= 3;
    }
found:
    if ((ip->i_mode & wanted) != 0)
        return 0;
    u.u_error = EACCES;
    return 1;
}

daddr_t
bmap(struct inode *ip, daddr_t block, int rwflag, int flags)
{
    (void)ip;
    (void)block;
    (void)rwflag;
    (void)flags;
    u.u_error = EOPNOTSUPP;
    return (daddr_t)-1;
}

int
uiomove(caddr_t data, u_int count, struct uio *uio)
{
    struct iovec *iov;
    unsigned long address;
    u_int chunk;
    int error;

    while (count != 0 && uio->uio_resid != 0) {
        if (uio->uio_iovcnt <= 0)
            return EINVAL;
        iov = uio->uio_iov;
        if (iov->iov_len == 0) {
            ++uio->uio_iov;
            --uio->uio_iovcnt;
            continue;
        }
        chunk = iov->iov_len < count ? iov->iov_len : count;
        address = (unsigned long)iov->iov_base;
        if (address >= I386_USER_VADDR_START &&
            address < I386_USER_VADDR_END) {
            if (chunk > I386_USER_VADDR_END - address)
                return EFAULT;
            if (uio->uio_rw == UIO_READ)
                error = copyout(data, iov->iov_base, chunk);
            else
                error = copyin(iov->iov_base, data, chunk);
            if (error != 0)
                return error;
        } else if (uio->uio_rw == UIO_READ)
            bcopy(data, iov->iov_base, chunk);
        else
            bcopy(iov->iov_base, data, chunk);
        data += chunk;
        iov->iov_base += chunk;
        iov->iov_len -= chunk;
        uio->uio_offset += chunk;
        uio->uio_resid -= chunk;
        count -= chunk;
    }
    return 0;
}

int
rdwri(enum uio_rw rw, struct inode *ip, caddr_t base, int length,
    off_t offset, int ioflag, int *residual)
{
    struct mount *mp;
    struct iovec iov;
    struct uio uio;
    int error;

    if (length < 0 || ip == (struct inode *)0 || ip->i_fs == (struct fs *)0)
        return EINVAL;
    mp = (struct mount *)((int)ip->i_fs -
        offsetof(struct mount, m_filsys));
    if (mp->m_ops == (struct vfsops *)0 || mp->m_ops->vfs_rwip == 0)
        return EOPNOTSUPP;
    iov.iov_base = base;
    iov.iov_len = (size_t)length;
    uio.uio_iov = &iov;
    uio.uio_iovcnt = 1;
    uio.uio_offset = offset;
    uio.uio_resid = (u_int)length;
    uio.uio_rw = rw;
    error = (*mp->m_ops->vfs_rwip)(ip, &uio, ioflag);
    if (residual != (int *)0)
        *residual = (int)uio.uio_resid;
    else if (error == 0 && uio.uio_resid != 0)
        error = EIO;
    return error;
}

int
i386_vfs_bootstrap_mount(dev_t dev)
{
    struct mount *mp;
    struct inode *root;
    int error;

    if (i386_vfs_mounted)
        return dev == i386_vfs_dev ? 0 : EBUSY;
    i386_vfs_init();
    error = (*bdevsw[major(dev)].d_open)(dev, FREAD, 0);
    if (error != 0)
        return error;

    mp = &mount[0];
    bzero(mp, sizeof(*mp));
    mp->m_dev = dev;
    mp->m_type = MOUNT_FAT;
    mp->m_ops = &fat_vfsops;
    mp->m_flags = MNT_RDONLY | MNT_NOATIME;
    error = (*mp->m_ops->vfs_mount)(mp, dev, mp->m_flags,
        (struct inode *)0);
    if (error != 0)
        goto failed;
    root = iget(dev, &mp->m_filsys, ROOTINO);
    if (root == (struct inode *)0) {
        error = u.u_error != 0 ? u.u_error : EIO;
        (void)(*mp->m_ops->vfs_unmount)(mp);
        goto failed;
    }
    IUNLOCK(root);
    rootdir = root;
    i386_vfs_dev = dev;
    i386_vfs_mounted = 1;
    i386_early_puts("vfs-root: fat,read-only\n");
    return 0;

failed:
    bzero(mp, sizeof(*mp));
    (void)(*bdevsw[major(dev)].d_close)(dev, FREAD, 0);
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
