/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#ifndef NSIG
#include <sys/types.h>

#define NSIG        33

#define SIGHUP      1       /* hangup */
#define SIGINT      2       /* interrupt */
#define SIGQUIT     3       /* quit */
#define SIGILL      4       /* illegal instruction (not reset when caught) */
#define SIGTRAP     5       /* trace trap (not reset when caught) */
#define SIGIOT      6       /* IOT instruction */
#define SIGABRT     SIGIOT  /* compatibility */
#define SIGEMT      7       /* EMT instruction */
#define SIGFPE      8       /* floating point exception */
#define SIGKILL     9       /* kill (cannot be caught or ignored) */
#define SIGBUS      10      /* bus error */
#define SIGSEGV     11      /* segmentation violation */
#define SIGSYS      12      /* bad argument to system call */
#define SIGPIPE     13      /* write on a pipe with no one to read it */
#define SIGALRM     14      /* alarm clock */
#define SIGTERM     15      /* software termination signal from kill */
#define SIGURG      16      /* urgent condition on IO channel */
#define SIGSTOP     17      /* sendable stop signal not from tty */
#define SIGTSTP     18      /* stop signal from tty */
#define SIGCONT     19      /* continue a stopped process */
#define SIGCHLD     20      /* to parent on child stop or exit */
#define SIGCLD      SIGCHLD /* compatibility */
#define SIGTTIN     21      /* to readers pgrp upon background tty read */
#define SIGTTOU     22      /* like TTIN for output if (tp->t_local&LTOSTOP) */
#define SIGIO       23      /* input/output possible signal */
#define SIGXCPU     24      /* exceeded CPU time limit */
#define SIGXFSZ     25      /* exceeded file size limit */
#define SIGVTALRM   26      /* virtual time alarm */
#define SIGPROF     27      /* profiling time alarm */
#define SIGWINCH    28      /* window size changes */
#define SIGINFO     29      /* information request */
#define SIGUSR1     30      /* user defined signal 1 */
#define SIGUSR2     31      /* user defined signal 2 */
#define SIGPWR      32      /* power fail/restart */

typedef void (*sig_t) (int); /* type of signal function */
typedef int sig_atomic_t;

#define SIG_ERR     (sig_t) -1
#define SIG_DFL     (sig_t) 0
#define SIG_IGN     (sig_t) 1

typedef unsigned long sigset_t;

/*
 * Signal vector "template" used in sigaction call.
 */
struct  sigaction {
    sig_t   sa_handler;         /* signal handler */
    sigset_t sa_mask;           /* signal mask to apply */
    int     sa_flags;           /* see signal options below */
};

#define SA_ONSTACK      0x0001  /* take signal on signal stack */
#define SA_RESTART      0x0002  /* restart system on signal return */
#define SA_DISABLE      0x0004  /* disable taking signals on alternate stack */
#define SA_NOCLDSTOP    0x0008  /* do not generate SIGCHLD on child stop */

/*
 * Flags for sigprocmask:
 */
#define SIG_BLOCK       1       /* block specified signal set */
#define SIG_UNBLOCK     2       /* unblock specified signal set */
#define SIG_SETMASK     3       /* set specified signal set */

/*
 * Structure used in sigaltstack call.
 */
struct  sigaltstack {
    char    *ss_base;           /* signal stack base */
    int     ss_size;            /* signal stack length */
    int     ss_flags;           /* SA_DISABLE and/or SA_ONSTACK */
};
#define MINSIGSTKSZ     128                 /* minimum allowable stack */
#define SIGSTKSZ        (MINSIGSTKSZ + 384) /* recommended stack size */

/*
 * 4.3 compatibility:
 * Signal vector "template" used in sigvec call.
 */
struct  sigvec {
    sig_t   sv_handler;         /* signal handler */
    sigset_t sv_mask;           /* signal mask to apply */
    int     sv_flags;           /* see signal options below */
};
#define SV_ONSTACK      SA_ONSTACK  /* take signal on signal stack */
#define SV_INTERRUPT    SA_RESTART  /* same bit, opposite sense */
#define sv_onstack      sv_flags    /* isn't compatibility wonderful! */

/*
 * 4.3 compatibility:
 * Structure used in sigstack call.
 */
struct  sigstack {
    char    *ss_sp;             /* signal stack pointer */
    int     ss_onstack;         /* current status */
};

/*
 * Information pushed on stack when a signal is delivered.
 * This is used by the kernel to restore state following
 * execution of the signal handler.  It is also made available
 * to the handler to allow it to properly restore state if
 * a non-standard exit is performed.
 */
#ifdef I386
struct  sigcontext {
    int     sc_onstack;         /* sigstack state to restore */
    sigset_t sc_mask;           /* signal mask to restore */
    unsigned sc_gs;
    unsigned sc_fs;
    unsigned sc_es;
    unsigned sc_ds;
    unsigned sc_edi;
    unsigned sc_esi;
    unsigned sc_ebp;
    unsigned sc_ebx;
    unsigned sc_edx;
    unsigned sc_ecx;
    unsigned sc_eax;
    unsigned sc_eip;
    unsigned sc_cs;
    unsigned sc_eflags;
    unsigned sc_esp;
    unsigned sc_ss;
};
#else
struct  sigcontext {
    int     sc_onstack;         /* sigstack state to restore */
    sigset_t sc_mask;           /* signal mask to restore */
    int     sc_r1;              /* r1 to restore */
    int     sc_r2;              /* and other registers */
    int     sc_r3;
    int     sc_r4;
    int     sc_r5;
    int     sc_r6;
    int     sc_r7;
    int     sc_r8;
    int     sc_r9;
    int     sc_r10;
    int     sc_r11;
    int     sc_r12;
    int     sc_r13;
    int     sc_r14;
    int     sc_r15;
    int     sc_r16;
    int     sc_r17;
    int     sc_r18;
    int     sc_r19;
    int     sc_r20;
    int     sc_r21;
    int     sc_r22;
    int     sc_r23;
    int     sc_r24;
    int     sc_r25;
    int     sc_gp;
    int     sc_sp;              /* sp to restore */
    int     sc_fp;
    int     sc_ra;
    int     sc_lo;
    int     sc_hi;
    int     sc_pc;              /* pc to restore */
};
#endif

/*
 * Macro for converting signal number to a mask suitable for
 * sigblock().
 */
#define sigmask(m)              (1UL << ((m)-1))
#ifdef KERNEL
#define sigaddset(set, signo)   (*(set) |= 1UL << ((signo) - 1), 0)
#define sigdelset(set, signo)   (*(set) &= ~(1UL << ((signo) - 1)), 0)
#define sigemptyset(set)        (*(set) = (sigset_t)0, (int)0)
#define sigfillset(set)         (*(set) = ~(sigset_t)0, (int)0)
#define sigismember(set, signo) ((*(set) & (1UL << ((signo) - 1))) != 0)
#endif

#ifdef KERNEL

/* Table of signal properties. */
extern const char sigprop [NSIG + 1];

/*
 * Send an interrupt to process.
 */
void sendsig(sig_t p, int sig, sigset_t mask);

#else /* KERNEL */

sig_t   signal(int, sig_t);
int     sigaction(int signum, const struct sigaction *act,
                   struct sigaction *oldact);
int     sigvec(int sig, struct sigvec *vec, struct sigvec *ovec);
int     kill(pid_t pid, int sig);
int     sigpause(sigset_t mask);
sigset_t sigblock(sigset_t mask);
sigset_t sigsetmask(sigset_t mask);
int     sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int     siginterrupt(int sig, int flag);
int     sigsuspend(const sigset_t *mask);
int     sigaddset(sigset_t *set, int signo);
int     sigdelset(sigset_t *set, int signo);
int     sigemptyset(sigset_t *set);
int     sigfillset(sigset_t *set);
int     sigismember(sigset_t *set, int signo);

#endif /* KERNEL */

#endif /* NSIG */
