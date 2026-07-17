/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/user.h>

#include <vm/vm_shm.h>

static int
shm_copy_name(const char *user_name, char *name)
{
    unsigned index;
    char byte;
    int error;

    if (user_name == 0 || name == 0)
        return EFAULT;
    for (index = 0; index <= VM_SHM_NAME_MAX; ++index) {
        error = copyin((caddr_t)(user_name + index), &byte, 1);
        if (error != 0)
            return error;
        name[index] = byte;
        if (byte == '\0') {
            if (index < 2)
                return EINVAL;
            return 0;
        }
        if ((index == 0 && byte != '/') ||
            (index != 0 && byte == '/'))
            return EINVAL;
    }
    name[0] = '\0';
    return ENAMETOOLONG;
}

static int
shm_access(struct vm_shm *shm, int flags)
{
    struct vm_shm_info info;
    unsigned required;
    int error;

    error = vm_shm_get_info(shm, &info);
    if (error != 0)
        return error;
    if (u.u_uid == 0)
        return 0;
    required = 0;
    if ((flags & FREAD) != 0)
        required |= S_IREAD;
    if ((flags & FWRITE) != 0)
        required |= S_IWRITE;
    if (u.u_uid != (uid_t)info.vsi_owner) {
        required >>= 3;
        if (!groupmember((gid_t)info.vsi_group))
            required >>= 3;
    }
    return (info.vsi_mode & required) == required ? 0 : EACCES;
}

static void
shm_falloc_abort(struct file *fp, int fd)
{
    u.u_ofile[fd] = 0;
    fp->f_count = 0;
    while (u.u_lastfile >= 0 && u.u_ofile[u.u_lastfile] == 0)
        --u.u_lastfile;
}

void
shm_open(void)
{
    struct a {
        const char *name;
        int oflag;
        int mode;
    } *uap;
    struct vm_shm *shm;
    struct file *fp;
    char name[VM_SHM_NAME_MAX + 1];
    int access_mode;
    int acquired;
    int fd;
    int flags;
    int error;

    uap = (struct a *)u.u_arg;
    access_mode = uap->oflag & O_ACCMODE;
    if ((access_mode != O_RDONLY && access_mode != O_RDWR) ||
        (uap->oflag & ~(O_ACCMODE | O_CREAT | O_EXCL | O_TRUNC)) != 0 ||
        ((uap->oflag & O_TRUNC) != 0 && access_mode != O_RDWR)) {
        u.u_error = EINVAL;
        return;
    }
    error = shm_copy_name(uap->name, name);
    if (error != 0) {
        u.u_error = error;
        return;
    }
    fp = falloc();
    if (fp == 0)
        return;
    fd = u.u_rval;
    flags = FFLAGS(uap->oflag) & FMASK;
    shm = 0;
    acquired = 0;
    error = vm_shm_lookup(name, &shm);
    if (error == 0) {
        if ((uap->oflag & (O_CREAT | O_EXCL)) ==
            (O_CREAT | O_EXCL))
            error = EEXIST;
        if (error == 0)
            error = shm_access(shm, flags);
        if (error == 0)
            error = vm_shm_retain(shm);
        acquired = error == 0;
    } else if (error == ENOENT && (uap->oflag & O_CREAT) != 0) {
        error = vm_shm_create(name, (unsigned)u.u_uid,
            (unsigned)u.u_groups[0],
            (unsigned)uap->mode & ~(unsigned)u.u_cmask, &shm);
        acquired = error == 0;
    }
    if (error == 0 && (uap->oflag & O_TRUNC) != 0)
        error = vm_shm_truncate(shm, 0);
    if (error != 0) {
        if (acquired)
            (void)vm_shm_close(shm);
        shm_falloc_abort(fp, fd);
        u.u_error = error;
        return;
    }
    fp->f_flag = flags;
    fp->f_type = DTYPE_SHM;
    fp->f_data = (caddr_t)shm;
}

void
shm_unlink(void)
{
    struct a {
        const char *name;
    } *uap;
    struct vm_shm_info info;
    struct vm_shm *shm;
    char name[VM_SHM_NAME_MAX + 1];
    int error;

    uap = (struct a *)u.u_arg;
    error = shm_copy_name(uap->name, name);
    if (error == 0)
        error = vm_shm_lookup(name, &shm);
    if (error == 0)
        error = vm_shm_get_info(shm, &info);
    if (error == 0 && u.u_uid != 0 &&
        u.u_uid != (uid_t)info.vsi_owner)
        error = EPERM;
    if (error == 0)
        error = vm_shm_unlink(shm);
    u.u_error = error;
}

int
shm_truncate_file(struct file *fp, off_t length)
{
    if (fp == 0 || fp->f_type != DTYPE_SHM || fp->f_data == 0)
        return EINVAL;
    if ((fp->f_flag & FWRITE) == 0)
        return EINVAL;
    if (length < 0 || (uint64_t)length > VM_SIZE_MAX)
        return EFBIG;
    return vm_shm_truncate((struct vm_shm *)fp->f_data,
        (vm_size_t)length);
}

int
shm_fstat(struct file *fp, struct stat *status)
{
    struct vm_shm_info info;
    int error;

    if (fp == 0 || fp->f_type != DTYPE_SHM || fp->f_data == 0 ||
        status == 0)
        return EINVAL;
    error = vm_shm_get_info((struct vm_shm *)fp->f_data, &info);
    if (error != 0)
        return error;
    status->st_mode = S_IFREG | info.vsi_mode;
    status->st_nlink = info.vsi_linked ? 1 : 0;
    status->st_uid = (uid_t)info.vsi_owner;
    status->st_gid = (gid_t)info.vsi_group;
    status->st_size = (off_t)info.vsi_size;
    status->st_blksize = VM_PAGE_SIZE;
    status->st_blocks = info.vsi_size / DEV_BSIZE +
        (info.vsi_size % DEV_BSIZE != 0);
    return 0;
}

static int
shm_rw(struct file *fp, struct uio *uio)
{
    (void)fp;
    (void)uio;
    return EOPNOTSUPP;
}

static int
shm_ioctl(struct file *fp, u_int command, char *data)
{
    (void)fp;
    (void)command;
    (void)data;
    return EINVAL;
}

static int
shm_select(struct file *fp, int flag)
{
    (void)fp;
    (void)flag;
    return 1;
}

static int
shm_close_file(struct file *fp)
{
    struct vm_shm *shm;

    if (fp == 0 || fp->f_type != DTYPE_SHM || fp->f_data == 0)
        return EINVAL;
    shm = (struct vm_shm *)fp->f_data;
    fp->f_data = 0;
    return vm_shm_close(shm);
}

const struct fileops shmops = {
    shm_rw, shm_ioctl, shm_select, shm_close_file
};
