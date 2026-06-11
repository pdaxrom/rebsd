#include <sys/param.h>
#include <sys/errno.h>
#include <sys/user.h>
#include <sys/systm.h>

long dumplo;

void
kmemdev(void)
{
    u.u_rval = NODEV;
}

void
nosys(void)
{
    u.u_error = ENOSYS;
}

void
sc_msec(void)
{
    u.u_rval = 0;
}

void
addupc(caddr_t pc, struct uprof *prof, int ticks)
{
}
