#include <sys/errno.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/inode.h>
#include <sys/wait.h>

#include "file_bootstrap.h"

void
rexit(void)
{
    struct proc *parent;
    struct proc *process;
    int status;

    process = u.u_procp;
    parent = process->p_pptr;
    status = W_EXITCODE(u.u_arg[0], 0);
    i386_file_bootstrap_close_all();
    if (u.u_cdir != (struct inode *)0 && u.u_cdir->i_count != 0)
        --u.u_cdir->i_count;
    if (proc_zombify(process, status) != 0)
        panic("early exit");
    if (parent != (struct proc *)0 && parent->p_stat == SSLEEP &&
        parent->p_wchan == (caddr_t)parent) {
        parent->p_wchan = 0;
        parent->p_stat = SRUN;
        setrq(parent);
    }
    swtch();
    panic("early exit return");
}

void
wait4(void)
{
    struct proc *child_process;
    struct proc *parent;
    int child;
    int error;
    int status;

    parent = u.u_procp;
    if ((u.u_arg[0] != WAIT_ANY && u.u_arg[0] <= 0) ||
        (u.u_arg[2] & ~WNOHANG) != 0 || u.u_arg[3] != 0) {
        u.u_error = EINVAL;
        return;
    }
    for (;;) {
        error = proc_waitable(parent, u.u_arg[0], &child_process);
        if (error != EWOULDBLOCK)
            break;
        if ((u.u_arg[2] & WNOHANG) != 0) {
            u.u_rval = 0;
            return;
        }
        parent->p_wchan = (caddr_t)parent;
        parent->p_stat = SSLEEP;
        swtch();
    }
    if (error != 0) {
        u.u_error = error;
        return;
    }
    child = child_process->p_pid;
    status = child_process->p_xstat;
    if (u.u_arg[1] != 0 &&
        copyout((caddr_t)&status, (caddr_t)u.u_arg[1],
        sizeof(status)) != 0) {
        u.u_error = EFAULT;
        return;
    }
    error = proc_reap(child_process);
    if (error != 0) {
        u.u_error = error;
        return;
    }
    u.u_rval = child;
}
