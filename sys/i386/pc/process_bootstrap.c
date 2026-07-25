#include <sys/errno.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <vm/vmspace.h>

#include "process.h"
#include "tss.h"
#include "vmspace_bootstrap.h"

static struct proc i386_bootstrap_proc;
static struct user *i386_bootstrap_uarea;
static struct vmspace *i386_bootstrap_vmspace;
static int i386_bootstrap_ready;

int
i386_process_bootstrap_validate(void)
{
    unsigned kernel_stack;

    if (!i386_bootstrap_ready ||
        i386_bootstrap_uarea == (struct user *)0 ||
        i386_bootstrap_vmspace == (struct vmspace *)0)
        return EINVAL;
    kernel_stack = (unsigned)(unsigned long)i386_bootstrap_uarea + USIZE;
    md_uarea_guard_check(i386_bootstrap_uarea);
    if (md_curuser != i386_bootstrap_uarea ||
        i386_bootstrap_uarea->u_procp != &i386_bootstrap_proc ||
        i386_bootstrap_proc.p_uarea != i386_bootstrap_uarea ||
        i386_bootstrap_proc.p_addr !=
        (size_t)(unsigned long)i386_bootstrap_uarea ||
        i386_bootstrap_proc.p_vmspace != i386_bootstrap_vmspace ||
        i386_bootstrap_proc.p_pid != 0 ||
        i386_bootstrap_proc.p_ppid != 0 ||
        i386_bootstrap_proc.p_stat != SRUN ||
        (i386_bootstrap_proc.p_flag & (SLOAD | SSYS)) !=
        (SLOAD | SSYS) ||
        vmspace_current() != i386_bootstrap_vmspace ||
        i386_tss_kernel_stack() != kernel_stack)
        return EFAULT;
    return 0;
}

int
i386_process_bootstrap(void)
{
    struct user *up;
    struct vmspace *vmspace;
    int error;
    int index;

    if (i386_bootstrap_ready)
        return i386_process_bootstrap_validate();
    if (md_curuser != (struct user *)0 ||
        vmspace_current() != (struct vmspace *)0)
        return EBUSY;

    up = md_uarea_alloc();
    if (up == (struct user *)0)
        return ENOMEM;
    vmspace = (struct vmspace *)0;
    error = vmspace_create(&vmspace);
    if (error != 0)
        goto failed;

    bzero(&i386_bootstrap_proc, sizeof(i386_bootstrap_proc));
    i386_bootstrap_proc.p_uarea = up;
    i386_bootstrap_proc.p_addr = (size_t)(unsigned long)up;
    i386_bootstrap_proc.p_vmspace = vmspace;
    i386_bootstrap_proc.p_stat = SRUN;
    i386_bootstrap_proc.p_flag = SLOAD | SSYS;
    i386_bootstrap_proc.p_nice = NZERO;

    up->u_procp = &i386_bootstrap_proc;
    up->u_cmask = CMASK;
    up->u_lastfile = -1;
    for (index = 1; index < NGROUPS; ++index)
        up->u_groups[index] = NOGROUP;
    for (index = 0; index < RLIM_NLIMITS; ++index) {
        up->u_rlimit[index].rlim_cur = RLIM_INFINITY;
        up->u_rlimit[index].rlim_max = RLIM_INFINITY;
    }

    error = i386_vmspace_activate(vmspace);
    if (error != 0)
        goto failed;
    md_curuser = up;
    i386_tss_set_kernel_stack((unsigned)(unsigned long)up + USIZE);
    i386_bootstrap_uarea = up;
    i386_bootstrap_vmspace = vmspace;
    i386_bootstrap_ready = 1;
    return i386_process_bootstrap_validate();

failed:
    if (vmspace != (struct vmspace *)0)
        (void)vmspace_destroy(vmspace);
    md_uarea_free(up);
    return error;
}
