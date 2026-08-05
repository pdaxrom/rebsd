/*
 * Copyright (c) 2026 The ReBSD Project.
 * All rights reserved.
 *
 * Anonymous pipes are in-core kernel objects.  They do not allocate an inode
 * or use a mounted filesystem, so pipe(2) is available before userland has
 * configured any writable block device.
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

struct pipe_info {
    int             pi_inuse;
    int             pi_locked;
    int             pi_wanted;
    u_int           pi_readers;
    u_int           pi_writers;
    u_int           pi_head;
    u_int           pi_tail;
    u_int           pi_count;
    uid_t           pi_uid;
    gid_t           pi_gid;
    struct proc     *pi_rsel;
    struct proc     *pi_wsel;
    int             pi_rscoll;
    int             pi_wscoll;
    char            pi_buffer[MAXPIPSIZ];
};

static struct pipe_info pipe_table[NPIPE];

static void
pipe_lock(struct pipe_info *pi)
{
    while (pi->pi_locked) {
        pi->pi_wanted = 1;
        sleep((caddr_t)pi, PPIPE);
    }
    pi->pi_locked = 1;
}

static void
pipe_unlock(struct pipe_info *pi)
{
    pi->pi_locked = 0;
    if (pi->pi_wanted) {
        pi->pi_wanted = 0;
        wakeup((caddr_t)pi);
    }
}

static struct pipe_info *
pipe_alloc(void)
{
    struct pipe_info *pi;
    int i, s;

    s = splhigh();
    for (i = 0; i < NPIPE; ++i) {
        pi = &pipe_table[i];
        if (!pi->pi_inuse) {
            pi->pi_inuse = 1;
            pi->pi_locked = 0;
            pi->pi_wanted = 0;
            pi->pi_readers = 1;
            pi->pi_writers = 1;
            pi->pi_head = 0;
            pi->pi_tail = 0;
            pi->pi_count = 0;
            pi->pi_uid = u.u_uid;
            pi->pi_gid = u.u_groups[0];
            pi->pi_rsel = NULL;
            pi->pi_wsel = NULL;
            pi->pi_rscoll = 0;
            pi->pi_wscoll = 0;
            splx(s);
            return pi;
        }
    }
    splx(s);
    return NULL;
}

static void
pipe_free(struct pipe_info *pi)
{
    int s;

    s = splhigh();
    pi->pi_inuse = 0;
    splx(s);
}

static void
pipe_wake_readers(struct pipe_info *pi)
{
    wakeup((caddr_t)&pi->pi_count);
    if (pi->pi_rsel != NULL) {
        selwakeup(pi->pi_rsel, pi->pi_rscoll);
        pi->pi_rsel = NULL;
        pi->pi_rscoll = 0;
    }
}

static void
pipe_wake_writers(struct pipe_info *pi)
{
    wakeup((caddr_t)&pi->pi_tail);
    if (pi->pi_wsel != NULL) {
        selwakeup(pi->pi_wsel, pi->pi_wscoll);
        pi->pi_wsel = NULL;
        pi->pi_wscoll = 0;
    }
}

static int
pipe_read_move(struct pipe_info *pi, struct uio *uio, u_int amount)
{
    u_int before, moved, part;
    int error;

    while (amount != 0) {
        part = MIN(amount, (u_int)MAXPIPSIZ - pi->pi_head);
        before = uio->uio_resid;
        error = uiomove((caddr_t)&pi->pi_buffer[pi->pi_head], part, uio);
        moved = before - uio->uio_resid;
        pi->pi_head = (pi->pi_head + moved) % MAXPIPSIZ;
        pi->pi_count -= moved;
        amount -= moved;
        if (error != 0 || moved != part)
            return error != 0 ? error : EFAULT;
    }
    return 0;
}

static int
pipe_write_move(struct pipe_info *pi, struct uio *uio, u_int amount)
{
    u_int before, moved, part;
    int error;

    while (amount != 0) {
        part = MIN(amount, (u_int)MAXPIPSIZ - pi->pi_tail);
        before = uio->uio_resid;
        error = uiomove((caddr_t)&pi->pi_buffer[pi->pi_tail], part, uio);
        moved = before - uio->uio_resid;
        pi->pi_tail = (pi->pi_tail + moved) % MAXPIPSIZ;
        pi->pi_count += moved;
        amount -= moved;
        if (error != 0 || moved != part)
            return error != 0 ? error : EFAULT;
    }
    return 0;
}

static int
pipe_read(struct file *fp, struct uio *uio)
{
    struct pipe_info *pi;
    u_int initial, amount;
    int error;

    pi = (struct pipe_info *)fp->f_data;
    initial = uio->uio_resid;
    pipe_lock(pi);
    while (pi->pi_count == 0) {
        if (pi->pi_writers == 0) {
            pipe_unlock(pi);
            return 0;
        }
        if (fp->f_flag & FNONBLOCK) {
            pipe_unlock(pi);
            return EWOULDBLOCK;
        }
        pipe_unlock(pi);
        error = tsleep((caddr_t)&pi->pi_count, PPIPE | PCATCH, 0);
        if (error != 0)
            return error;
        pipe_lock(pi);
    }
    amount = MIN(uio->uio_resid, pi->pi_count);
    error = pipe_read_move(pi, uio, amount);
    if (uio->uio_resid != initial) {
        pipe_wake_writers(pi);
        if (error != 0)
            error = 0;
    }
    pipe_unlock(pi);
    return error;
}

static int
pipe_write(struct file *fp, struct uio *uio)
{
    struct pipe_info *pi;
    u_int initial, atomic, available, amount;
    int error;

    pi = (struct pipe_info *)fp->f_data;
    initial = uio->uio_resid;
    atomic = initial <= MAXPIPSIZ;
    error = 0;
    pipe_lock(pi);
    while (uio->uio_resid != 0) {
        if (pi->pi_readers == 0) {
            psignal(u.u_procp, SIGPIPE);
            error = EPIPE;
            break;
        }
        available = MAXPIPSIZ - pi->pi_count;
        if (available == 0 || (atomic && available < uio->uio_resid)) {
            if (fp->f_flag & FNONBLOCK) {
                error = EWOULDBLOCK;
                break;
            }
            pipe_unlock(pi);
            error = tsleep((caddr_t)&pi->pi_tail, PPIPE | PCATCH, 0);
            if (error != 0) {
                if (uio->uio_resid != initial)
                    return 0;
                return error;
            }
            pipe_lock(pi);
            continue;
        }
        amount = atomic ? uio->uio_resid : MIN(uio->uio_resid, available);
        error = pipe_write_move(pi, uio, amount);
        if (amount != 0)
            pipe_wake_readers(pi);
        if (error != 0)
            break;
    }
    if (uio->uio_resid != initial && error != 0)
        error = 0;
    pipe_unlock(pi);
    return error;
}

static int
pipe_rw(struct file *fp, struct uio *uio)
{
    if (uio->uio_rw == UIO_READ)
        return pipe_read(fp, uio);
    return pipe_write(fp, uio);
}

static int
pipe_ioctl(struct file *fp, u_int com, char *data)
{
    struct pipe_info *pi;

    if (com == FIONBIO || com == FIOASYNC)
        return 0;
    if (com != FIONREAD)
        return ENOTTY;
    pi = (struct pipe_info *)fp->f_data;
    pipe_lock(pi);
    *(long *)data = (long)pi->pi_count;
    pipe_unlock(pi);
    return 0;
}

static int
pipe_select(struct file *fp, int which)
{
    struct pipe_info *pi;
    struct proc *p;
    int ready;
    extern int selwait;

    pi = (struct pipe_info *)fp->f_data;
    ready = 0;
    pipe_lock(pi);
    if (which == FREAD) {
        if (pi->pi_count != 0 || pi->pi_writers == 0)
            ready = 1;
        else if ((p = pi->pi_rsel) != NULL &&
            p->p_wchan == (caddr_t)&selwait)
            pi->pi_rscoll = 1;
        else
            pi->pi_rsel = u.u_procp;
    } else if (which == FWRITE) {
        if (pi->pi_readers == 0 || pi->pi_count < MAXPIPSIZ)
            ready = 1;
        else if ((p = pi->pi_wsel) != NULL &&
            p->p_wchan == (caddr_t)&selwait)
            pi->pi_wscoll = 1;
        else
            pi->pi_wsel = u.u_procp;
    }
    pipe_unlock(pi);
    return ready;
}

static int
pipe_close(struct file *fp)
{
    struct pipe_info *pi;

    pi = (struct pipe_info *)fp->f_data;
    pipe_lock(pi);
    if ((fp->f_flag & FREAD) && pi->pi_readers != 0)
        pi->pi_readers--;
    if ((fp->f_flag & FWRITE) && pi->pi_writers != 0)
        pi->pi_writers--;
    pipe_wake_readers(pi);
    pipe_wake_writers(pi);
    if (pi->pi_readers == 0 && pi->pi_writers == 0) {
        pipe_unlock(pi);
        pipe_free(pi);
    } else
        pipe_unlock(pi);
    fp->f_data = NULL;
    return 0;
}

int
pipe_stat(struct file *fp, struct stat *sb)
{
    struct pipe_info *pi;

    pi = (struct pipe_info *)fp->f_data;
    pipe_lock(pi);
    sb->st_mode = S_IFIFO | S_IRUSR | S_IWUSR;
    sb->st_nlink = 1;
    sb->st_uid = pi->pi_uid;
    sb->st_gid = pi->pi_gid;
    sb->st_size = pi->pi_count;
    sb->st_blksize = MAXPIPSIZ;
    sb->st_blocks = 0;
    pipe_unlock(pi);
    return 0;
}

const struct fileops pipeops = {
    pipe_rw, pipe_ioctl, pipe_select, pipe_close
};

/*
 * Create one read endpoint and one write endpoint.  The secondary syscall
 * result register carries the write descriptor on both supported ABIs.
 */
void
pipe(void)
{
    struct pipe_info *pi;
    struct file *rf, *wf;
    int r;

    pi = pipe_alloc();
    if (pi == NULL) {
        u.u_error = ENFILE;
        return;
    }
    rf = falloc();
    if (rf == NULL) {
        pipe_free(pi);
        return;
    }
    r = u.u_rval;
    wf = falloc();
    if (wf == NULL) {
        rf->f_count = 0;
        fdrelease(r);
        pipe_free(pi);
        return;
    }
    u.u_rval2 = u.u_rval;
    u.u_rval = r;
    rf->f_flag = FREAD;
    wf->f_flag = FWRITE;
    rf->f_type = wf->f_type = DTYPE_PIPE;
    rf->f_data = wf->f_data = (caddr_t)pi;
}
