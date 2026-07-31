#include <sys/errno.h>
#include <sys/param.h>
#include <sys/signal.h>
#include <sys/signalvar.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <vm/vmspace.h>

#include "context.h"
#include "interrupt.h"
#include "signal_machdep.h"
#include "trap.h"

static int
i386_signal_frame_address(struct i386_trapframe *frame, int sig,
    vm_vaddr_t *address, int *oonstack)
{
    vm_vaddr_t stack_top;

    *oonstack = u.u_sigstk.ss_flags & SA_ONSTACK;
    if ((u.u_psflags & SAS_ALTSTACK) != 0 &&
        (u.u_sigstk.ss_flags & SA_ONSTACK) == 0 &&
        (u.u_sigonstack & sigmask(sig)) != 0) {
        stack_top = (vm_vaddr_t)(unsigned long)u.u_sigstk.ss_base;
        if (u.u_sigstk.ss_size < 0 ||
            stack_top > VM_VADDR_MAX - (unsigned)u.u_sigstk.ss_size)
            return EFAULT;
        stack_top += (unsigned)u.u_sigstk.ss_size;
        u.u_sigstk.ss_flags |= SA_ONSTACK;
    } else {
        stack_top = frame->tf_useresp;
    }

    if (stack_top < sizeof(struct i386_sigframe))
        return EFAULT;
    *address = (stack_top - sizeof(struct i386_sigframe)) & ~0x0fu;
    if ((u.u_sigstk.ss_flags & SA_ONSTACK) != 0)
        return 0;

    return i386_grow_user_stack(*address, 0);
}

void
sendsig(sig_t handler, int sig, long mask)
{
    struct i386_trapframe *frame;
    struct i386_sigframe signal_frame;
    vm_vaddr_t frame_address;
    int oonstack;

    if (md_curuser == (struct user *)0 || u.u_procp == (struct proc *)0 ||
        u.u_procp->p_vmspace == (struct vmspace *)0 ||
        u.u_frame == (int *)0) {
        fatalsig(SIGILL);
        return;
    }
    frame = (struct i386_trapframe *)u.u_frame;
    if ((frame->tf_cs & 3u) != 3u ||
        i386_signal_frame_address(frame, sig, &frame_address,
            &oonstack) != 0) {
        fatalsig(SIGILL);
        return;
    }

    bzero(&signal_frame, sizeof(signal_frame));
    signal_frame.sf_return = u.u_sigtramp;
    signal_frame.sf_signum = sig;
    signal_frame.sf_code = u.u_code;
    signal_frame.sf_context = frame_address +
        offsetof(struct i386_sigframe, sf_sc);
    signal_frame.sf_sc.sc_onstack = oonstack;
    signal_frame.sf_sc.sc_mask = mask;
    signal_frame.sf_sc.sc_gs = frame->tf_gs;
    signal_frame.sf_sc.sc_fs = frame->tf_fs;
    signal_frame.sf_sc.sc_es = frame->tf_es;
    signal_frame.sf_sc.sc_ds = frame->tf_ds;
    signal_frame.sf_sc.sc_edi = frame->tf_edi;
    signal_frame.sf_sc.sc_esi = frame->tf_esi;
    signal_frame.sf_sc.sc_ebp = frame->tf_ebp;
    signal_frame.sf_sc.sc_ebx = frame->tf_ebx;
    signal_frame.sf_sc.sc_edx = frame->tf_edx;
    signal_frame.sf_sc.sc_ecx = frame->tf_ecx;
    signal_frame.sf_sc.sc_eax = frame->tf_eax;
    signal_frame.sf_sc.sc_eip = frame->tf_eip;
    signal_frame.sf_sc.sc_cs = frame->tf_cs;
    signal_frame.sf_sc.sc_eflags = frame->tf_eflags;
    signal_frame.sf_sc.sc_esp = frame->tf_useresp;
    signal_frame.sf_sc.sc_ss = frame->tf_ss;

    if (copyout((caddr_t)&signal_frame, (caddr_t)frame_address,
        sizeof(signal_frame)) != 0) {
        fatalsig(SIGILL);
        return;
    }

    frame->tf_gs = I386_USER_DATA_SELECTOR;
    frame->tf_fs = I386_USER_DATA_SELECTOR;
    frame->tf_es = I386_USER_DATA_SELECTOR;
    frame->tf_ds = I386_USER_DATA_SELECTOR;
    frame->tf_eip = (unsigned)(unsigned long)handler;
    frame->tf_cs = I386_USER_CODE_SELECTOR;
    frame->tf_eflags = (frame->tf_eflags & I386_EFLAGS_USER_SETTABLE) |
        I386_EFLAGS_RESERVED | I386_EFLAGS_INTERRUPT;
    frame->tf_eflags &= ~I386_EFLAGS_DIRECTION;
    frame->tf_useresp = frame_address;
    frame->tf_ss = I386_USER_DATA_SELECTOR;
}

void
sigreturn(void)
{
    struct i386_trapframe *frame;
    struct sigcontext context;
    struct vmspace *vmspace;
    vm_vaddr_t stack_probe;

    if (md_curuser == (struct user *)0 || u.u_procp == (struct proc *)0 ||
        u.u_frame == (int *)0) {
        u.u_error = EFAULT;
        return;
    }
    frame = (struct i386_trapframe *)u.u_frame;
    if (copyin((caddr_t)(unsigned long)(unsigned)u.u_arg[0],
        (caddr_t)&context, sizeof(context)) != 0) {
        u.u_error = EFAULT;
        return;
    }
    vmspace = u.u_procp->p_vmspace;
    if (vmspace == (struct vmspace *)0 ||
        context.sc_cs != I386_USER_CODE_SELECTOR ||
        context.sc_ss != I386_USER_DATA_SELECTOR ||
        context.sc_ds != I386_USER_DATA_SELECTOR ||
        context.sc_es != I386_USER_DATA_SELECTOR ||
        context.sc_fs != I386_USER_DATA_SELECTOR ||
        context.sc_gs != I386_USER_DATA_SELECTOR ||
        vmspace_check(vmspace, context.sc_eip, 1, VM_PROT_EXECUTE) != 0 ||
        context.sc_esp == 0) {
        u.u_error = EINVAL;
        return;
    }
    stack_probe = context.sc_esp - 1u;
    if (vmspace_check(vmspace, stack_probe, 1, VM_PROT_WRITE) != 0) {
        u.u_error = EINVAL;
        return;
    }

    if ((context.sc_onstack & SA_ONSTACK) != 0)
        u.u_sigstk.ss_flags |= SA_ONSTACK;
    else
        u.u_sigstk.ss_flags &= ~SA_ONSTACK;
    u.u_procp->p_sigmask = context.sc_mask & ~sigcantmask;

    frame->tf_gs = I386_USER_DATA_SELECTOR;
    frame->tf_fs = I386_USER_DATA_SELECTOR;
    frame->tf_es = I386_USER_DATA_SELECTOR;
    frame->tf_ds = I386_USER_DATA_SELECTOR;
    frame->tf_edi = context.sc_edi;
    frame->tf_esi = context.sc_esi;
    frame->tf_ebp = context.sc_ebp;
    frame->tf_ebx = context.sc_ebx;
    frame->tf_edx = context.sc_edx;
    frame->tf_ecx = context.sc_ecx;
    frame->tf_eax = context.sc_eax;
    frame->tf_eip = context.sc_eip;
    frame->tf_cs = I386_USER_CODE_SELECTOR;
    frame->tf_eflags =
        (context.sc_eflags & I386_EFLAGS_USER_SETTABLE) |
        I386_EFLAGS_RESERVED | I386_EFLAGS_INTERRUPT;
    frame->tf_useresp = context.sc_esp;
    frame->tf_ss = I386_USER_DATA_SELECTOR;
    u.u_error = EJUSTRETURN;
}
