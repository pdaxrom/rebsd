#include <sys/param.h>
#include <sys/errno.h>
#include <sys/signalvar.h>
#include <sys/user.h>
#include <sys/systm.h>

long dumplo;
extern volatile unsigned int ct_ticks;
struct proc;
void psignal(struct proc *p, int sig);

void
kmemdev(void)
{
    /*
     * Keep the historical syscall entry present, but do not publish /dev/kmem
     * on N64. Direct kernel memory access should not become a default ABI.
     */
    u.u_rval = NODEV;
}

void
nosys(void)
{
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

    if (pc < (caddr_t)prof->pr_off)
        return;

    indx = pc - (caddr_t)prof->pr_off;
    indx = (indx * prof->pr_scale) >> 16;
    if (indx >= prof->pr_size)
        return;

    prof->pr_base[indx] += ticks;
}
