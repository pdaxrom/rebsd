#include <sys/param.h>
#include <sys/signalvar.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>

#include "interrupt.h"
#include "user_return.h"

static struct i386_user_return_ops i386_user_return_test_ops;
static int i386_user_return_test_ops_active;

void
i386_user_return_set_ops(const struct i386_user_return_ops *ops)
{
    if (ops == (const struct i386_user_return_ops *)0) {
        i386_user_return_test_ops_active = 0;
        return;
    }
    i386_user_return_test_ops = *ops;
    i386_user_return_test_ops_active = 1;
}

static int
i386_user_return_next_signal(struct proc *process)
{
    if (i386_user_return_test_ops_active)
        return (*i386_user_return_test_ops.uro_next_signal)(process);
    return CURSIG(process);
}

static void
i386_user_return_deliver_signal(int signum)
{
    if (i386_user_return_test_ops_active)
        (*i386_user_return_test_ops.uro_deliver_signal)(signum);
    else
        postsig(signum);
}

static void
i386_user_return_reschedule(struct proc *process)
{
    int priority;

    if (i386_user_return_test_ops_active) {
        priority =
            (*i386_user_return_test_ops.uro_set_priority)(process);
        if (i386_user_return_test_ops.uro_current_priority != (int *)0)
            *i386_user_return_test_ops.uro_current_priority = priority;
        if (*i386_user_return_test_ops.uro_reschedule == 0)
            return;
        (*i386_user_return_test_ops.uro_enqueue)(process);
        u.u_ru.ru_nivcsw++;
        (*i386_user_return_test_ops.uro_switch)();
        return;
    }

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
}
