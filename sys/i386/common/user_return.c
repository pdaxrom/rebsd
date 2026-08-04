#include <sys/param.h>
#include <sys/signalvar.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>

#include "interrupt.h"
#include "fpu.h"

static int
i386_user_return_next_signal(struct proc *process)
{
    return CURSIG(process);
}

static void
i386_user_return_deliver_signal(int signum)
{
    postsig(signum);
}

static void
i386_user_return_reschedule(struct proc *process)
{
    curpri = (char)setpri(process);
    if (!runrun)
        return;
    setrq(process);
    u.u_ru.ru_nivcsw++;
    swtch();
}

void
i386_user_return(struct i386_trapframe *frame)
{
    struct proc *process;
    int signum;

    if ((frame->tf_cs & 3u) != 3u || md_curuser == (struct user *)0)
        return;
    process = u.u_procp;
    if (process == (struct proc *)0)
        return;

    u.u_frame = (int *)frame;
    __asm__ volatile ("sti" : : : "memory");
    for (;;) {
        signum = i386_user_return_next_signal(process);
        if (signum <= 0)
            break;
        i386_user_return_deliver_signal(signum);
    }
    i386_user_return_reschedule(process);
    __asm__ volatile ("cli" : : : "memory");
    i386_fpu_restore_user(frame);
}
