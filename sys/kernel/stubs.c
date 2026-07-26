#include <sys/param.h>
#include <sys/errno.h>
#include <sys/conf.h>
#include <sys/signalvar.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/systm.h>
#if defined(MIPS) || defined(N64)
#include <machine/io.h>
#endif

long dumplo;
struct proc;
void psignal(struct proc *p, int sig);

void
kmemdev(void)
{
    u.u_rval = makedev(MEM_MAJOR, 1);
}

void
nosys(void)
{
#if defined(MIPS) || defined(N64)
    int *frame = u.u_frame;

    if (frame) {
        unsigned pc = frame[FRAME_PC];
        unsigned inst = 0;
        unsigned code = ~0;

        if (copyin((caddr_t)pc, (caddr_t)&inst, sizeof(inst)) == 0) {
            code = (inst >> 6) & 0377;
        }
        uprintf("nosys: pid=%d comm=%s code=%u pc=%x sp=%x "
            "a0=%x a1=%x a2=%x a3=%x inst=%x\n",
            u.u_procp ? u.u_procp->p_pid : -1, u.u_comm, code, pc,
            frame[FRAME_SP], frame[FRAME_R4], frame[FRAME_R5],
            frame[FRAME_R6], frame[FRAME_R7], inst);
    }
#endif
    if (u.u_signal[SIGSYS] == SIG_IGN || u.u_signal[SIGSYS] == SIG_HOLD)
        u.u_error = EINVAL;
    psignal(u.u_procp, SIGSYS);
}

void
sc_msec(void)
{
    u.u_rval = ct_ticks * (1000 / HZ);
}

void
addupc(caddr_t pc, struct uprof *prof, int ticks)
{
    unsigned indx;
    unsigned value;

    if (pc < (caddr_t)prof->pr_off)
        return;

    indx = pc - (caddr_t)prof->pr_off;
    indx = (indx * prof->pr_scale) >> 16;
    if (indx >= prof->pr_size)
        return;

    if (copyin((caddr_t)&prof->pr_base[indx], (caddr_t)&value,
        sizeof(value)) != 0)
        return;
    value += ticks;
    (void)copyout((caddr_t)&value, (caddr_t)&prof->pr_base[indx],
        sizeof(value));
}
