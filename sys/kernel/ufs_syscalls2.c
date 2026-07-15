/*
 * ufs_syscalls was getting too large.  Various UFS related system calls were
 * relocated to this file.
 */
#include <sys/param.h>
#include <sys/file.h>
#include <sys/user.h>
#include <sys/inode.h>
#include <sys/buf.h>
#include <sys/fs.h>
#include <sys/namei.h>
#include <sys/mount.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <stdint.h>

#ifdef ROMFS_ENABLED
extern struct vfsops mipsromfs_vfsops;
#endif
#ifdef FAT_ENABLED
extern struct vfsops fat_vfsops;
#endif

static int
ufs_statfs (struct mount *mp, struct statfs *sbp)
{
    struct  statfs  sfs;
    register struct statfs *sfsp;
    struct  fs  *fs = &mp->m_filsys;

    sfsp = &sfs;
    sfsp->f_type = mp->m_type ? mp->m_type : MOUNT_UFS;
    sfsp->f_bsize = MAXBSIZE;
    sfsp->f_iosize = MAXBSIZE;
    sfsp->f_blocks = fs->fs_fsize - fs->fs_isize;
    sfsp->f_bfree = fs->fs_tfree;
    sfsp->f_bavail = fs->fs_tfree;
    sfsp->f_files = (fs->fs_isize - 1) * INOPB;
    sfsp->f_ffree = fs->fs_tinode;

    bcopy (mp->m_mnton, sfsp->f_mntonname, MNAMELEN);
    bcopy (mp->m_mntfrom, sfsp->f_mntfromname, MNAMELEN);
    sfsp->f_flags = mp->m_flags & MNT_VISFLAGMASK;
    return copyout ((caddr_t) sfsp, (caddr_t) sbp, sizeof (struct statfs));
}

struct vfsops *
vfs_getops(int fstype)
{
    switch (fstype) {
    case MOUNT_UFS:
        return &ufs_vfsops;
#ifdef ROMFS_ENABLED
    case MOUNT_ROMFS:
        return &mipsromfs_vfsops;
#endif
#ifdef FAT_ENABLED
    case MOUNT_FAT:
        return &fat_vfsops;
#endif
    default:
        return 0;
    }
}

int
vfs_statfs(struct mount *mp, struct statfs *sbp)
{
    if (mp->m_ops == 0 || mp->m_ops->vfs_statfs == 0)
        return EINVAL;
    return (*mp->m_ops->vfs_statfs)(mp, sbp);
}

void
statfs()
{
    register struct a {
        char    *path;
        struct  statfs  *buf;
    } *uap = (struct a *)u.u_arg;
    register struct inode   *ip;
    struct  nameidata nd;
    register struct nameidata *ndp = &nd;
    struct  mount   *mp;

    NDINIT (ndp, LOOKUP, FOLLOW, uap->path);
    ip = namei(ndp);
    if (! ip)
        return;
    mp = (struct mount *)((int)ip->i_fs - offsetof(struct mount, m_filsys));
    iput(ip);
    u.u_error = vfs_statfs (mp, uap->buf);
}

void
fstatfs()
{
    register struct a {
        int     fd;
        struct  statfs *buf;
    } *uap = (struct a *)u.u_arg;
    register struct inode *ip;
    struct  mount *mp;

    ip = getinode(uap->fd);
    if (! ip)
        return;
    mp = (struct mount *)((int)ip->i_fs - offsetof(struct mount, m_filsys));
    u.u_error = vfs_statfs (mp, uap->buf);
}

void
getfsstat()
{
    register struct a {
        struct  statfs  *buf;
        int     bufsize;
        u_int   flags;
    } *uap = (struct a *)u.u_arg;
    register struct mount *mp;
    struct statfs *sfsp;
    int count, maxcount, error;

    maxcount = uap->bufsize / sizeof (struct statfs);
    sfsp = (struct statfs *)uap->buf;
    count = 0;
    for (mp = mount; mp < &mount[NMOUNT]; mp++) {
        if (mp->m_inodp == NULL)
            continue;
        if (count < maxcount) {
            error = vfs_statfs (mp, sfsp);
            if (error) {
                u.u_error = error;
                return;
            }
            sfsp++;
        }
        count++;
    }
    if (count > maxcount)
        u.u_rval = maxcount;
    else
        u.u_rval = count;
}

/*
 * This is somewhat inefficient in that the inode table is scanned for each
 * filesystem but it didn't seem worth a page or two of code on something
 * which only happens every 30 seconds.
 */
static void
syncinodes(struct fs *fs)
{
    register struct inode *ip;

    /*
     * Write back each (modified) inode.
     */
    for (ip = inode; ip < inode+NINODE; ip++) {
        /*
         * Attempt to reduce the overhead by short circuiting the scan if the
         * inode is not for the filesystem being processed.
         */
        if (ip->i_fs != fs)
            continue;
        if ((ip->i_flag & ILOCKED) != 0 || ip->i_count == 0 ||
               (ip->i_flag & (IMOD|IACC|IUPD|ICHG)) == 0)
            continue;
        ip->i_flag |= ILOCKED;
        ip->i_count++;
        iupdat(ip, &time, &time, 0);
        iput(ip);
    }
}

/*
 * 'ufs_sync' is the routine which syncs a single filesystem.  This was
 * created to replace 'update' which 'unmount' called.  It seemed silly to
 * sync _every_ filesystem when unmounting just one filesystem.
 */
int
ufs_sync(struct mount *mp)
{
    register struct fs *fs;
    struct  buf *bp;
    int error = 0;

    fs = &mp->m_filsys;
    if (fs->fs_fmod && (mp->m_flags & MNT_RDONLY)) {
        printf("fs = %s\n", fs->fs_fsmnt);
        panic("sync: rofs");
    }
    syncinodes(fs);     /* sync the inodes for this filesystem */
    bflush(mp->m_dev);  /* flush dirty data blocks */
    /*
     * And lastly the superblock, if the filesystem was modified.
     * Write back modified superblocks. Consistency check that the superblock
     * of each file system is still in the buffer cache.
     */
    if (fs->fs_fmod) {
        bp = getblk(mp->m_dev, SUPERB);
        fs->fs_fmod = 0;
        fs->fs_time = time.tv_sec;
        bcopy(fs, bp->b_addr, sizeof (struct fs));
        bwrite(bp);
        error = geterror(bp);
    }
    return(error);
}

struct vfsops ufs_vfsops = {
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    ufs_statfs,
    ufs_sync,
    0,
    VFSOPS_BLOCK_DEVICE,
};

int
vfs_sync(struct mount *mp)
{
    if (mp->m_ops == 0 || mp->m_ops->vfs_sync == 0)
        return 0;
    return (*mp->m_ops->vfs_sync)(mp);
}

/*
 * mode mask for creation of files
 */
void
umask()
{
    register struct a {
        int     mask;
    } *uap = (struct a *)u.u_arg;

    u.u_rval = u.u_cmask;
    u.u_cmask = uap->mask & 07777;
}

static int
lseek_add(off_t base, off_t delta, off_t *result)
{
    if ((delta > 0 && base > INT64_MAX - delta) ||
        (delta < 0 && base < INT64_MIN - delta))
        return EOVERFLOW;
    *result = base + delta;
    return 0;
}

static void
lseek1(int fd, off_t off, int sbase, int wide)
{
    register struct file *fp;
    off_t result;

    if ((fp = getf(fd)) == NULL)
        return;
    if (fp->f_type != DTYPE_INODE) {
        u.u_error = ESPIPE;
        return;
    }
    switch (sbase) {

    case L_INCR:
        u.u_error = lseek_add(fp->f_offset, off, &result);
        break;
    case L_XTND:
        u.u_error = lseek_add(((struct inode *)fp->f_data)->i_size,
            off, &result);
        break;
    case L_SET:
        result = off;
        break;
    default:
        u.u_error = EINVAL;
        return;
    }
    if (u.u_error)
        return;
    if (result < 0) {
        u.u_error = EINVAL;
        return;
    }
    if (!wide && result > INT32_MAX) {
        u.u_error = EOVERFLOW;
        return;
    }
    fp->f_offset = result;
    if (wide)
        syscall_off64_result(result);
    else
        u.u_rval = (int)result;
}

/* Original 32-bit ABI retained for old binaries. */
void
lseek()
{
    lseek1(u.u_arg[0], (int32_t)u.u_arg[1], u.u_arg[2], 0);
}

void
lseek64()
{
    lseek1(u.u_arg[0], syscall_off64_arg(&u.u_arg[1]), u.u_arg[3], 1);
}

/*
 * Synch an open file.
 */
void
fsync()
{
    register struct a {
        int     fd;
    } *uap = (struct a *)u.u_arg;
    register struct inode *ip;

    if ((ip = getinode(uap->fd)) == NULL)
        return;
    ilock(ip);
    syncip(ip);
    iunlock(ip);
}

void
utimes()
{
    register struct a {
        char    *fname;
        struct  timeval *tptr;
    } *uap = (struct a *)u.u_arg;
    register struct inode *ip;
    struct  nameidata nd;
    register struct nameidata *ndp = &nd;
    struct timeval tv[2];
    struct vattr vattr;

    VATTR_NULL(&vattr);
    if (uap->tptr == NULL) {
        tv[0].tv_sec = tv[1].tv_sec = time.tv_sec;
        vattr.va_vaflags |= VA_UTIMES_NULL;
    } else {
        u.u_error = copyin ((caddr_t)uap->tptr,(caddr_t)tv,sizeof(tv));
        if (u.u_error)
            return;
    }
    NDINIT (ndp, LOOKUP, FOLLOW, uap->fname);
    if ((ip = namei(ndp)) == NULL)
        return;
    vattr.va_atime = tv[0].tv_sec;
    vattr.va_mtime = tv[1].tv_sec;
    u.u_error = ufs_setattr(ip, &vattr);
    iput(ip);
}
