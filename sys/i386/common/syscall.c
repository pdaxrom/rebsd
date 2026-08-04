#include <sys/errno.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>

#include "interrupt.h"
#include "syscall.h"

static const struct sysent *i386_syscall_table;
static unsigned i386_syscall_table_count;

void
i386_syscall_set_table(const struct sysent *table, unsigned count)
{
    if (table == (const struct sysent *)0 || count == 0) {
        i386_syscall_table = (const struct sysent *)0;
        i386_syscall_table_count = 0;
        return;
    }
    i386_syscall_table = table;
    i386_syscall_table_count = count;
}

void
i386_syscall_get_table(const struct sysent **table, unsigned *count)
{
    *table = i386_syscall_table;
    *count = i386_syscall_table_count;
}

int
i386_syscall_install_production(void)
{
    if (nsysent <= 177 || sysent[1].sy_call != rexit ||
        sysent[2].sy_call != fork || sysent[3].sy_call != read ||
        sysent[5].sy_call != open || sysent[6].sy_call != close ||
        sysent[7].sy_call != wait4 || sysent[11].sy_call != execv ||
        sysent[19].sy_call != lseek || sysent[20].sy_call != getpid ||
        sysent[59].sy_call != execve || sysent[162].sy_call != mmap ||
        sysent[175].sy_call != shmctl || sysent[177].sy_call != pwrite)
        return EINVAL;
    i386_syscall_set_table(sysent, (unsigned)nsysent);
    return 0;
}

static void
i386_syscall_error(struct i386_trapframe *frame, int error)
{
    frame->tf_eax = (unsigned)error;
    /*
     * libc turns EAX into -1 after saving errno.  Keep the secondary
     * return word negative as well so 64-bit syscall results become the
     * required (off_t)-1 instead of 0x00000000ffffffff.
     */
    frame->tf_edx = (unsigned)-1;
    frame->tf_eflags |= I386_EFLAGS_CARRY;
}

void
i386_syscall_dispatch(struct i386_trapframe *frame)
{
    const struct sysent *callp;
    unsigned code;
    unsigned arg_count;

    if ((frame->tf_cs & 3u) != 3u || md_curuser == (struct user *)0 ||
        i386_syscall_table == (const struct sysent *)0) {
        i386_syscall_error(frame, ENOSYS);
        return;
    }

    code = frame->tf_eax;
    if (code >= i386_syscall_table_count) {
        i386_syscall_error(frame, ENOSYS);
        return;
    }
    callp = &i386_syscall_table[code];
    arg_count = sizeof(u.u_arg) / sizeof(u.u_arg[0]);
    if (callp->sy_narg < 0 || (unsigned)callp->sy_narg > arg_count ||
        (unsigned)callp->sy_narg > I386_SYSCALL_MAX_ARGS) {
        i386_syscall_error(frame, EINVAL);
        return;
    }

    for (code = 0; code < arg_count; ++code)
        u.u_arg[code] = 0;
    if (callp->sy_narg > 0)
        u.u_arg[0] = frame->tf_ebx;
    if (callp->sy_narg > 1)
        u.u_arg[1] = frame->tf_ecx;
    if (callp->sy_narg > 2)
        u.u_arg[2] = frame->tf_edx;
    if (callp->sy_narg > 3)
        u.u_arg[3] = frame->tf_esi;
    if (callp->sy_narg > 4)
        u.u_arg[4] = frame->tf_edi;
    if (callp->sy_narg > 5)
        u.u_arg[5] = frame->tf_ebp;
    if (callp->sy_narg > 6 &&
        copyin((caddr_t)(unsigned long)frame->tf_useresp,
        (caddr_t)&u.u_arg[6],
        (unsigned)(callp->sy_narg - 6) * sizeof(u.u_arg[0])) != 0) {
        i386_syscall_error(frame, EFAULT);
        return;
    }

    u.u_frame = (int *)frame;
    u.u_rval = 0;
    u.u_rval2 = 0;
    u.u_error = 0;
    if (setjmp(&u.u_qsave) == 0)
        (*callp->sy_call)();

    switch (u.u_error) {
    case 0:
        frame->tf_eax = (unsigned)u.u_rval;
        frame->tf_edx = (unsigned)u.u_rval2;
        frame->tf_eflags &= ~I386_EFLAGS_CARRY;
        break;
    case ERESTART:
        if (frame->tf_eip < 2u)
            i386_syscall_error(frame, EFAULT);
        else
            frame->tf_eip -= 2u;
        break;
    case EJUSTRETURN:
        break;
    default:
        i386_syscall_error(frame,
            u.u_error > 0 ? u.u_error : EINVAL);
        break;
    }
}
