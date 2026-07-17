/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/inode.h>
#include <sys/mount.h>
#include <sys/systm.h>
#include <sys/uio.h>

#include <vm/vm_vnode.h>

#define VM_VNODE_SHARED_SIZE    0x80000000u

struct vm_vnode_shared {
    struct inode     *vvs_inode;
    struct vm_object *vvs_object;
    unsigned          vvs_references;
    unsigned          vvs_busy;
};

static struct vm_vnode_shared vm_vnode_shared_pool[NINODE];

static int
vm_vnode_backing_sync_locked(struct inode *inode)
{
    struct mount *mount;

    if (inode->i_fs != 0) {
        mount = (struct mount *)((int)inode->i_fs -
            offsetof(struct mount, m_filsys));
        if (mount->m_ops != 0 && mount->m_ops != &ufs_vfsops)
            return vfs_sync(mount);
    }
    syncip(inode);
    return 0;
}

static void
vm_vnode_reference(void *cookie)
{
    struct inode *inode = cookie;

    igrab(inode);
    iunlock(inode);
}

static void
vm_vnode_release(void *cookie)
{
    irele((struct inode *)cookie);
}

static int
vm_vnode_pagein(void *cookie, vm_ooffset_t offset, void *buffer,
    vm_size_t length)
{
    struct inode *inode = cookie;
    vm_ooffset_t file_size;
    vm_size_t count;
    int residual;
    int error;

    bzero((caddr_t)buffer, (unsigned)length);
    ilock(inode);
    if (inode->i_size < 0) {
        error = EIO;
        goto out;
    }
    file_size = (vm_ooffset_t)inode->i_size;
    if (offset >= file_size) {
        error = ENXIO;
        goto out;
    }
    count = length;
    if ((vm_ooffset_t)count > file_size - offset)
        count = (vm_size_t)(file_size - offset);
    residual = 0;
    error = rdwri(UIO_READ, inode, (caddr_t)buffer, (int)count,
        (off_t)offset, 0, &residual);
    if (error == 0 && residual != 0)
        error = EIO;
out:
    iunlock(inode);
    return error;
}

static const struct vm_object_pager_ops vm_vnode_pager_ops = {
    vm_vnode_reference,
    vm_vnode_release,
    vm_vnode_pagein,
    0,
    0
};

int
vm_vnode_object_create(struct inode *inode, vm_ooffset_t offset,
    vm_size_t size, struct vm_object **result)
{
    if (inode == 0 || result == 0 || (inode->i_mode & IFMT) != IFREG ||
        size == 0 || (offset & VM_PAGE_MASK) != 0 ||
        offset > INT64_MAX || (vm_ooffset_t)size - 1 > INT64_MAX - offset)
        return EINVAL;
    return vm_object_create_paged(size, &vm_vnode_pager_ops, inode,
        offset, result);
}

static struct vm_vnode_shared *
vm_vnode_shared_find(struct inode *inode)
{
    unsigned index;

    for (index = 0; index < NINODE; ++index) {
        if (vm_vnode_shared_pool[index].vvs_inode == inode &&
            vm_vnode_shared_pool[index].vvs_object != 0)
            return &vm_vnode_shared_pool[index];
    }
    return 0;
}

static void
vm_vnode_shared_reference(void *cookie)
{
    struct vm_vnode_shared *shared = cookie;

    if (shared->vvs_references++ == 0) {
        igrab(shared->vvs_inode);
        iunlock(shared->vvs_inode);
    }
}

static void
vm_vnode_shared_release(void *cookie)
{
    struct vm_vnode_shared *shared = cookie;
    struct inode *inode;

    if (--shared->vvs_references != 0)
        return;
    inode = shared->vvs_inode;
    bzero((caddr_t)shared, sizeof(*shared));
    irele(inode);
}

static int
vm_vnode_shared_pagein(void *cookie, vm_ooffset_t offset, void *buffer,
    vm_size_t length)
{
    struct vm_vnode_shared *shared = cookie;
    int error;

    ++shared->vvs_busy;
    error = vm_vnode_pagein(shared->vvs_inode, offset, buffer, length);
    --shared->vvs_busy;
    return error;
}

static int
vm_vnode_shared_pageout(void *cookie, vm_ooffset_t offset,
    const void *buffer, vm_size_t length, unsigned flags)
{
    struct vm_vnode_shared *shared = cookie;
    struct inode *inode = shared->vvs_inode;
    vm_ooffset_t file_size;
    vm_size_t count;
    int residual;
    int error;
    int locked;

    locked = (flags & VM_PAGER_IO_LOCKED) != 0;
    ++shared->vvs_busy;
    if (!locked)
        ilock(inode);
    if (inode->i_size < 0) {
        error = EIO;
        goto out;
    }
    file_size = (vm_ooffset_t)inode->i_size;
    if (offset >= file_size) {
        error = ENXIO;
        goto out;
    }
    count = length;
    if ((vm_ooffset_t)count > file_size - offset)
        count = (vm_size_t)(file_size - offset);
    residual = 0;
    error = rdwri(UIO_WRITE, inode, (caddr_t)buffer, (int)count,
        (off_t)offset, (flags & VM_PAGER_IO_SYNC) != 0 ? IO_SYNC : 0,
        &residual);
    if (error == 0 && residual != 0)
        error = EIO;
out:
    if (!locked)
        iunlock(inode);
    --shared->vvs_busy;
    return error;
}

static int
vm_vnode_shared_sync(void *cookie, unsigned flags)
{
    struct vm_vnode_shared *shared = cookie;
    int error;
    int locked;

    locked = (flags & VM_PAGER_IO_LOCKED) != 0;
    ++shared->vvs_busy;
    if (!locked)
        ilock(shared->vvs_inode);
    error = vm_vnode_backing_sync_locked(shared->vvs_inode);
    if (!locked)
        iunlock(shared->vvs_inode);
    --shared->vvs_busy;
    return error;
}

static const struct vm_object_pager_ops vm_vnode_shared_pager_ops = {
    vm_vnode_shared_reference,
    vm_vnode_shared_release,
    vm_vnode_shared_pagein,
    vm_vnode_shared_pageout,
    vm_vnode_shared_sync
};

int
vm_vnode_shared_object(struct inode *inode, struct vm_object **result)
{
    struct vm_vnode_shared *shared;
    unsigned index;
    int error;

    if (inode == 0 || result == 0 || (inode->i_mode & IFMT) != IFREG)
        return EINVAL;
    shared = vm_vnode_shared_find(inode);
    if (shared != 0) {
        error = vm_object_reference(shared->vvs_object);
        if (error == 0)
            *result = shared->vvs_object;
        return error;
    }
    for (index = 0; index < NINODE; ++index) {
        if (vm_vnode_shared_pool[index].vvs_inode == 0)
            break;
    }
    if (index == NINODE)
        return ENOSPC;
    shared = &vm_vnode_shared_pool[index];
    bzero((caddr_t)shared, sizeof(*shared));
    shared->vvs_inode = inode;
    error = vm_object_create_paged(VM_VNODE_SHARED_SIZE,
        &vm_vnode_shared_pager_ops, shared, 0, &shared->vvs_object);
    if (error != 0) {
        bzero((caddr_t)shared, sizeof(*shared));
        return error;
    }
    *result = shared->vvs_object;
    return 0;
}

int
vm_vnode_sync_locked(struct inode *inode, vm_ooffset_t offset,
    vm_size_t size, int synchronous)
{
    struct vm_vnode_shared *shared;
    unsigned flags;

    shared = vm_vnode_shared_find(inode);
    if (shared == 0 || shared->vvs_busy != 0 || size == 0)
        return 0;
    if (offset >= VM_VNODE_SHARED_SIZE)
        return 0;
    if ((vm_ooffset_t)size > VM_VNODE_SHARED_SIZE - offset)
        size = (vm_size_t)(VM_VNODE_SHARED_SIZE - offset);
    flags = VM_PAGER_IO_LOCKED;
    if (synchronous)
        flags |= VM_PAGER_IO_SYNC;
    return vm_object_sync(shared->vvs_object, offset, size, flags);
}

int
vm_vnode_update_locked(struct inode *inode, vm_ooffset_t offset,
    const void *buffer, vm_size_t size)
{
    struct vm_vnode_shared *shared;

    shared = vm_vnode_shared_find(inode);
    if (shared == 0 || shared->vvs_busy != 0 || size == 0)
        return 0;
    if (offset >= VM_VNODE_SHARED_SIZE)
        return 0;
    if ((vm_ooffset_t)size > VM_VNODE_SHARED_SIZE - offset)
        size = (vm_size_t)(VM_VNODE_SHARED_SIZE - offset);
    return vm_object_update(shared->vvs_object, offset, buffer, size);
}

int
vm_vnode_invalidate_locked(struct inode *inode, vm_ooffset_t offset,
    vm_size_t size)
{
    struct vm_vnode_shared *shared;

    shared = vm_vnode_shared_find(inode);
    if (shared == 0 || shared->vvs_busy != 0 || size == 0)
        return 0;
    if (offset >= VM_VNODE_SHARED_SIZE)
        return 0;
    if ((vm_ooffset_t)size > VM_VNODE_SHARED_SIZE - offset)
        size = (vm_size_t)(VM_VNODE_SHARED_SIZE - offset);
    return vm_object_invalidate(shared->vvs_object, offset, size);
}

int
vm_vnode_truncate_locked(struct inode *inode, vm_ooffset_t length)
{
    struct vm_vnode_shared *shared;
    vm_ooffset_t rounded;
    int error;

    shared = vm_vnode_shared_find(inode);
    if (shared == 0 || shared->vvs_busy != 0)
        return 0;
    if (length >= VM_VNODE_SHARED_SIZE) {
        ++shared->vvs_busy;
        error = vm_object_sync(shared->vvs_object, 0,
            VM_VNODE_SHARED_SIZE, VM_PAGER_IO_LOCKED);
        --shared->vvs_busy;
        return error;
    }
    rounded = (length + VM_PAGE_MASK) & ~((vm_ooffset_t)VM_PAGE_MASK);
    ++shared->vvs_busy;
    error = 0;
    if (rounded != 0)
        error = vm_object_sync(shared->vvs_object, 0,
            (vm_size_t)rounded, VM_PAGER_IO_LOCKED);
    if (error == 0 && rounded > length)
        error = vm_object_zero_range(shared->vvs_object, length,
            (vm_size_t)(rounded - length));
    if (error == 0 && rounded < VM_VNODE_SHARED_SIZE)
        error = vm_object_invalidate(shared->vvs_object, rounded,
            VM_VNODE_SHARED_SIZE - (vm_size_t)rounded);
    --shared->vvs_busy;
    return error;
}

int
vm_vnode_fsync_locked(struct inode *inode)
{
    int error;

    error = 0;
    if ((inode->i_mode & IFMT) == IFREG && inode->i_size > 0)
        error = vm_vnode_sync_locked(inode, 0,
            (vm_size_t)inode->i_size, 0);
    if (error == 0)
        error = vm_vnode_backing_sync_locked(inode);
    return error;
}
