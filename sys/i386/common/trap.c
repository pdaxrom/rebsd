#include <sys/param.h>
#include <sys/signal.h>
#include <sys/user.h>
#include <sys/proc.h>

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
