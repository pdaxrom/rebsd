/*
 * Copyright (c) 2026 The ReBSD Project.
 * All rights reserved.
 *
 * POSIX named pipes.  FIFO data is an in-core kernel object; only the
 * inode name, ownership and permissions are stored in the filesystem.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/inode.h>
#include <sys/file.h>
#include <sys/fs.h>
#include <sys/uio.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

struct fifo_info {
    int             fi_inuse;
    u_int           fi_readers;
    u_int           fi_writers;
    u_int           fi_head;
    u_int           fi_tail;
    u_int           fi_count;
    char            fi_buffer[MAXPIPSIZ];
};

static struct fifo_info fifo_table[NFIFO];

static struct fifo_info *
fifo_alloc(void)
{
    struct fifo_info *fi;
    int i, s;

    s = splhigh();
    for (i = 0; i < NFIFO; i++) {
        fi = &fifo_table[i];
        if (fi->fi_inuse == 0) {
            fi->fi_inuse = 1;
            fi->fi_readers = 0;
            fi->fi_writers = 0;
            fi->fi_head = 0;
            fi->fi_tail = 0;
            fi->fi_count = 0;
            splx(s);
            return fi;
        }
    }
    splx(s);
    return NULL;
}

static void
fifo_free(struct fifo_info *fi)
{
    int s;

    s = splhigh();
    fi->fi_inuse = 0;
    splx(s);
}

static void
fifo_wake_readers(struct inode *ip, struct fifo_info *fi)
{
    wakeup((caddr_t)&fi->fi_count);
    if (ip->i_rsel != NULL) {
        selwakeup(ip->i_rsel, (long)(ip->i_flag & IRCOLL));
        ip->i_rsel = NULL;
        ip->i_flag &= ~IRCOLL;
    }
}

static void
fifo_wake_writers(struct inode *ip, struct fifo_info *fi)
{
    wakeup((caddr_t)&fi->fi_tail);
    if (ip->i_wsel != NULL) {
        selwakeup(ip->i_wsel, (long)(ip->i_flag & IWCOLL));
        ip->i_wsel = NULL;
        ip->i_flag &= ~IWCOLL;
    }
}

static void
fifo_wake_openers(struct fifo_info *fi)
{
    wakeup((caddr_t)&fi->fi_readers);
    wakeup((caddr_t)&fi->fi_writers);
}

int
fifo_open(struct inode *ip, int mode)
{
    struct fifo_info *fi;
    int error;

    ILOCK(ip);
    fi = ip->i_fifo;
    if (fi == NULL) {
        fi = fifo_alloc();
        if (fi == NULL) {
            IUNLOCK(ip);
            return ENFILE;
        }
        ip->i_fifo = fi;
    }

    if ((mode & (FREAD | FWRITE)) == 0) {
        error = EINVAL;
        goto fail;
    }
    if ((mode & FWRITE) && !(mode & FREAD) &&
        (mode & FNONBLOCK) && fi->fi_readers == 0) {
        error = ENXIO;
        goto fail;
    }

    if (mode & FREAD)
        fi->fi_readers++;
    if (mode & FWRITE)
        fi->fi_writers++;
    fifo_wake_openers(fi);

    if ((mode & FREAD) && !(mode & FWRITE) && !(mode & FNONBLOCK)) {
        while (fi->fi_writers == 0) {
            IUNLOCK(ip);
            error = tsleep((caddr_t)&fi->fi_writers, PPIPE | PCATCH, 0);
            ILOCK(ip);
            if (error != 0)
                goto rollback;
        }
    }
    if ((mode & FWRITE) && !(mode & FREAD) && !(mode & FNONBLOCK)) {
        while (fi->fi_readers == 0) {
            IUNLOCK(ip);
            error = tsleep((caddr_t)&fi->fi_readers, PPIPE | PCATCH, 0);
            ILOCK(ip);
            if (error != 0)
                goto rollback;
        }
    }
    IUNLOCK(ip);
    return 0;

rollback:
    if (mode & FREAD)
        fi->fi_readers--;
    if (mode & FWRITE)
        fi->fi_writers--;
    fifo_wake_openers(fi);
fail:
    if (fi->fi_readers == 0 && fi->fi_writers == 0) {
        ip->i_fifo = NULL;
        fifo_free(fi);
    }
    IUNLOCK(ip);
    return error;
}

static int
fifo_read_move(struct fifo_info *fi, struct uio *uio, u_int amount)
{
    u_int before, moved, part;
    int error;

    while (amount != 0) {
        part = MIN(amount, (u_int)MAXPIPSIZ - fi->fi_head);
        before = uio->uio_resid;
        error = uiomove((caddr_t)&fi->fi_buffer[fi->fi_head], part, uio);
        moved = before - uio->uio_resid;
        fi->fi_head = (fi->fi_head + moved) % MAXPIPSIZ;
        fi->fi_count -= moved;
        amount -= moved;
        if (error != 0 || moved != part)
            return error != 0 ? error : EFAULT;
    }
    return 0;
}

static int
fifo_write_move(struct fifo_info *fi, struct uio *uio, u_int amount)
{
    u_int before, moved, part;
    int error;

    while (amount != 0) {
        part = MIN(amount, (u_int)MAXPIPSIZ - fi->fi_tail);
        before = uio->uio_resid;
        error = uiomove((caddr_t)&fi->fi_buffer[fi->fi_tail], part, uio);
        moved = before - uio->uio_resid;
        fi->fi_tail = (fi->fi_tail + moved) % MAXPIPSIZ;
        fi->fi_count += moved;
        amount -= moved;
        if (error != 0 || moved != part)
            return error != 0 ? error : EFAULT;
    }
    return 0;
}

static int
fifo_read(struct file *fp, struct uio *uio)
{
    struct inode *ip;
    struct fifo_info *fi;
    u_int initial, amount;
    int error;

    ip = (struct inode *)fp->f_data;
    initial = uio->uio_resid;
    ILOCK(ip);
    fi = ip->i_fifo;
    if (fi == NULL) {
        IUNLOCK(ip);
        return EIO;
    }
    while (fi->fi_count == 0) {
        if (fi->fi_writers == 0) {
            IUNLOCK(ip);
            return 0;
        }
        if (fp->f_flag & FNONBLOCK) {
            IUNLOCK(ip);
            return EWOULDBLOCK;
        }
        IUNLOCK(ip);
        error = tsleep((caddr_t)&fi->fi_count, PPIPE | PCATCH, 0);
        if (error != 0)
            return error;
        ILOCK(ip);
    }
    amount = MIN(uio->uio_resid, fi->fi_count);
    error = fifo_read_move(fi, uio, amount);
    if (uio->uio_resid != initial) {
        ip->i_flag |= IACC;
        fifo_wake_writers(ip, fi);
        if (error != 0)
            error = 0;
    }
    IUNLOCK(ip);
    return error;
}

static int
fifo_write(struct file *fp, struct uio *uio)
{
    struct inode *ip;
    struct fifo_info *fi;
    u_int initial, atomic, available, amount;
    int error;

    ip = (struct inode *)fp->f_data;
    initial = uio->uio_resid;
    /* Userland PIPE_BUF is defined to the same MAXPIPSIZ value. */
    atomic = initial <= MAXPIPSIZ;
    error = 0;
    ILOCK(ip);
    fi = ip->i_fifo;
    if (fi == NULL) {
        IUNLOCK(ip);
        return EIO;
    }

    while (uio->uio_resid != 0) {
        if (fi->fi_readers == 0) {
            psignal(u.u_procp, SIGPIPE);
            error = EPIPE;
            break;
        }
        available = MAXPIPSIZ - fi->fi_count;
        if (available == 0 || (atomic && available < uio->uio_resid)) {
            if (fp->f_flag & FNONBLOCK) {
                error = EWOULDBLOCK;
                break;
            }
            IUNLOCK(ip);
            error = tsleep((caddr_t)&fi->fi_tail, PPIPE | PCATCH, 0);
            if (error != 0) {
                if (uio->uio_resid != initial)
                    return 0;
                return error;
            }
            ILOCK(ip);
            continue;
        }
        amount = atomic ? uio->uio_resid : MIN(uio->uio_resid, available);
        error = fifo_write_move(fi, uio, amount);
        if (amount != 0) {
            ip->i_flag |= IUPD | ICHG;
            fifo_wake_readers(ip, fi);
        }
        if (error != 0)
            break;
    }
    if (uio->uio_resid != initial && error != 0)
        error = 0;
    IUNLOCK(ip);
    return error;
}

static int
fifo_rw(struct file *fp, struct uio *uio)
{
    if (uio->uio_rw == UIO_READ)
        return fifo_read(fp, uio);
    return fifo_write(fp, uio);
}

static int
fifo_ioctl(struct file *fp, u_int com, char *data)
{
    struct inode *ip;
    struct fifo_info *fi;

    if (com == FIONBIO || com == FIOASYNC)
        return 0;
    if (com != FIONREAD)
        return ENOTTY;
    ip = (struct inode *)fp->f_data;
    ILOCK(ip);
    fi = ip->i_fifo;
    *(long *)data = fi != NULL ? (long)fi->fi_count : 0;
    IUNLOCK(ip);
    return 0;
}

static int
fifo_select(struct file *fp, int which)
{
    struct inode *ip;
    struct fifo_info *fi;
    struct proc *p;
    int ready;
    extern int selwait;

    ip = (struct inode *)fp->f_data;
    ready = 0;
    ILOCK(ip);
    fi = ip->i_fifo;
    if (fi == NULL)
        ready = 1;
    else if (which == FREAD) {
        if (fi->fi_count != 0 || fi->fi_writers == 0)
            ready = 1;
        else if ((p = ip->i_rsel) != NULL &&
            p->p_wchan == (caddr_t)&selwait)
            ip->i_flag |= IRCOLL;
        else
            ip->i_rsel = u.u_procp;
    } else if (which == FWRITE) {
        if (fi->fi_readers == 0 || fi->fi_count < MAXPIPSIZ)
            ready = 1;
        else if ((p = ip->i_wsel) != NULL &&
            p->p_wchan == (caddr_t)&selwait)
            ip->i_flag |= IWCOLL;
        else
            ip->i_wsel = u.u_procp;
    }
    IUNLOCK(ip);
    return ready;
}

static int
fifo_close(struct file *fp)
{
    struct inode *ip;
    struct fifo_info *fi;

    ip = (struct inode *)fp->f_data;
    ILOCK(ip);
    fi = ip->i_fifo;
    if (fi != NULL) {
        if ((fp->f_flag & FREAD) && fi->fi_readers != 0)
            fi->fi_readers--;
        if ((fp->f_flag & FWRITE) && fi->fi_writers != 0)
            fi->fi_writers--;
        fifo_wake_openers(fi);
        fifo_wake_readers(ip, fi);
        fifo_wake_writers(ip, fi);
        if (fi->fi_readers == 0 && fi->fi_writers == 0) {
            ip->i_fifo = NULL;
            fifo_free(fi);
        }
    }
    IUNLOCK(ip);
    fp->f_data = NULL;
    irele(ip);
    return 0;
}

int
fifo_stat(struct file *fp, struct stat *sb)
{
    struct inode *ip;
    struct fifo_info *fi;
    int error;

    ip = (struct inode *)fp->f_data;
    ILOCK(ip);
    error = ino_stat(ip, sb);
    fi = ip->i_fifo;
    sb->st_size = fi != NULL ? fi->fi_count : 0;
    sb->st_blocks = 0;
    IUNLOCK(ip);
    return error;
}

const struct fileops fifoops = {
    fifo_rw, fifo_ioctl, fifo_select, fifo_close
};
