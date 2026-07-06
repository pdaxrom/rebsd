/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/map.h>
#include <sys/buf.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/vm.h>
#ifdef N64_ZSWAP
#include <machine/ramswap.h>
#endif

static void
swap_release_range(size_t addr, u_int bytes)
{
    size_t blocks;

    blocks = btod(bytes);
    if (blocks == 0)
        return;
#ifdef N64_ZSWAP
    n64zswap_free(addr, blocks);
#endif
    mfree(swapmap, blocks, addr);
}

static void
swap_release_image(size_t a[3], u_int dsize, u_int ssize)
{
    swap_release_range(a[0], dsize);
    swap_release_range(a[1], ssize);
    swap_release_range(a[2], USIZE);
}

int
swapout_possible(u_int dsize, u_int ssize)
{
    struct mapent tmpent[SMAPSIZ];
    struct map tmpmap;
    size_t a[3];

    if (nswap == 0)
        return 1;

    bcopy(swapmap[0].m_map, tmpent, sizeof(tmpent));
    tmpmap.m_map = tmpent;
    tmpmap.m_limit = &tmpent[SMAPSIZ];
    tmpmap.m_name = swapmap[0].m_name;
    return malloc3(&tmpmap, btod(dsize), btod(ssize), btod(USIZE), a) != 0;
}

/*
 * Swap a process in.
 * Allocate data and possible text separately.  It would be better
 * to do largest first.  Text, data, and stack are allocated in
 * that order, as that is likely to be in order of size.
 * U area goes into u0 buffer.
 */
void
swapin (struct proc *p)
{
    size_t daddr = USER_DATA_START;
    size_t saddr = USER_DATA_END - p->p_ssize;
    size_t uaddr = (size_t) &u0;

#ifdef N64_TRACE
    printf ("n64swapin: pid=%d d=%x/%u s=%x/%u u=%x\n",
        p->p_pid, p->p_daddr, p->p_dsize, p->p_saddr, p->p_ssize,
        p->p_addr);
#endif
    if (p->p_dsize) {
        if (swap (p->p_daddr, daddr, p->p_dsize, B_READ) != 0)
            panic ("hard err: swap");
        swap_release_range(p->p_daddr, p->p_dsize);
    }
    if (p->p_ssize) {
        if (swap (p->p_saddr, saddr, p->p_ssize, B_READ) != 0)
            panic ("hard err: swap");
        swap_release_range(p->p_saddr, p->p_ssize);
    }
    if (swap (p->p_addr, uaddr, USIZE, B_READ) != 0)
        panic ("hard err: swap");
    swap_release_range(p->p_addr, USIZE);

    p->p_daddr = daddr;
    p->p_saddr = saddr;
    p->p_addr = uaddr;
    if (p->p_stat == SRUN)
        setrq (p);
    p->p_flag |= SLOAD;
    p->p_time = 0;
#ifdef N64_TRACE
    printf ("n64swapin: done pid=%d paddr=%x flag=%x\n",
        p->p_pid, p->p_addr, p->p_flag);
#endif
#ifdef UCB_METER
    cnt.v_swpin++;
#endif
}

/*
 * Swap out process p.
 * odata and ostack are the old data size and the stack size
 * of the process, and are supplied during core expansion swaps.
 * The freecore flag causes its core to be freed -- it may be
 * off when called to create an image for a child process
 * in newproc.
 *
 * panic: out of swap space
 */
int
swapout (struct proc *p, int freecore, u_int odata, u_int ostack)
{
    size_t a[3];
    int error;

    if (odata == (u_int) X_OLDSIZE)
        odata = p->p_dsize;
    if (ostack == (u_int) X_OLDSIZE)
        ostack = p->p_ssize;
    if (malloc3 (swapmap, btod (p->p_dsize), btod (p->p_ssize),
        btod (USIZE), a) == NULL)
        return ENOMEM;
#ifdef N64_TRACE
    printf ("n64swapout: pid=%d d=%x/%u s=%x/%u u=%x -> %x,%x,%x\n",
        p->p_pid, p->p_daddr, p->p_dsize, p->p_saddr, p->p_ssize,
        p->p_addr, a[0], a[1], a[2]);
#endif
    p->p_flag |= SLOCK;
    if (odata) {
        error = swap (a[0], p->p_daddr, odata, B_WRITE);
        if (error != 0)
            goto fail;
    }
    if (ostack) {
        error = swap (a[1], p->p_saddr, ostack, B_WRITE);
        if (error != 0)
            goto fail;
    }
    error = swap (a[2], p->p_addr, USIZE, B_WRITE);
    if (error != 0)
        goto fail;
    /*
     * Increment u_ru.ru_nswap for process being tossed out of core.
     * We can be called to swap out a process other than the current
     * process, so we have to map in the victim's u structure briefly.
     * Note, savekdsa6 *must* be a static, because we remove the stack
     * in the next instruction.  The splclock is to prevent the clock
     * from coming in and doing accounting for the wrong process, plus
     * we don't want to come through here twice.  Why are we doing
     * this, anyway?
     */
    {
        int s;

        s = splclock();
        u.u_ru.ru_nswap++;
        splx (s);
    }
    p->p_daddr = a[0];
    p->p_saddr = a[1];
    p->p_addr = a[2];
    p->p_flag &= ~(SLOAD|SLOCK);
    p->p_time = 0;
#ifdef N64_TRACE
    printf ("n64swapout: done pid=%d paddr=%x flag=%x\n",
        p->p_pid, p->p_addr, p->p_flag);
#endif

#ifdef UCB_METER
    cnt.v_swpout++;
#endif
    if (runout) {
        runout = 0;
        wakeup ((caddr_t)&runout);
    }
    return 0;

fail:
    p->p_flag &= ~SLOCK;
    swap_release_image(a, p->p_dsize, p->p_ssize);
    return error;
}
