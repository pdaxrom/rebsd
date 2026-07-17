#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/signal.h>
#include <sys/signalvar.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <machine/io.h>
#include <vm/vmspace.h>

void
sendsig(sig_t p, int sig, long mask)
{
    struct sigframe {
        int     sf_space[4];
        struct  sigcontext sf_sc;
    };
    int *regs = u.u_frame;
    struct sigframe local_frame;
    struct sigframe *sfp = &local_frame;
    struct sigframe *user_sfp;
    vm_vaddr_t guard_end;
    int oonstack;

    bzero((caddr_t)&local_frame, sizeof(local_frame));

    oonstack = u.u_sigstk.ss_flags & SA_ONSTACK;

    if ((u.u_psflags & SAS_ALTSTACK) &&
        !(u.u_sigstk.ss_flags & SA_ONSTACK) &&
        (u.u_sigonstack & sigmask(sig))) {
        user_sfp = (struct sigframe *)(u.u_sigstk.ss_base +
            u.u_sigstk.ss_size);
        u.u_sigstk.ss_flags |= SA_ONSTACK;
    } else {
        user_sfp = (struct sigframe *)regs[FRAME_SP];
    }

    user_sfp--;
    if (!(u.u_sigstk.ss_flags & SA_ONSTACK)) {
        if (vm_vaddr_round_page(u.u_procp->p_daddr + u.u_dsize,
            &guard_end) != 0 || guard_end > VM_VADDR_MAX - VM_PAGE_SIZE) {
            fatalsig(SIGILL);
            return;
        }
        guard_end += VM_PAGE_SIZE;
        if ((vm_vaddr_t)user_sfp < guard_end ||
            vmspace_grow_stack(u.u_procp->p_vmspace,
            u.u_procp->p_saddr, (vm_vaddr_t)user_sfp,
            guard_end) != 0) {
            fatalsig(SIGILL);
            return;
        }
        if (u.u_procp->p_ssize < USER_DATA_END - (unsigned)user_sfp) {
            u.u_procp->p_ssize = USER_DATA_END - (unsigned)user_sfp;
            u.u_procp->p_saddr = (unsigned)user_sfp;
            u.u_ssize = u.u_procp->p_ssize;
        }
    }

    sfp->sf_sc.sc_onstack = oonstack;
    sfp->sf_sc.sc_mask = mask;
    sfp->sf_sc.sc_r1  = regs[FRAME_R1];
    sfp->sf_sc.sc_r2  = regs[FRAME_R2];
    sfp->sf_sc.sc_r3  = regs[FRAME_R3];
    sfp->sf_sc.sc_r4  = regs[FRAME_R4];
    sfp->sf_sc.sc_r5  = regs[FRAME_R5];
    sfp->sf_sc.sc_r6  = regs[FRAME_R6];
    sfp->sf_sc.sc_r7  = regs[FRAME_R7];
    sfp->sf_sc.sc_r8  = regs[FRAME_R8];
    sfp->sf_sc.sc_r9  = regs[FRAME_R9];
    sfp->sf_sc.sc_r10 = regs[FRAME_R10];
    sfp->sf_sc.sc_r11 = regs[FRAME_R11];
    sfp->sf_sc.sc_r12 = regs[FRAME_R12];
    sfp->sf_sc.sc_r13 = regs[FRAME_R13];
    sfp->sf_sc.sc_r14 = regs[FRAME_R14];
    sfp->sf_sc.sc_r15 = regs[FRAME_R15];
    sfp->sf_sc.sc_r16 = regs[FRAME_R16];
    sfp->sf_sc.sc_r17 = regs[FRAME_R17];
    sfp->sf_sc.sc_r18 = regs[FRAME_R18];
    sfp->sf_sc.sc_r19 = regs[FRAME_R19];
    sfp->sf_sc.sc_r20 = regs[FRAME_R20];
    sfp->sf_sc.sc_r21 = regs[FRAME_R21];
    sfp->sf_sc.sc_r22 = regs[FRAME_R22];
    sfp->sf_sc.sc_r23 = regs[FRAME_R23];
    sfp->sf_sc.sc_r24 = regs[FRAME_R24];
    sfp->sf_sc.sc_r25 = regs[FRAME_R25];
    sfp->sf_sc.sc_gp  = regs[FRAME_GP];
    sfp->sf_sc.sc_sp  = regs[FRAME_SP];
    sfp->sf_sc.sc_fp  = regs[FRAME_FP];
    sfp->sf_sc.sc_ra  = regs[FRAME_RA];
    sfp->sf_sc.sc_lo  = regs[FRAME_LO];
    sfp->sf_sc.sc_hi  = regs[FRAME_HI];
    sfp->sf_sc.sc_pc  = regs[FRAME_PC];

    if (copyout((caddr_t)sfp, (caddr_t)user_sfp,
        sizeof(*sfp)) != 0) {
        fatalsig(SIGILL);
        return;
    }

    regs[FRAME_R4] = sig;
    regs[FRAME_R5] = u.u_code;
    regs[FRAME_R6] = (int)&user_sfp->sf_sc;
    regs[FRAME_RA] = (int)u.u_sigtramp;
    regs[FRAME_SP] = (int)user_sfp;
    regs[FRAME_PC] = (int)p;
}

void
sigreturn(void)
{
    int *regs = u.u_frame;
    struct sigcontext context;
    struct sigcontext *user_scp =
        (struct sigcontext *)(regs[FRAME_SP] + 16);
    struct sigcontext *scp = &context;

    if (copyin((caddr_t)user_scp, (caddr_t)scp, sizeof(*scp)) != 0) {
        u.u_error = EFAULT;
        return;
    }

    u.u_error = EJUSTRETURN;
    if (scp->sc_onstack & SA_ONSTACK)
        u.u_sigstk.ss_flags |= SA_ONSTACK;
    else
        u.u_sigstk.ss_flags &= ~SA_ONSTACK;
    u.u_procp->p_sigmask = scp->sc_mask & ~sigcantmask;

    regs[FRAME_R1] = scp->sc_r1;
    regs[FRAME_R2] = scp->sc_r2;
    regs[FRAME_R3] = scp->sc_r3;
    regs[FRAME_R4] = scp->sc_r4;
    regs[FRAME_R5] = scp->sc_r5;
    regs[FRAME_R6] = scp->sc_r6;
    regs[FRAME_R7] = scp->sc_r7;
    regs[FRAME_R8] = scp->sc_r8;
    regs[FRAME_R9] = scp->sc_r9;
    regs[FRAME_R10] = scp->sc_r10;
    regs[FRAME_R11] = scp->sc_r11;
    regs[FRAME_R12] = scp->sc_r12;
    regs[FRAME_R13] = scp->sc_r13;
    regs[FRAME_R14] = scp->sc_r14;
    regs[FRAME_R15] = scp->sc_r15;
    regs[FRAME_R16] = scp->sc_r16;
    regs[FRAME_R17] = scp->sc_r17;
    regs[FRAME_R18] = scp->sc_r18;
    regs[FRAME_R19] = scp->sc_r19;
    regs[FRAME_R20] = scp->sc_r20;
    regs[FRAME_R21] = scp->sc_r21;
    regs[FRAME_R22] = scp->sc_r22;
    regs[FRAME_R23] = scp->sc_r23;
    regs[FRAME_R24] = scp->sc_r24;
    regs[FRAME_R25] = scp->sc_r25;
    regs[FRAME_GP] = scp->sc_gp;
    regs[FRAME_SP] = scp->sc_sp;
    regs[FRAME_FP] = scp->sc_fp;
    regs[FRAME_RA] = scp->sc_ra;
    regs[FRAME_LO] = scp->sc_lo;
    regs[FRAME_HI] = scp->sc_hi;
    regs[FRAME_PC] = scp->sc_pc;
}
