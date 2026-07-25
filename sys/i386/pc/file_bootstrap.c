#include "file_bootstrap.h"

#include <sys/param.h>
#include <sys/dir.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/inode.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/user.h>

static struct file *i386_file_last;

static void
i386_file_descriptor_release(int descriptor, struct file *fp)
{
    if (descriptor >= 0 && descriptor < NOFILE)
        u.u_ofile[descriptor] = (struct file *)0;
    if (fp != (struct file *)0)
        bzero(fp, sizeof(*fp));
    while (u.u_lastfile >= 0 &&
        u.u_ofile[u.u_lastfile] == (struct file *)0)
        --u.u_lastfile;
}

struct file *
falloc(void)
{
    struct file *fp;
    int descriptor;
    int index;

    descriptor = -1;
    for (index = 0; index < NOFILE; ++index) {
        if (u.u_ofile[index] == (struct file *)0) {
            descriptor = index;
            break;
        }
    }
    if (descriptor < 0) {
        u.u_error = EMFILE;
        return (struct file *)0;
    }

    if (i386_file_last == (struct file *)0)
        i386_file_last = file;
    for (fp = i386_file_last; fp < &file[NFILE]; ++fp)
        if (fp->f_count == 0)
            goto found;
    for (fp = file; fp < i386_file_last; ++fp)
        if (fp->f_count == 0)
            goto found;
    u.u_error = ENFILE;
    return (struct file *)0;

found:
    bzero(fp, sizeof(*fp));
    fp->f_count = 1;
    u.u_ofile[descriptor] = fp;
    u.u_pofile[descriptor] = 0;
    if (descriptor > u.u_lastfile)
        u.u_lastfile = descriptor;
    u.u_rval = descriptor;
    i386_file_last = fp + 1;
    if (i386_file_last == &file[NFILE])
        i386_file_last = file;
    return fp;
}

struct file *
getf(int descriptor)
{
    struct file *fp;

    if (descriptor < 0 || descriptor >= NOFILE ||
        (fp = u.u_ofile[descriptor]) == (struct file *)0) {
        u.u_error = EBADF;
        return (struct file *)0;
    }
    return fp;
}

int
closef(struct file *fp)
{
    struct inode *ip;

    if (fp == (struct file *)0)
        return 0;
    if (fp->f_count == 0)
        return EBADF;
    if (fp->f_count > 1) {
        --fp->f_count;
        return 0;
    }
    if (fp->f_type != DTYPE_INODE)
        return EOPNOTSUPP;
    ip = (struct inode *)fp->f_data;
    bzero(fp, sizeof(*fp));
    if (ip != (struct inode *)0)
        irele(ip);
    return 0;
}

void
open(void)
{
    struct nameidata nd;
    struct inode *ip;
    struct file *fp;
    int descriptor;
    int flags;
    int error;

    flags = u.u_arg[1];
    if ((flags & O_ACCMODE) != O_RDONLY ||
        (flags & (O_CREAT | O_TRUNC | O_APPEND)) != 0) {
        u.u_error = EROFS;
        return;
    }
    if ((flags & (O_SHLOCK | O_EXLOCK | O_ASYNC | O_FSYNC)) != 0) {
        u.u_error = EOPNOTSUPP;
        return;
    }
    if (rootdir == (struct inode *)0) {
        u.u_error = ENOENT;
        return;
    }

    fp = falloc();
    if (fp == (struct file *)0)
        return;
    descriptor = u.u_rval;
    bzero(&nd, sizeof(nd));
    NDINIT(&nd, LOOKUP, FOLLOW, (caddr_t)u.u_arg[0]);
    u.u_error = 0;
    ip = namei(&nd);
    if (ip == (struct inode *)0) {
        error = u.u_error != 0 ? u.u_error : ENOENT;
        i386_file_descriptor_release(descriptor, fp);
        u.u_error = error;
        return;
    }
    if (access(ip, IREAD) != 0) {
        error = u.u_error != 0 ? u.u_error : EACCES;
        iput(ip);
        i386_file_descriptor_release(descriptor, fp);
        u.u_error = error;
        return;
    }
    if ((ip->i_mode & IFMT) != IFREG &&
        (ip->i_mode & IFMT) != IFDIR) {
        iput(ip);
        i386_file_descriptor_release(descriptor, fp);
        u.u_error = EOPNOTSUPP;
        return;
    }

    IUNLOCK(ip);
    fp->f_flag = FREAD | (flags & FNONBLOCK);
    fp->f_type = DTYPE_INODE;
    fp->f_data = (caddr_t)ip;
    fp->f_offset = 0;
    u.u_rval = descriptor;
}

void
read(void)
{
    struct inode *ip;
    struct file *fp;
    unsigned count;
    int residual;
    int error;

    fp = getf(u.u_arg[0]);
    if (fp == (struct file *)0)
        return;
    if ((fp->f_flag & FREAD) == 0) {
        u.u_error = EBADF;
        return;
    }
    if (fp->f_type != DTYPE_INODE ||
        (ip = (struct inode *)fp->f_data) == (struct inode *)0) {
        u.u_error = EOPNOTSUPP;
        return;
    }
    if ((ip->i_mode & IFMT) == IFDIR) {
        u.u_error = EISDIR;
        return;
    }
    if ((ip->i_mode & IFMT) != IFREG) {
        u.u_error = EOPNOTSUPP;
        return;
    }
    count = (unsigned)u.u_arg[2];
    if (count > 0x7fffffffu) {
        u.u_error = EINVAL;
        return;
    }

    residual = (int)count;
    ILOCK(ip);
    error = rdwri(UIO_READ, ip, (caddr_t)u.u_arg[1], (int)count,
        fp->f_offset, 0, &residual);
    IUNLOCK(ip);
    if (residual < 0 || (unsigned)residual > count) {
        u.u_error = EIO;
        return;
    }
    fp->f_offset += (off_t)(count - (unsigned)residual);
    u.u_rval = (int)(count - (unsigned)residual);
    u.u_error = error;
}

void
close(void)
{
    struct file *fp;
    int descriptor;

    descriptor = u.u_arg[0];
    fp = getf(descriptor);
    if (fp == (struct file *)0)
        return;
    u.u_ofile[descriptor] = (struct file *)0;
    while (u.u_lastfile >= 0 &&
        u.u_ofile[u.u_lastfile] == (struct file *)0)
        --u.u_lastfile;
    u.u_error = closef(fp);
}

void
i386_file_bootstrap_close_all(void)
{
    struct file *fp;
    int descriptor;
    int error;

    error = 0;
    for (descriptor = 0; descriptor <= u.u_lastfile; ++descriptor) {
        fp = u.u_ofile[descriptor];
        if (fp == (struct file *)0)
            continue;
        u.u_ofile[descriptor] = (struct file *)0;
        u.u_pofile[descriptor] = 0;
        if (closef(fp) != 0)
            error = EIO;
    }
    u.u_lastfile = -1;
    if (error != 0)
        panic("i386 exit close");
}

static int
i386_lseek_add(off_t base, off_t delta, off_t *result)
{
    if ((delta > 0 && base > (off_t)0x7fffffffffffffffLL - delta) ||
        (delta < 0 && base < (off_t)(-0x7fffffffffffffffLL - 1) - delta))
        return EOVERFLOW;
    *result = base + delta;
    return 0;
}

void
lseek(void)
{
    struct inode *ip;
    struct file *fp;
    off_t offset;
    off_t result;

    fp = getf(u.u_arg[0]);
    if (fp == (struct file *)0)
        return;
    if (fp->f_type != DTYPE_INODE ||
        (ip = (struct inode *)fp->f_data) == (struct inode *)0) {
        u.u_error = ESPIPE;
        return;
    }
    offset = (off_t)(int)u.u_arg[1];
    result = 0;
    switch (u.u_arg[2]) {
    case L_SET:
        result = offset;
        break;
    case L_INCR:
        u.u_error = i386_lseek_add(fp->f_offset, offset, &result);
        break;
    case L_XTND:
        u.u_error = i386_lseek_add(ip->i_size, offset, &result);
        break;
    default:
        u.u_error = EINVAL;
        return;
    }
    if (u.u_error != 0)
        return;
    if (result < 0 || result > (off_t)0x7fffffff) {
        u.u_error = result < 0 ? EINVAL : EOVERFLOW;
        return;
    }
    fp->f_offset = result;
    u.u_rval = (int)result;
}

int
i386_file_bootstrap_validate_closed(void)
{
    int index;

    if (u.u_lastfile != -1)
        return EBUSY;
    for (index = 0; index < NOFILE; ++index)
        if (u.u_ofile[index] != (struct file *)0)
            return EBUSY;
    for (index = 0; index < NFILE; ++index)
        if (file[index].f_count != 0)
            return EBUSY;
    return 0;
}
