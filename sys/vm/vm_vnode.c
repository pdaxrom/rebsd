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
#include <sys/systm.h>
#include <sys/uio.h>

#include <vm/vm_vnode.h>

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
    vm_vnode_pagein
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
