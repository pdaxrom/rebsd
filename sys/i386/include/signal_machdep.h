#ifndef _I386_SIGNAL_MACHDEP_H_
#define _I386_SIGNAL_MACHDEP_H_

#include <sys/signal.h>

/*
 * cdecl entry stack for a signal handler:
 *   return address, signal number, code, sigcontext pointer.
 */
struct i386_sigframe {
    unsigned sf_return;
    int sf_signum;
    int sf_code;
    unsigned sf_context;
    struct sigcontext sf_sc;
};

int i386_signal_selftest(void);

#endif
