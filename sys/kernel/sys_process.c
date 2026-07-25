/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/inode.h>
#include <sys/vm.h>
#include <sys/ptrace.h>

struct ipc ipc;

/*
 * sys-trace system call.
 */
void
ptrace()
{
    register struct proc *p;
    register struct a {
        int req;
        int pid;
        int *addr;
        int data;
    } *uap;

    uap = (struct a *)u.u_arg;
    if (uap->req == PT_SYSCALL_TRACE) {
        if (uap->pid != 0 && uap->pid != u.u_procp->p_pid) {
            u.u_error = EINVAL;
            return;
        }
        if (uap->data)
            u.u_procp->p_flag |= P_SYSTRACE;
        else
            u.u_procp->p_flag &= ~P_SYSTRACE;
        return;
    }
    if (uap->req <= 0) {
        u.u_procp->p_flag |= P_TRACED;
        return;
    }
    p = pfind(uap->pid);
    if (p == 0 || p->p_stat != SSTOP || p->p_ppid != u.u_procp->p_pid ||
        !(p->p_flag & P_TRACED)) {
        u.u_error = ESRCH;
        return;
    }
    while (ipc.ip_lock)
        sleep((caddr_t)&ipc, PZERO);
    ipc.ip_lock = p->p_pid;
    ipc.ip_data = uap->data;
    ipc.ip_addr = uap->addr;
    ipc.ip_req = uap->req;
    p->p_flag &= ~P_WAITED;
    setrun(p);
    while (ipc.ip_req > 0)
        sleep((caddr_t)&ipc, PZERO);
    u.u_rval = ipc.ip_data;
    if (ipc.ip_req < 0)
        u.u_error = EIO;
    ipc.ip_lock = 0;
    wakeup((caddr_t)&ipc);
}

/*
 * Code that the child process
 * executes to implement the command
 * of the parent process in tracing.
 */
int
procxmt()
{
    register int i, *p;

    if (ipc.ip_lock != u.u_procp->p_pid)
        return(0);
    u.u_procp->p_slptime = 0;
    i = ipc.ip_req;
    ipc.ip_req = 0;
    wakeup ((caddr_t)&ipc);
    switch (i) {

    /* read user I */
    case PT_READ_I:

    /* read user D */
    case PT_READ_D:
        if (copyin((caddr_t)ipc.ip_addr, (caddr_t)&ipc.ip_data,
            sizeof(ipc.ip_data)) != 0)
            goto error;
        break;

    /* read u */
    case PT_READ_U:
        i = (int) ipc.ip_addr;
        if (i < 0 || i >= USIZE)
            goto error;
        ipc.ip_data = ((unsigned*)&u) [i/sizeof(int)];
        break;

    /* write user I */
    case PT_WRITE_I:
    /* write user D */
    case PT_WRITE_D:
        if (copyout((caddr_t)&ipc.ip_data, (caddr_t)ipc.ip_addr,
            sizeof(ipc.ip_data)) != 0)
            goto error;
        break;

    /* write u */
    case PT_WRITE_U:
        i = (int)ipc.ip_addr;
        p = (int*)&u + i/sizeof(int);
        if (md_user_frame_write(u.u_frame, p, ipc.ip_data) != 0)
            goto error;
        break;

    /* set signal and continue */
    /* one version causes a trace-trap */
    case PT_STEP:
        if (md_user_frame_single_step(u.u_frame) != 0)
            goto error;
        /* FALL THROUGH TO ... */
    case PT_CONTINUE:
        if ((int)ipc.ip_addr != 1 &&
            md_user_frame_set_pc(u.u_frame, (unsigned)ipc.ip_addr) != 0)
            goto error;
        if (ipc.ip_data > NSIG)
            goto error;
        u.u_procp->p_ptracesig = ipc.ip_data;
        return(1);

    /* force exit */
    case PT_KILL:
        exit(u.u_procp->p_ptracesig);
        /*NOTREACHED*/

    default:
error:
        ipc.ip_req = -1;
    }
    return(0);
}
