/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/vm.h>
#include <sys/systm.h>
#include <vm/vmspace.h>

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
