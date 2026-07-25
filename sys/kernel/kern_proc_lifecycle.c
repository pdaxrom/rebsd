#include <sys/errno.h>
#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <vm/vmspace.h>

int
proc_zombify(struct proc *process, int status)
{
    struct proc **hash;

    if (process == (struct proc *)0 ||
        process == &proc[0] || process == &proc[1] ||
        process->p_pptr == (struct proc *)0 ||
        process->p_prev == (struct proc **)0 ||
        process->p_stat == SZOMB)
        return EINVAL;

    hash = &pidhash[PIDHASH(process->p_pid)];
    while (*hash != (struct proc *)0 && *hash != process)
        hash = &(*hash)->p_hash;
    if (*hash != process)
        return ESRCH;

    *hash = process->p_hash;
    process->p_hash = (struct proc *)0;
    if ((*process->p_prev = process->p_nxt) != (struct proc *)0)
        process->p_nxt->p_prev = process->p_prev;
    process->p_nxt = zombproc;
    if (process->p_nxt != (struct proc *)0)
        process->p_nxt->p_prev = &process->p_nxt;
    process->p_prev = &zombproc;
    zombproc = process;
    process->p_xstat = status;
    process->p_stat = SZOMB;
    return 0;
}

int
proc_reap(struct proc *parent, int pid, int *status, int *result)
{
    struct proc *process;
    int child_found;
    int error;

    if (parent == (struct proc *)0 || result == (int *)0)
        return EINVAL;

    child_found = 0;
    for (process = zombproc; process != (struct proc *)0;
        process = process->p_nxt) {
        if (process->p_pptr != parent)
            continue;
        if (pid != -1 && process->p_pid != pid)
            continue;
        if (process->p_vmspace != (struct vmspace *)0) {
            error = vmspace_destroy(process->p_vmspace);
            if (error != 0)
                return error;
            process->p_vmspace = (struct vmspace *)0;
        }
        if (status != (int *)0)
            *status = process->p_xstat;
        *result = process->p_pid;
        if (process->p_uarea != (struct user *)0) {
            md_uarea_free(process->p_uarea);
            process->p_uarea = (struct user *)0;
        }
        if ((*process->p_prev = process->p_nxt) != (struct proc *)0)
            process->p_nxt->p_prev = process->p_prev;
        bzero(process, sizeof(*process));
        process->p_nxt = freeproc;
        freeproc = process;
        return 0;
    }

    for (process = allproc; process != (struct proc *)0;
        process = process->p_nxt) {
        if (process->p_pptr == parent &&
            (pid == -1 || process->p_pid == pid)) {
            child_found = 1;
            break;
        }
    }
    return child_found ? EWOULDBLOCK : ECHILD;
}
