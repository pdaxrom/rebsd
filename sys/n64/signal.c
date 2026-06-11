#include <sys/param.h>
#include <sys/errno.h>
#include <sys/signal.h>
#include <sys/signalvar.h>
#include <sys/user.h>
#include <sys/proc.h>

void
sendsig(sig_t p, int sig, long mask)
{
    fatalsig(sig);
}

void
sigreturn(void)
{
    u.u_error = ENOSYS;
}
