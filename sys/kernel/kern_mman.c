/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/inode.h>
#include <sys/vm.h>
#include <sys/mman.h>
#include <sys/systm.h>
#include <vm/vmspace.h>
#include <vm/vm_object.h>
#include <vm/vm_vnode.h>

#define MMAP_DEFAULT_BASE       0x10000000u

static int
mman_range(unsigned address, unsigned length, vm_vaddr_t *start,
    vm_size_t *size)
{
    if (length == 0 || !vm_vaddr_page_aligned((vm_vaddr_t)address) ||
        vm_size_round_page((vm_size_t)length, size) != 0 ||
        *size == 0 || *size - 1 > VM_VADDR_MAX - address)
        return EINVAL;
    *start = (vm_vaddr_t)address;
    return 0;
}

void
brk()
{
    struct a {
        int naddr;
    };
    struct proc *p;
    vm_vaddr_t old_end, new_end;
    unsigned address, base, maxmem, newsize, oldsize;
    int error;

    p = u.u_procp;
    address = (unsigned)((struct a *)u.u_arg)->naddr;
    base = (unsigned)p->p_daddr;
    if (address < base) {
        u.u_error = EINVAL;
        return;
    }
    newsize = address - base;
    oldsize = p->p_dsize;
    maxmem = MAXMEM;
    if (p->p_vmspace == 0 || newsize < p->p_dmin ||
        newsize > maxmem || u.u_tsize > maxmem - newsize ||
        u.u_ssize > maxmem - newsize - u.u_tsize) {
        u.u_error = ENOMEM;
        return;
    }

    if (vm_vaddr_round_page(base + oldsize,
        &old_end) != 0 || vm_vaddr_round_page(u.u_procp->p_daddr + newsize,
        &new_end) != 0) {
        u.u_error = ENOMEM;
        return;
    }
    error = 0;
    if (new_end > old_end) {
        vm_vaddr_t stack_page = vm_vaddr_trunc_page(p->p_saddr);

        if (stack_page < VM_PAGE_SIZE ||
            new_end > stack_page - VM_PAGE_SIZE) {
            u.u_error = ENOMEM;
            return;
        }
        error = vmspace_map_anon(p->p_vmspace, old_end,
            new_end - old_end, VM_PROT_READ | VM_PROT_WRITE, 0);
    }
    if (error == 0 && newsize > oldsize) {
        error = vmspace_zero(p->p_vmspace, base + oldsize,
            newsize - oldsize);
        if (error != 0 && new_end > old_end)
            (void)vmspace_unmap(p->p_vmspace, old_end,
                new_end - old_end);
    } else if (error == 0 && newsize < oldsize) {
        if (new_end > base + newsize)
            error = vmspace_zero(p->p_vmspace, base + newsize,
                new_end - (base + newsize));
        if (error == 0 && old_end > new_end)
            error = vmspace_unmap(p->p_vmspace, new_end,
                old_end - new_end);
    }
    if (error != 0) {
        u.u_error = error == EINVAL ? ENOMEM : error;
        return;
    }
    p->p_dsize = newsize;
    u.u_dsize = newsize;
    u.u_rval = u.u_procp->p_daddr + u.u_dsize;
}

void
mmap(void)
{
    struct a {
        unsigned address;
        unsigned length;
        int protection;
        int flags;
        int fd;
        int pad;
        int offset[2];
    } *uap;
    struct vmspace *vmspace;
    struct vm_object *object;
    struct file *fp;
    struct inode *inode;
    vm_vaddr_t hint;
    vm_vaddr_t result;
    vm_size_t size;
    off_t offset;
    int error;
    unsigned vm_flags;

    uap = (struct a *)u.u_arg;
    vmspace = vmspace_current();
    if (vmspace == 0 || uap->length == 0 ||
        (uap->protection & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) != 0) {
        u.u_error = EINVAL;
        return;
    }
    if ((uap->flags & ~(MAP_PRIVATE | MAP_SHARED | MAP_FIXED |
        MAP_ANON)) != 0 ||
        ((uap->flags & (MAP_PRIVATE | MAP_SHARED)) != MAP_PRIVATE &&
        (uap->flags & (MAP_PRIVATE | MAP_SHARED)) != MAP_SHARED)) {
        u.u_error = EINVAL;
        return;
    }
    if ((uap->flags & MAP_SHARED) != 0) {
        u.u_error = EOPNOTSUPP;
        return;
    }
    offset = syscall_off64_arg(&u.u_arg[6]);
    if (vm_size_round_page((vm_size_t)uap->length, &size) != 0 ||
        size == 0) {
        u.u_error = EINVAL;
        return;
    }
    if ((uap->flags & MAP_ANON) != 0) {
        if (uap->fd != -1 || offset != 0) {
            u.u_error = EINVAL;
            return;
        }
        object = 0;
    } else {
        if (offset < 0 || (offset & (off_t)VM_PAGE_MASK) != 0) {
            u.u_error = EINVAL;
            return;
        }
        fp = getf(uap->fd);
        if (fp == 0)
            return;
        if (fp->f_type != DTYPE_INODE) {
            u.u_error = ENODEV;
            return;
        }
        if ((fp->f_flag & FREAD) == 0) {
            u.u_error = EACCES;
            return;
        }
        inode = (struct inode *)fp->f_data;
        if (inode == 0 || (inode->i_mode & IFMT) != IFREG) {
            u.u_error = ENODEV;
            return;
        }
        error = vm_vnode_object_create(inode, (vm_ooffset_t)offset,
            size, &object);
        if (error != 0) {
            u.u_error = error;
            return;
        }
    }
    vm_flags = 0;
    if ((uap->flags & MAP_FIXED) != 0) {
        if (!vm_vaddr_page_aligned((vm_vaddr_t)uap->address)) {
            if (object != 0)
                (void)vm_object_release(object);
            u.u_error = EINVAL;
            return;
        }
        result = (vm_vaddr_t)uap->address;
        if (object == 0)
            error = vmspace_map_anon_fixed(vmspace, result, size,
                (vm_prot_t)uap->protection, vm_flags);
        else
            error = vmspace_map_object_fixed(vmspace, result, size,
                (vm_prot_t)uap->protection, VM_PROT_ALL, vm_flags,
                object);
    } else {
        hint = uap->address == 0 ? MMAP_DEFAULT_BASE :
            vm_vaddr_trunc_page((vm_vaddr_t)uap->address);
        if (object == 0) {
            error = vmspace_map_anon_any(vmspace, hint, size,
                (vm_prot_t)uap->protection, vm_flags, &result);
            if (error == ENOMEM && hint != MMAP_DEFAULT_BASE)
                error = vmspace_map_anon_any(vmspace, MMAP_DEFAULT_BASE,
                    size, (vm_prot_t)uap->protection, vm_flags, &result);
        } else {
            error = vmspace_map_object_any(vmspace, hint, size,
                (vm_prot_t)uap->protection, VM_PROT_ALL, vm_flags,
                object, &result);
            if (error == ENOMEM && hint != MMAP_DEFAULT_BASE)
                error = vmspace_map_object_any(vmspace,
                    MMAP_DEFAULT_BASE, size,
                    (vm_prot_t)uap->protection, VM_PROT_ALL, vm_flags,
                    object, &result);
        }
    }
    if (error != 0) {
        if (object != 0)
            (void)vm_object_release(object);
        u.u_error = error;
        return;
    }
    u.u_rval = (int)result;
}

void
munmap(void)
{
    struct a {
        unsigned address;
        unsigned length;
    } *uap;
    vm_vaddr_t start = 0;
    vm_size_t size = 0;

    uap = (struct a *)u.u_arg;
    u.u_error = mman_range(uap->address, uap->length, &start, &size);
    if (u.u_error == 0)
        u.u_error = vmspace_unmap(vmspace_current(), start, size);
}

void
mprotect(void)
{
    struct a {
        unsigned address;
        unsigned length;
        int protection;
    } *uap;
    vm_vaddr_t start = 0;
    vm_size_t size = 0;

    uap = (struct a *)u.u_arg;
    if ((uap->protection & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) != 0) {
        u.u_error = EINVAL;
        return;
    }
    u.u_error = mman_range(uap->address, uap->length, &start, &size);
    if (u.u_error == 0)
        u.u_error = vmspace_protect(vmspace_current(), start, size,
            (vm_prot_t)uap->protection);
}

void
msync(void)
{
    struct a {
        unsigned address;
        unsigned length;
        int flags;
    } *uap;
    vm_vaddr_t start = 0;
    vm_size_t size = 0;
    int mode;

    uap = (struct a *)u.u_arg;
    mode = uap->flags & (MS_ASYNC | MS_SYNC);
    if ((uap->flags & ~(MS_ASYNC | MS_INVALIDATE | MS_SYNC)) != 0 ||
        (mode != MS_ASYNC && mode != MS_SYNC)) {
        u.u_error = EINVAL;
        return;
    }
    u.u_error = mman_range(uap->address, uap->length, &start, &size);
    if (u.u_error == 0)
        u.u_error = vmspace_check(vmspace_current(), start, size,
            VM_PROT_NONE);
}

void
madvise(void)
{
    struct a {
        unsigned address;
        unsigned length;
        int advice;
    } *uap;
    vm_vaddr_t start = 0;
    vm_size_t size = 0;

    uap = (struct a *)u.u_arg;
    if (uap->advice < MADV_NORMAL || uap->advice > MADV_FREE) {
        u.u_error = EINVAL;
        return;
    }
    u.u_error = mman_range(uap->address, uap->length, &start, &size);
    if (u.u_error == 0)
        u.u_error = vmspace_check(vmspace_current(), start, size,
            VM_PROT_NONE);
}

void
mincore(void)
{
    struct a {
        unsigned address;
        unsigned length;
        unsigned char *vector;
    } *uap;
    struct vmspace *vmspace;
    vm_vaddr_t address;
    vm_vaddr_t start = 0;
    vm_size_t size = 0;
    unsigned char state;
    int resident;

    uap = (struct a *)u.u_arg;
    if (uap->vector == 0) {
        u.u_error = EFAULT;
        return;
    }
    u.u_error = mman_range(uap->address, uap->length, &start, &size);
    vmspace = vmspace_current();
    if (u.u_error == 0)
        u.u_error = vmspace_check(vmspace, start, size, VM_PROT_NONE);
    if (u.u_error != 0)
        return;
    for (address = start; address < start + size;
        address += VM_PAGE_SIZE) {
        u.u_error = vmspace_mincore(vmspace, address, &resident);
        if (u.u_error != 0)
            return;
        state = resident ? MINCORE_INCORE : 0;
        u.u_error = copyout((caddr_t)&state,
            (caddr_t)&uap->vector[(address - start) / VM_PAGE_SIZE], 1);
    }
}

void
mlock(void)
{
    struct a {
        unsigned address;
        unsigned length;
    } *uap;
    vm_vaddr_t start = 0;
    vm_size_t size = 0;

    uap = (struct a *)u.u_arg;
    u.u_error = mman_range(uap->address, uap->length, &start, &size);
    if (u.u_error == 0)
        u.u_error = vmspace_wire(vmspace_current(), start, size, 1);
}

void
munlock(void)
{
    struct a {
        unsigned address;
        unsigned length;
    } *uap;
    vm_vaddr_t start = 0;
    vm_size_t size = 0;

    uap = (struct a *)u.u_arg;
    u.u_error = mman_range(uap->address, uap->length, &start, &size);
    if (u.u_error == 0)
        u.u_error = vmspace_wire(vmspace_current(), start, size, 0);
}
