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
#include <sys/conf.h>
#include <sys/vm.h>
#include <sys/mman.h>
#include <sys/systm.h>
#include <vm/vmspace.h>
#include <vm/vm_object.h>
#include <vm/vm_shm.h>
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
    vm_ooffset_t object_offset;
    vm_prot_t maximum;
    dev_t dev;
    off_t offset;
    unsigned device_paddr;
    int device_cache;
    int device_mapping;
    int error;
    int had_object;
    unsigned vm_flags;

    uap = (struct a *)u.u_arg;
    vmspace = vmspace_current();
    inode = 0;
    device_mapping = 0;
    error = 0;
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
    offset = syscall_off64_arg(&u.u_arg[6]);
    if (vm_size_round_page((vm_size_t)uap->length, &size) != 0 ||
        size == 0) {
        u.u_error = EINVAL;
        return;
    }
    vm_flags = (uap->flags & MAP_SHARED) != 0 ? VM_MAP_SHARED : 0;
    if ((uap->flags & MAP_ANON) != 0) {
        if (uap->fd != -1 || offset != 0) {
            u.u_error = EINVAL;
            return;
        }
        object = 0;
        object_offset = 0;
        maximum = VM_PROT_ALL;
        had_object = 0;
    } else {
        if (offset < 0 || (offset & (off_t)VM_PAGE_MASK) != 0) {
            u.u_error = EINVAL;
            return;
        }
        fp = getf(uap->fd);
        if (fp == 0)
            return;
        if (fp->f_type == DTYPE_SHM) {
            if ((uap->protection & PROT_EXEC) != 0 ||
                (uint64_t)offset > VM_SIZE_MAX) {
                u.u_error = (uap->protection & PROT_EXEC) != 0 ?
                    EACCES : EOVERFLOW;
                return;
            }
            maximum = VM_PROT_READ;
            if ((uap->flags & MAP_SHARED) != 0) {
                if ((fp->f_flag & FWRITE) != 0)
                    maximum |= VM_PROT_WRITE;
                error = vm_shm_object_reference(
                    (struct vm_shm *)fp->f_data, &object);
                had_object = error == 0 &&
                    vmspace_contains_object(vmspace, object);
            } else {
                maximum |= VM_PROT_WRITE;
                error = vm_shm_object_clone(
                    (struct vm_shm *)fp->f_data, &object);
                had_object = 0;
                vm_flags |= VM_MAP_COW;
            }
            object_offset = (vm_ooffset_t)offset;
        } else if (fp->f_type != DTYPE_INODE) {
            u.u_error = ENODEV;
            return;
        } else if ((fp->f_flag & FREAD) == 0) {
            u.u_error = EACCES;
            return;
        } else if ((uap->flags & MAP_SHARED) != 0 &&
            (uap->protection & PROT_WRITE) != 0 &&
            (fp->f_flag & FWRITE) == 0) {
            u.u_error = EACCES;
            return;
        } else {
            inode = (struct inode *)fp->f_data;
        }
        if (error != 0) {
            u.u_error = error;
            return;
        }
        if (fp->f_type == DTYPE_SHM) {
            if (((vm_prot_t)uap->protection & ~maximum) != 0) {
                (void)vm_object_release(object);
                u.u_error = EACCES;
                return;
            }
        } else if (inode == 0) {
            u.u_error = ENODEV;
            return;
        } else if ((inode->i_mode & IFMT) == IFCHR) {
            dev = inode->i_rdev;
            if (major(dev) == MEM_MAJOR && minor(dev) == ZERO_MINOR) {
                if (offset != 0) {
                    u.u_error = EINVAL;
                    return;
                }
                object = 0;
                object_offset = 0;
                maximum = VM_PROT_ALL;
                had_object = 0;
            } else {
                if ((uap->flags & MAP_SHARED) == 0 ||
                    (uap->protection & PROT_EXEC) != 0 ||
                    major(dev) >= nchrdev ||
                    cdevsw[major(dev)].d_mmap == 0) {
                    u.u_error = ENODEV;
                    return;
                }
                maximum = VM_PROT_READ;
                if ((fp->f_flag & FWRITE) != 0)
                    maximum |= VM_PROT_WRITE;
                if (((vm_prot_t)uap->protection & ~maximum) != 0) {
                    u.u_error = EACCES;
                    return;
                }
                error = (*cdevsw[major(dev)].d_mmap)(dev, offset,
                    (u_int)size, uap->protection, &device_paddr,
                    &device_cache);
                if (error != 0) {
                    u.u_error = error;
                    return;
                }
                object = 0;
                object_offset = 0;
                had_object = 0;
                device_mapping = 1;
            }
        } else if ((inode->i_mode & IFMT) != IFREG) {
            u.u_error = ENODEV;
            return;
        } else if ((uap->flags & MAP_SHARED) != 0) {
            if ((vm_ooffset_t)offset > 0x80000000u ||
                (vm_ooffset_t)size >
                0x80000000u - (vm_ooffset_t)offset) {
                u.u_error = EOVERFLOW;
                return;
            }
            error = vm_vnode_shared_object(inode, &object);
            object_offset = (vm_ooffset_t)offset;
            maximum = VM_PROT_READ | VM_PROT_EXECUTE;
            if ((fp->f_flag & FWRITE) != 0)
                maximum |= VM_PROT_WRITE;
            had_object = error == 0 &&
                vmspace_contains_object(vmspace, object);
        } else {
            error = vm_vnode_object_create(inode,
                (vm_ooffset_t)offset, size, &object);
            object_offset = 0;
            maximum = VM_PROT_ALL;
            had_object = 0;
        }
        if (error != 0) {
            u.u_error = error;
            return;
        }
    }
    if ((uap->flags & MAP_FIXED) != 0) {
        if (!vm_vaddr_page_aligned((vm_vaddr_t)uap->address)) {
            if (object != 0)
                (void)vm_object_release(object);
            u.u_error = EINVAL;
            return;
        }
        result = (vm_vaddr_t)uap->address;
        if (device_mapping)
            error = vmspace_map_device_fixed(vmspace, result, size,
                (vm_prot_t)uap->protection, maximum,
                (vm_paddr_t)device_paddr,
                (enum pmap_cache)device_cache);
        else if (object == 0)
            error = vmspace_map_anon_fixed(vmspace, result, size,
                (vm_prot_t)uap->protection, vm_flags);
        else
            error = vmspace_map_object_fixed(vmspace, result, size,
                (vm_prot_t)uap->protection, maximum, vm_flags,
                object, object_offset);
    } else {
        hint = uap->address == 0 ? MMAP_DEFAULT_BASE :
            vm_vaddr_trunc_page((vm_vaddr_t)uap->address);
        if (device_mapping) {
            error = vmspace_map_device_any(vmspace, hint, size,
                (vm_prot_t)uap->protection, maximum,
                (vm_paddr_t)device_paddr,
                (enum pmap_cache)device_cache, &result);
            if (error == ENOMEM && hint != MMAP_DEFAULT_BASE)
                error = vmspace_map_device_any(vmspace,
                    MMAP_DEFAULT_BASE, size,
                    (vm_prot_t)uap->protection, maximum,
                    (vm_paddr_t)device_paddr,
                    (enum pmap_cache)device_cache, &result);
        } else if (object == 0) {
            error = vmspace_map_anon_any(vmspace, hint, size,
                (vm_prot_t)uap->protection, vm_flags, &result);
            if (error == ENOMEM && hint != MMAP_DEFAULT_BASE)
                error = vmspace_map_anon_any(vmspace, MMAP_DEFAULT_BASE,
                    size, (vm_prot_t)uap->protection, vm_flags, &result);
        } else {
            error = vmspace_map_object_any(vmspace, hint, size,
                (vm_prot_t)uap->protection, maximum, vm_flags,
                object, object_offset, &result);
            if (error == ENOMEM && hint != MMAP_DEFAULT_BASE)
                error = vmspace_map_object_any(vmspace,
                    MMAP_DEFAULT_BASE, size,
                    (vm_prot_t)uap->protection, maximum, vm_flags,
                    object, object_offset, &result);
        }
    }
    if (error != 0) {
        if (object != 0)
            (void)vm_object_release(object);
        u.u_error = error;
        return;
    }
    if (object != 0 && had_object)
        (void)vm_object_release(object);
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
    unsigned pager_flags;

    uap = (struct a *)u.u_arg;
    mode = uap->flags & (MS_ASYNC | MS_SYNC);
    if ((uap->flags & ~(MS_ASYNC | MS_INVALIDATE | MS_SYNC)) != 0 ||
        (mode != MS_ASYNC && mode != MS_SYNC)) {
        u.u_error = EINVAL;
        return;
    }
    u.u_error = mman_range(uap->address, uap->length, &start, &size);
    pager_flags = mode == MS_SYNC ? VM_PAGER_IO_SYNC : 0;
    if ((uap->flags & MS_INVALIDATE) != 0)
        pager_flags |= VM_PAGER_IO_INVALIDATE;
    if (u.u_error == 0)
        u.u_error = vmspace_sync(vmspace_current(), start, size,
            pager_flags);
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
