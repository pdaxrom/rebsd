/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/inode.h>
#include <sys/fs.h>
#include <sys/buf.h>
#include <sys/mount.h>
#include <sys/file.h>
#include <sys/namei.h>
#include <sys/conf.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/proc.h>

/*
 * Common code for mount and umount.
 * Check that the user's argument is a reasonable
 * thing on which to mount, otherwise return error.
 */
int
getmdev (dev_t *pdev, caddr_t fname)
{
    register dev_t dev;
    register struct inode *ip;
    struct  nameidata nd;
    register struct nameidata *ndp = &nd;

    if (!suser())
        return (u.u_error);
    NDINIT (ndp, LOOKUP, FOLLOW, fname);
    ip = namei(ndp);
    if (ip == NULL) {
        if (u.u_error == ENOENT)
            return (ENODEV); /* needs translation */
        return (u.u_error);
    }
    if ((ip->i_mode&IFMT) != IFBLK) {
        iput(ip);
        return (ENOTBLK);
    }
    dev = (dev_t)ip->i_rdev;
    iput(ip);
    if (major(dev) >= nblkdev)
        return (ENXIO);
    *pdev = dev;
    return (0);
}

static int
getcdev(dev_t *pdev, caddr_t fname)
{
    register dev_t dev;
    register struct inode *ip;
    struct  nameidata nd;
    register struct nameidata *ndp = &nd;

    if (!suser())
        return (u.u_error);
    NDINIT (ndp, LOOKUP, FOLLOW, fname);
    ip = namei(ndp);
    if (ip == NULL)
        return (u.u_error);
    if ((ip->i_mode&IFMT) != IFCHR) {
        iput(ip);
        return (ENODEV);
    }
    dev = (dev_t)ip->i_rdev;
    iput(ip);
    if (major(dev) >= nchrdev)
        return (ENXIO);
    *pdev = dev;
    return (0);
}

void
mount_updname (struct fs *fs, char *on, char *from, int lenon, int lenfrom)
{
    struct  mount   *mp;

    bzero (fs->fs_fsmnt, sizeof (fs->fs_fsmnt));
    bcopy (on, fs->fs_fsmnt, sizeof (fs->fs_fsmnt) - 1);
    mp = (struct mount*) ((int) fs - offsetof (struct mount, m_filsys));
    bzero (mp->m_mnton, sizeof (mp->m_mnton));
    bzero (mp->m_mntfrom, sizeof (mp->m_mntfrom));
    bcopy (on, mp->m_mnton, lenon);
    bcopy (from, mp->m_mntfrom, lenfrom);
}

void
smount()
{
    register struct a {
        char    *fspec;
        char    *freg;
        int flags;
    } *uap = (struct a *)u.u_arg;
    dev_t dev = 0;
    register struct inode *ip;
    register struct fs *fs;
    struct  nameidata nd;
    struct  nameidata *ndp = &nd;
    struct  mount   *mp;
    struct  vfsops  *ops;
    u_int lenon, lenfrom;
    int error = 0;
    int flags, fstype;
    char    mnton[MNAMELEN], mntfrom[MNAMELEN];

    flags = uap->flags;
    fstype = MNT_FSTYPE(flags);
    flags &= ~MNT_FSTYPE_MASK;
    if (fstype == MOUNT_NONE)
        fstype = MOUNT_UFS;
    if (fstype < MOUNT_UFS || fstype > MOUNT_MAXTYPE) {
        u.u_error = EINVAL;
        return;
    }
    ops = vfs_getops(fstype);
    if (ops == 0) {
        u.u_error = ENOSYS;
        return;
    }
    if ((ops->vfs_flags & VFSOPS_READ_ONLY) != 0)
        flags |= MNT_RDONLY;
    if ((ops->vfs_flags & VFSOPS_DEVICE_MASK) == VFSOPS_BLOCK_DEVICE)
        u.u_error = getmdev (&dev, uap->fspec);
    else if ((ops->vfs_flags & VFSOPS_DEVICE_MASK) == VFSOPS_CHAR_DEVICE)
        u.u_error = getcdev (&dev, uap->fspec);
    else
        u.u_error = EINVAL;
    if (u.u_error)
        return;

    NDINIT (ndp, LOOKUP, FOLLOW, uap->freg);
    if ((ip = namei(ndp)) == NULL)
        return;
    if ((ip->i_mode&IFMT) != IFDIR) {
        error = ENOTDIR;
        goto    cmnout;
    }
    copyinstr(uap->freg, mnton, sizeof(mnton) - 1, &lenon);
    copyinstr(uap->fspec, mntfrom, sizeof(mntfrom) - 1, &lenfrom);

    if (flags & MNT_UPDATE) {
        fs = ip->i_fs;
        mp = (struct mount *)
            ((int)fs - offsetof(struct mount, m_filsys));
        if (ip->i_number != ROOTINO) {
            error = EINVAL;     /* Not a mount point */
            goto cmnout;
        }
        /*
         * Check that the device passed in is the same one that is in the mount
         * table entry for this mount point.
         */
        if (dev != mp->m_dev) {
            error = EINVAL;     /* not right mount point */
            goto cmnout;
        }
        /*
         * This is where the RW to RO transformation would be done.  It is, for now,
         * too much work to port pages of code to do (besides which most
         * programs get very upset at having access yanked out from under them).
         */
        if (fs->fs_ronly == 0 && (flags & MNT_RDONLY)) {
            error = EPERM;      /* ! RW to RO updates */
            goto cmnout;
        }
        /*
         * However, going from RO to RW is easy.  Then merge in the new
         * flags (async, sync, nodev, etc) passed in from the program.
         */
        if (fs->fs_ronly && ((flags & MNT_RDONLY) == 0)) {
            fs->fs_ronly = 0;
            mp->m_flags &= ~MNT_RDONLY;
        }
#define _MF (MNT_NOSUID | MNT_NODEV | MNT_NOEXEC | MNT_ASYNC | MNT_SYNCHRONOUS | MNT_NOATIME)
        mp->m_flags &= ~_MF;
        mp->m_flags |= (flags & _MF);
#undef _MF
        iput(ip);
        u.u_error = 0;
        goto updname;
    } else {
        /*
         * This is where a new mount (not an update of an existing mount point) is
         * done.
         *
         * The directory being mounted on can have no other references AND can not
         * currently be a mount point.  Mount points have an inode number of (you
         * guessed it) ROOTINO which is 2.
         */
        if (ip->i_count != 1 || (ip->i_number == ROOTINO)) {
            error = EBUSY;
            goto cmnout;
        }
        if (fstype == MOUNT_UFS)
            fs = mountfs (dev, flags, ip);
        else
            fs = vfs_mountfs (fstype, dev, flags, ip);
        if (fs == 0)
            return;
    }
    /*
     * Lastly, both for new mounts and updates of existing mounts, update the
     * mounted-on and mounted-from fields.
     */
updname:
    mount_updname(fs, mnton, mntfrom, lenon, lenfrom);
    return;
cmnout:
    iput(ip);
    u.u_error = error;
}

struct fs *
vfs_mountfs(int fstype, dev_t dev, int flags, struct inode *ip)
{
    register struct mount *mp = 0;
    register struct fs *fs;
    register struct vfsops *ops;
    register int error;
    int needclose = 0;
    int openflags;

    ops = vfs_getops(fstype);
    if (ops == 0 || ops->vfs_mount == 0) {
        error = ENOSYS;
        goto out;
    }
    if ((ops->vfs_flags & VFSOPS_DEVICE_MASK) == VFSOPS_BLOCK_DEVICE) {
        openflags = FREAD;
        if ((ops->vfs_flags & VFSOPS_READ_ONLY) == 0 &&
            (flags & MNT_RDONLY) == 0)
            openflags |= FWRITE;
        error = (*bdevsw[major(dev)].d_open)(dev, openflags, S_IFBLK);
        if (error)
            goto out;
        needclose = 1;
    }
    for (mp = &mount[0]; mp < &mount[NMOUNT]; mp++)
        if (mp->m_inodp != 0 && dev == mp->m_dev && mp->m_ops != 0 &&
            (mp->m_ops->vfs_flags & VFSOPS_DEVICE_MASK) ==
            (ops->vfs_flags & VFSOPS_DEVICE_MASK)) {
            mp = 0;
            error = EBUSY;
            goto out;
        }
    for (mp = &mount[0]; mp < &mount[NMOUNT]; mp++)
        if (mp->m_inodp == 0)
            goto found;
    mp = 0;
    error = EMFILE;
    goto out;
found:
    mp->m_inodp = ip;
    mp->m_dev = dev;
    mp->m_type = fstype;
    mp->m_ops = ops;
    mp->m_data = 0;
    fs = &mp->m_filsys;
    bzero((caddr_t)fs, sizeof(*fs));
    fs->fs_ronly = (flags & MNT_RDONLY) != 0;
    fs->fs_flags = flags;

    error = (*ops->vfs_mount)(mp, dev, flags, ip);
    if (error)
        goto out;

    if (ip) {
        ip->i_flag |= IMOUNT;
        cacheinval(ip);
        IUNLOCK(ip);
    }
    return fs;
out:
    if (ip)
        iput(ip);
    if (mp) {
        mp->m_inodp = 0;
        mp->m_dev = 0;
        mp->m_type = MOUNT_NONE;
        mp->m_ops = 0;
        mp->m_data = 0;
    }
    if (needclose) {
        (*bdevsw[major(dev)].d_close)(dev, openflags, S_IFBLK);
        binval(dev);
    }
    u.u_error = error;
    return 0;
}

/*
 * Mount a filesystem on the given directory inode.
 *
 * this routine has races if running twice
 */
struct fs *
mountfs (dev_t dev, int flags, struct inode *ip)
{
    register struct mount *mp = 0;
    struct buf *tp = 0;
    register struct fs *fs;
    register int error;
    int ronly = flags & MNT_RDONLY;
    int needclose = 0;

    error = (*bdevsw[major(dev)].d_open) (dev,
        ronly ? FREAD : (FREAD | FWRITE), S_IFBLK);
    if (error)
        goto out;

    needclose = 1;
    tp = bread (dev, SUPERB);
    if (tp->b_flags & B_ERROR)
        goto out;
    for (mp = &mount[0]; mp < &mount[NMOUNT]; mp++)
        if (mp->m_inodp != 0 && dev == mp->m_dev) {
            mp = 0;
            error = EBUSY;
            needclose = 0;
            goto out;
        }
    for (mp = &mount[0]; mp < &mount[NMOUNT]; mp++)
        if (mp->m_inodp == 0)
            goto found;
    mp = 0;
    error = EMFILE;     /* needs translation */
    goto out;
found:
    mp->m_inodp = ip;   /* reserve slot */
    mp->m_dev = dev;
    mp->m_type = MOUNT_UFS;
    mp->m_ops = &ufs_vfsops;
    mp->m_data = 0;
    fs = &mp->m_filsys;
    bcopy (tp->b_addr, (caddr_t)fs, sizeof(struct fs));
    brelse (tp);
    tp = 0;
    if (fs->fs_magic1 != FSMAGIC1 || fs->fs_magic2 != FSMAGIC2) {
        error = EINVAL;
        goto out;
    }
    fs->fs_ronly = (ronly != 0);
    if (ronly == 0)
        fs->fs_fmod = 1;
    fs->fs_ilock = 0;
    fs->fs_flock = 0;
    fs->fs_nbehind = 0;
    fs->fs_lasti = 1;
    fs->fs_flags = flags;
    if (ip) {
        ip->i_flag |= IMOUNT;
        cacheinval(ip);
        IUNLOCK(ip);
    }
    return (fs);
out:
    if (error == 0)
        error = EIO;
    if (ip)
        iput(ip);
    if (mp) {
        mp->m_inodp = 0;
        mp->m_dev = 0;
        mp->m_type = MOUNT_NONE;
        mp->m_ops = 0;
        mp->m_data = 0;
    }
    if (tp)
        brelse(tp);
    if (needclose) {
        (*bdevsw[major(dev)].d_close)(dev,
            ronly? FREAD : FREAD|FWRITE, S_IFBLK);
        binval(dev);
    }
    u.u_error = error;
    return (0);
}

/*
 * Mount a filesystem as the process namespace root.  Root mounts do not
 * have a real covered inode, so reserve their mount-table slot with the
 * historical non-NULL sentinel after acquiring the root inode.
 */
int
vfs_mountroot(int fstype, dev_t dev, int flags, struct inode **rootp)
{
    struct mount *mp;
    struct inode *root;
    struct fs *fs;
    int error;
    int openflags;

    if (rootp == 0)
        return EINVAL;
    *rootp = 0;
    u.u_error = 0;
    if (fstype == MOUNT_UFS)
        fs = mountfs(dev, flags, 0);
    else
        fs = vfs_mountfs(fstype, dev, flags, 0);
    if (fs == 0)
        return u.u_error != 0 ? u.u_error : EIO;

    mp = (struct mount *)((char *)fs - offsetof(struct mount, m_filsys));
    root = iget(dev, fs, ROOTINO);
    if (root == 0) {
        error = u.u_error != 0 ? u.u_error : EIO;
        if (mp->m_ops != 0 && mp->m_ops->vfs_unmount != 0)
            (void)(*mp->m_ops->vfs_unmount)(mp);
        openflags = FREAD;
        if ((mp->m_ops == 0 ||
            (mp->m_ops->vfs_flags & VFSOPS_READ_ONLY) == 0) &&
            (flags & MNT_RDONLY) == 0)
            openflags |= FWRITE;
        (void)(*bdevsw[major(dev)].d_close)(dev, openflags, S_IFBLK);
        binval(dev);
        bzero(mp, sizeof(*mp));
        u.u_error = error;
        return error;
    }
    IUNLOCK(root);
    mp->m_inodp = (struct inode *)1;
    *rootp = root;
    return 0;
}

/*
 * Release an early root mount before any process has acquired cwd/root
 * references.  Refuse replacement once another live inode belongs to it.
 */
int
vfs_unmountroot(struct inode **rootp)
{
    struct mount *mp;
    struct inode *root;
    struct inode *ip;
    struct vfsops *ops;
    dev_t dev;
    int error;
    int openflags;

    if (rootp == 0 || (root = *rootp) == 0 || root->i_fs == 0)
        return EINVAL;
    mp = (struct mount *)((char *)root->i_fs -
        offsetof(struct mount, m_filsys));
    if (mp->m_inodp != (struct inode *)1 || root->i_number != ROOTINO ||
        root->i_count != 1)
        return EBUSY;
    for (ip = inode; ip < &inode[NINODE]; ++ip)
        if (ip != root && ip->i_dev == mp->m_dev && ip->i_count != 0)
            return EBUSY;

    dev = mp->m_dev;
    ops = mp->m_ops;
    openflags = FREAD;
    if ((ops == 0 || (ops->vfs_flags & VFSOPS_READ_ONLY) == 0) &&
        (mp->m_flags & MNT_RDONLY) == 0)
        openflags |= FWRITE;
    nchinval(dev);
    mp->m_inodp = 0;
    *rootp = 0;
    irele(root);
    if (iflush(dev) < 0)
        return EBUSY;
    error = 0;
    if (ops != 0 && ops->vfs_unmount != 0)
        error = (*ops->vfs_unmount)(mp);
    if (error == 0)
        error = (*bdevsw[major(dev)].d_close)(dev, openflags, S_IFBLK);
    binval(dev);
    bzero(mp, sizeof(*mp));
    return error;
}

static int
unmount1 (caddr_t fname)
{
    dev_t dev = 0;
    register struct mount *mp;
    register struct inode *ip;
    register int error;
    int aflag;

    error = getmdev(&dev, fname);
    if (error == ENOTBLK) {
        error = getcdev(&dev, fname);
        if (error)
            return (error);
    } else if (error)
        return (error);
    for (mp = &mount[0]; mp < &mount[NMOUNT]; mp++)
        if (mp->m_inodp != NULL && dev == mp->m_dev)
            goto found;
    return (EINVAL);
found:
    nchinval (dev); /* flush the name cache */
    aflag = mp->m_flags & MNT_ASYNC;
    mp->m_flags &= ~MNT_ASYNC;  /* Don't want async when unmounting */
    vfs_sync(mp);

    if (iflush(dev) < 0) {
        mp->m_flags |= aflag;
        return (EBUSY);
    }
    if (mp->m_ops != 0 && mp->m_ops->vfs_unmount != 0) {
        error = (*mp->m_ops->vfs_unmount)(mp);
        if (error)
            return (error);
    }
    ip = mp->m_inodp;
    ip->i_flag &= ~IMOUNT;
    irele(ip);
    if (mp->m_ops != 0 &&
        (mp->m_ops->vfs_flags & VFSOPS_DEVICE_MASK) ==
        VFSOPS_BLOCK_DEVICE) {
        (*bdevsw[major(dev)].d_close)(dev, 0, S_IFBLK);
        binval(dev);
    }
    mp->m_inodp = 0;
    mp->m_dev = 0;
    mp->m_type = MOUNT_NONE;
    mp->m_ops = 0;
    mp->m_data = 0;
    return (0);
}

void
umount()
{
    struct a {
        char    *fspec;
    } *uap = (struct a *)u.u_arg;

    u.u_error = unmount1 (uap->fspec);
}
