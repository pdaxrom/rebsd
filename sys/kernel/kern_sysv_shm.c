/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/systm.h>

#include <vm/vm_object.h>
#include <vm/vm_sysv_shm.h>
#include <vm/vmspace.h>

#define SYSV_SHM_MAP_BASE 0x20000000u

static int
sysv_shm_access(struct vm_sysv_shm *segment, unsigned required)
{
    struct vm_sysv_shm_info info;
    int error;

    error = vm_sysv_shm_get_info(segment, &info);
    if (error != 0)
        return error;
    if (u.u_uid == 0)
        return 0;
    if (u.u_uid != (uid_t)info.vssi_owner) {
        required >>= 3;
        if (!groupmember((gid_t)info.vssi_group))
            required >>= 3;
    }
    return (info.vssi_mode & required) == required ? 0 : EACCES;
}

static int
sysv_shm_owner(const struct vm_sysv_shm_info *info)
{
    return u.u_uid == 0 || u.u_uid == (uid_t)info->vssi_owner ||
        u.u_uid == (uid_t)info->vssi_creator;
}

void
shmget(void)
{
    struct a {
        key_t key;
        unsigned size;
        int shmflg;
    } *uap;
    struct vm_sysv_shm_info info;
    struct vm_sysv_shm *segment;
    unsigned required;
    int error;

    uap = (struct a *)u.u_arg;
    if ((uap->shmflg & ~(IPC_CREAT | IPC_EXCL | IPC_NOWAIT | 0777)) != 0) {
        u.u_error = EINVAL;
        return;
    }
    segment = 0;
    error = uap->key == IPC_PRIVATE ? ENOENT :
        vm_sysv_shm_lookup_key((int)uap->key, &segment);
    if (error == 0) {
        if ((uap->shmflg & (IPC_CREAT | IPC_EXCL)) ==
            (IPC_CREAT | IPC_EXCL)) {
            u.u_error = EEXIST;
            return;
        }
        error = vm_sysv_shm_get_info(segment, &info);
        if (error == 0 && uap->size != 0 && uap->size > info.vssi_size)
            error = EINVAL;
        required = 0;
        if ((uap->shmflg & 0444) != 0)
            required |= S_IREAD;
        if ((uap->shmflg & 0222) != 0)
            required |= S_IWRITE;
        if (error == 0 && required != 0)
            error = sysv_shm_access(segment, required);
    } else if (error == ENOENT &&
        (uap->key == IPC_PRIVATE || (uap->shmflg & IPC_CREAT) != 0)) {
        error = vm_sysv_shm_create((int)uap->key,
            (vm_size_t)uap->size, (unsigned)u.u_uid,
            (unsigned)u.u_groups[0], (unsigned)uap->shmflg,
            u.u_procp->p_pid, time.tv_sec, &segment);
        if (error == 0)
            error = vm_sysv_shm_get_info(segment, &info);
    }
    if (error == 0)
        u.u_rval = info.vssi_id;
    else
        u.u_error = error;
}

void
shmat(void)
{
    struct a {
        int shmid;
        const void *shmaddr;
        int shmflg;
    } *uap;
    struct vm_sysv_shm_info info;
    struct vm_sysv_shm *segment;
    struct vm_object *object;
    struct vmspace *vmspace;
    vm_vaddr_t address;
    vm_vaddr_t result;
    vm_size_t size;
    vm_prot_t protection;
    unsigned required;
    int had_object;
    int error;

    uap = (struct a *)u.u_arg;
    if ((uap->shmflg & ~(SHM_RDONLY | SHM_RND)) != 0) {
        u.u_error = EINVAL;
        return;
    }
    error = vm_sysv_shm_lookup_id(uap->shmid, &segment);
    if (error == 0)
        error = vm_sysv_shm_get_info(segment, &info);
    required = S_IREAD;
    protection = VM_PROT_READ;
    if ((uap->shmflg & SHM_RDONLY) == 0) {
        required |= S_IWRITE;
        protection |= VM_PROT_WRITE;
    }
    if (error == 0)
        error = sysv_shm_access(segment, required);
    if (error == 0)
        error = vm_size_round_page(info.vssi_size, &size);
    if (error != 0) {
        u.u_error = error;
        return;
    }
    address = (vm_vaddr_t)uap->shmaddr;
    if (address != 0 && (uap->shmflg & SHM_RND) != 0)
        address &= ~((vm_vaddr_t)SHMLBA - 1u);
    if (address != 0 && !vm_vaddr_page_aligned(address)) {
        u.u_error = EINVAL;
        return;
    }
    vmspace = vmspace_current();
    error = vm_sysv_shm_object_reference(segment, &object);
    if (error != 0) {
        u.u_error = error;
        return;
    }
    had_object = vmspace_contains_object(vmspace, object);
    if (address == 0)
        error = vmspace_map_object_any(vmspace, SYSV_SHM_MAP_BASE, size,
            protection, protection, VM_MAP_SHARED | VM_MAP_SYSV_SHM,
            object, 0, &result);
    else {
        result = address;
        error = vmspace_map_object(vmspace, result, size, protection,
            protection, VM_MAP_SHARED | VM_MAP_SYSV_SHM, object, 0);
    }
    if (error != 0) {
        (void)vm_object_release(object);
        u.u_error = error;
        return;
    }
    if (had_object)
        (void)vm_object_release(object);
    error = vmspace_sysv_attach(vmspace, segment, result, size,
        u.u_procp->p_pid, time.tv_sec);
    if (error != 0) {
        (void)vmspace_unmap(vmspace, result, size);
        u.u_error = error;
        return;
    }
    u.u_rval = (int)result;
}

void
shmdt(void)
{
    struct a {
        const void *shmaddr;
    } *uap;

    uap = (struct a *)u.u_arg;
    u.u_error = vmspace_sysv_detach(vmspace_current(),
        (vm_vaddr_t)uap->shmaddr, u.u_procp->p_pid, time.tv_sec);
}

static void
sysv_shm_export(const struct vm_sysv_shm_info *info,
    struct shmid_ds *status)
{
    bzero((caddr_t)status, sizeof(*status));
    status->shm_perm.uid = (uid_t)info->vssi_owner;
    status->shm_perm.gid = (gid_t)info->vssi_group;
    status->shm_perm.cuid = (uid_t)info->vssi_creator;
    status->shm_perm.cgid = (gid_t)info->vssi_creator_group;
    status->shm_perm.mode = (mode_t)info->vssi_mode;
    status->shm_perm.seq = info->vssi_sequence;
    status->shm_perm.key = (key_t)info->vssi_key;
    status->shm_segsz = info->vssi_size;
    status->shm_lpid = (pid_t)info->vssi_last_pid;
    status->shm_cpid = (pid_t)info->vssi_creator_pid;
    status->shm_nattch = info->vssi_attach_count;
    status->shm_atime = (time_t)info->vssi_attach_time;
    status->shm_dtime = (time_t)info->vssi_detach_time;
    status->shm_ctime = (time_t)info->vssi_change_time;
}

void
shmctl(void)
{
    struct a {
        int shmid;
        int command;
        struct shmid_ds *status;
    } *uap;
    struct vm_sysv_shm_info info;
    struct vm_sysv_shm *segment;
    struct shmid_ds status;
    unsigned owner;
    unsigned group;
    int error;

    uap = (struct a *)u.u_arg;
    error = vm_sysv_shm_lookup_id(uap->shmid, &segment);
    if (error == 0)
        error = vm_sysv_shm_get_info(segment, &info);
    if (error != 0) {
        u.u_error = error;
        return;
    }
    switch (uap->command) {
    case IPC_STAT:
        error = sysv_shm_access(segment, S_IREAD);
        if (error == 0) {
            sysv_shm_export(&info, &status);
            error = copyout((caddr_t)&status, (caddr_t)uap->status,
                sizeof(status));
        }
        break;

    case IPC_SET:
        if (!sysv_shm_owner(&info)) {
            error = EPERM;
            break;
        }
        error = copyin((caddr_t)uap->status, (caddr_t)&status,
            sizeof(status));
        if (error != 0)
            break;
        owner = (unsigned)status.shm_perm.uid;
        group = (unsigned)status.shm_perm.gid;
        if (u.u_uid != 0 && (owner != info.vssi_owner ||
            (group != info.vssi_group && !groupmember((gid_t)group)))) {
            error = EPERM;
            break;
        }
        error = vm_sysv_shm_set_permissions(segment, owner, group,
            (unsigned)status.shm_perm.mode, time.tv_sec);
        break;

    case IPC_RMID:
        error = sysv_shm_owner(&info) ?
            vm_sysv_shm_mark_remove(segment, u.u_procp->p_pid,
                time.tv_sec) : EPERM;
        break;

    default:
        error = EINVAL;
        break;
    }
    u.u_error = error;
}
