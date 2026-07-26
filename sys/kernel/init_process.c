/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <sys/param.h>
#include <sys/reboot.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <vm/pmap.h>
#include <vm/vmspace.h>

int boothowto;

/*
 * Finish constructing process 1 on its fresh kernel stack, then enter the
 * small user bootstrap which execs /sbin/init.  This is a separate entry
 * point so process creation never has to copy main()'s live C stack.
 */
void
init_process(void)
{
    struct proc *p;
    vm_vaddr_t data_start;
    vm_vaddr_t data_end;
    vm_vaddr_t stack_start;

#if defined(N64_TRACE) || defined(MIPS_TRACE)
    printf("mipsboot: proc1 trampoline pid=%d\n", u.u_procp->p_pid);
#endif
    (void)splhigh();
    p = u.u_procp;
    p->p_dsize = icodeend - icode;
    p->p_dmin = p->p_dsize;
    p->p_daddr = USER_DATA_START;
    p->p_ssize = 1024;
    p->p_saddr = USER_DATA_END - p->p_ssize;

    data_start = vm_vaddr_trunc_page(USER_DATA_START);
    if (vm_vaddr_round_page(USER_DATA_START + p->p_dsize,
        &data_end) != 0)
        panic("init data range");
    stack_start = vm_vaddr_trunc_page(p->p_saddr);
    if (vmspace_map_anon(p->p_vmspace, data_start,
        data_end - data_start, VM_PROT_ALL, VM_MAP_EXECUTABLE) != 0 ||
        vmspace_map_anon(p->p_vmspace, stack_start,
        USER_DATA_END - stack_start, VM_PROT_READ | VM_PROT_WRITE,
        VM_MAP_STACK) != 0 ||
        vmspace_write(p->p_vmspace, USER_DATA_START, icode,
        icodeend - icode) != 0)
        panic("init vmspace");

    if (boothowto & RB_SINGLE) {
        vm_vaddr_t flag_address = USER_DATA_START +
            (initflags - icode) + 1;
        char flag = 's';

        if (vmspace_write(p->p_vmspace, flag_address, &flag, 1) != 0)
            panic("init flags");
    }
    pmap_md_legacy_user_disable();
    if (vmspace_activate(p->p_vmspace) != 0)
        panic("init pmap");
#if defined(N64_TRACE) || defined(MIPS_TRACE)
    printf("mipsboot: entering proc1 user bootstrap\n");
#endif
    md_user_enter(USER_DATA_START, USER_DATA_END);
    panic("init user return");
}
