#include <sys/errno.h>
#include <sys/param.h>
#include <sys/signal.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <vm/vmspace.h>

#include "interrupt.h"
#include "trap.h"

int
i386_trap_signal(unsigned vector)
{
    switch (vector) {
    case 0:                 /* divide error */
    case 4:                 /* overflow */
    case 5:                 /* bound range */
    case 7:                 /* device not available */
    case 9:                 /* coprocessor segment overrun */
    case 16:                /* x87 floating point */
    case 19:                /* SIMD floating point */
        return SIGFPE;
    case 1:                 /* debug */
    case 3:                 /* breakpoint */
        return SIGTRAP;
    case 6:                 /* invalid opcode */
        return SIGILL;
    case 10:                /* invalid TSS */
    case 11:                /* segment not present */
    case 17:                /* alignment check */
        return SIGBUS;
    case 12:                /* stack fault */
    case 13:                /* general protection */
    case 14:                /* page fault */
        return SIGSEGV;
    default:
        return 0;
    }
}

int
i386_user_trap(struct i386_trapframe *frame, unsigned code)
{
    struct proc *process;
    int signum;

    if ((frame->tf_cs & 3u) != 3u || md_curuser == (struct user *)0)
        return 0;
    process = u.u_procp;
    signum = i386_trap_signal(frame->tf_vector);
    if (process == (struct proc *)0 || signum == 0)
        return 0;

    u.u_frame = (int *)frame;
    u.u_code = (int)code;
    psignal(process, signum);
    return 1;
}

int
i386_grow_user_stack(unsigned address, int from_fault)
{
    struct proc *process;
    vm_vaddr_t guard_end;
    vm_vaddr_t old_page;

    process = u.u_procp;
    if (process == (struct proc *)0 ||
        process->p_vmspace == (struct vmspace *)0 ||
        vm_vaddr_round_page(process->p_daddr + process->p_dsize,
            &guard_end) != 0 ||
        guard_end > VM_VADDR_MAX - VM_PAGE_SIZE)
        return EFAULT;
    guard_end += VM_PAGE_SIZE;
    if ((vm_vaddr_t)address < guard_end || address > USER_DATA_END)
        return EFAULT;

    old_page = vm_vaddr_trunc_page(process->p_saddr);
    if (from_fault &&
        vm_vaddr_trunc_page((vm_vaddr_t)address) >= old_page)
        return EFAULT;
    if (vmspace_grow_stack(process->p_vmspace, process->p_saddr,
        (vm_vaddr_t)address, guard_end) != 0)
        return EFAULT;

    if (process->p_ssize < USER_DATA_END - address) {
        process->p_ssize = USER_DATA_END - address;
        process->p_saddr = address;
        u.u_ssize = process->p_ssize;
    }
    return 0;
}
