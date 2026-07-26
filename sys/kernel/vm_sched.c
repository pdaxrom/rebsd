/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/vm.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/debug.h>
#include <vm/vm_object.h>
#ifdef USB_ENABLED
#include <usb/usb_task.h>
#endif

#define MINFINITY   -32767      /* minus infinity */

int     maxslp = MAXSLP;
char    runin;                  /* scheduling flag */
char    runout;                 /* scheduling flag */
u_int   swapstart, nswap;       /* start and size of swap space */
size_t  physmem;                /* total physical memory in bytes */
size_t  freemem;                /* remaining clicks of free memory */
short avenrun[3];
struct vmtotal total;
struct vmsum   sum;
struct vmrate  rate;
u_short avefree;                /* moving average of remaining free clicks */
u_short avefree30;              /* 30 sec (avefree is 5 sec) moving average */

/*
 * Process zero supplies the idle scheduler context and deferred USB work.
 * VM-backed processes remain resident; the object pager will perform
 * page-level reclamation instead of fixed-window whole-process swapping.
 */
void
sched()
{
    for (;;) {
        spl0();
        (void)vm_pager_pageout_scan();
#ifdef USB_ENABLED
        usb_task_run_pending();
#endif
        splhigh();
        ++runout;
#ifdef USB_ENABLED
        if (usb_task_any_pending())
            continue;
#endif
        sleep ((caddr_t)&runout, PSWP);
    }
}

/*
 * Count up various things once a second
 */
void
vmmeter()
{
#ifdef UCB_METER
    register u_short *cp, *rp;
    register long *sp;

    ave(avefree, freemem, 5);
    ave(avefree30, freemem, 30);
    cp = &cnt.v_first;
    rp = &rate.v_first;
    sp = &sum.v_first;
    while (cp <= &cnt.v_last) {
        ave(*rp, *cp, 5);
        *sp += *cp;
        *cp = 0;
        rp++, cp++, sp++;
    }
#endif

    if (time.tv_sec % 5 == 0) {
        vmtotal();
#ifdef UCB_METER
        rate.v_swpin = cnt.v_swpin;
        sum.v_swpin += cnt.v_swpin;
        cnt.v_swpin = 0;
        rate.v_swpout = cnt.v_swpout;
        sum.v_swpout += cnt.v_swpout;
        cnt.v_swpout = 0;
#endif
    }
}

/*
 * Compute Tenex style load average.  This code is adapted from similar code
 * by Bill Joy on the Vax system.  The major change is that we avoid floating
 * point since not all pdp-11's have it.  This makes the code quite hard to
 * read - it was derived with some algebra.
 *
 * "floating point" numbers here are stored in a 16 bit short, with 8 bits on
 * each side of the decimal point.  Some partial products will have 16 bits to
 * the right.
 */
static void
loadav (short *avg, int n)
{
    register int    i;
    static const long cexp[3] = {
        0353,   /* 256 * exp(-1/12)  */
        0373,   /* 256 * exp(-1/60)  */
        0376,   /* 256 * exp(-1/180) */
    };

    for (i = 0; i < 3; i++)
        avg[i] = (cexp[i] * (avg[i]-(n<<8)) + (((long)n)<<16)) >> 8;
}

void
vmtotal()
{
    register struct proc *p;
    register int nrun = 0;
#ifdef UCB_METER
    total.t_vmtxt = 0;
    total.t_avmtxt = 0;
    total.t_rmtxt = 0;
    total.t_armtxt = 0;
    total.t_vm = 0;
    total.t_avm = 0;
    total.t_rm = 0;
    total.t_arm = 0;
    total.t_rq = 0;
    total.t_dw = 0;
    total.t_sl = 0;
    total.t_sw = 0;
#endif
    for (p = allproc; p != NULL; p = p->p_nxt) {
        if (p->p_flag & SSYS)
            continue;
        if (p->p_stat) {
#ifdef UCB_METER
            if (p->p_stat != SZOMB) {
                total.t_vm += p->p_dsize + p->p_ssize + USIZE;
                if (p->p_flag & SLOAD)
                    total.t_rm += p->p_dsize + p->p_ssize
                        + USIZE;
            }
#endif
            switch (p->p_stat) {

            case SSLEEP:
            case SSTOP:
                if (!(p->p_flag & P_SINTR) && p->p_stat == SSLEEP)
                    nrun++;
#ifdef UCB_METER
                if (p->p_flag & SLOAD) {
                    if  (!(p->p_flag & P_SINTR))
                        total.t_dw++;
                    else if (p->p_slptime < maxslp)
                        total.t_sl++;
                } else if (p->p_slptime < maxslp)
                    total.t_sw++;
                if (p->p_slptime < maxslp)
                    goto active;
#endif
                break;

            case SRUN:
            case SIDL:
                nrun++;
#ifdef UCB_METER
                if (p->p_flag & SLOAD)
                    total.t_rq++;
                else
                    total.t_sw++;
active:
                total.t_avm += p->p_dsize + p->p_ssize + USIZE;
                if (p->p_flag & SLOAD)
                    total.t_arm += p->p_dsize + p->p_ssize
                        + USIZE;
#endif
                break;
            }
        }
    }
#ifdef UCB_METER
    total.t_vm += total.t_vmtxt;
    total.t_avm += total.t_avmtxt;
    total.t_rm += total.t_rmtxt;
    total.t_arm += total.t_armtxt;
    total.t_free = avefree;
#endif
    loadav (avenrun, nrun);
}
