/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/map.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/inode.h>
#include <sys/file.h>
#include <sys/vm.h>
#include <sys/kernel.h>
#include <sys/syslog.h>
#include <vm/vmspace.h>

int mpid;                   /* generic for unique process id's */
struct forkstat forkstat;

static void
newproc_fail(struct proc *child)
{
    struct proc **pp;
    struct file *fp;
    int n;

    if (child->p_vmspace != 0) {
        (void)vmspace_destroy(child->p_vmspace);
        child->p_vmspace = 0;
    }
    if (child->p_uarea != 0) {
        md_uarea_free(child->p_uarea);
        child->p_uarea = 0;
    }

    for (n = 0; n <= u.u_lastfile; n++) {
        fp = u.u_ofile[n];
        if (fp != NULL)
            fp->f_count--;
    }
    u.u_cdir->i_count--;
    if (u.u_rdir)
        u.u_rdir->i_count--;

    if ((*child->p_prev = child->p_nxt) != NULL)
        child->p_nxt->p_prev = child->p_prev;
    for (pp = &pidhash[PIDHASH(child->p_pid)]; *pp; pp = &(*pp)->p_hash) {
        if (*pp == child) {
            *pp = child->p_hash;
            break;
        }
    }

    child->p_stat = 0;
    child->p_pid = 0;
    child->p_ppid = 0;
    child->p_pgrp = 0;
    child->p_flag = 0;
    child->p_addr = 0;
    child->p_wchan = 0;
    child->p_sig = 0;
    child->p_sigcatch = 0;
    child->p_sigignore = 0;
    child->p_sigmask = 0;
    child->p_pptr = 0;
    child->p_nxt = freeproc;
    freeproc = child;
}

/*
 * Create a new process -- the internal version of system call fork.
 * It returns 0 in the parent and -1 on failure.  The child starts from an
 * architecture trampoline with a copied user trapframe; it never resumes a
 * copied live C stack.
 */
int
newproc (int isvfork)
{
    register struct proc *child, *parent;
    register int n;
    static int pidchecked = 0;
    struct file *fp;
    int error;

    /*
     * First, just locate a slot for a process
     * and copy the useful info from this process into it.
     * The panic "cannot happen" because fork has already
     * checked for the existence of a slot.
     */
    mpid++;
retry:
    if (mpid >= 30000) {
        mpid = 100;
        pidchecked = 0;
    }
    if (mpid >= pidchecked) {
        int doingzomb = 0;

        pidchecked = 30000;
        /*
         * Scan the proc table to check whether this pid
         * is in use.  Remember the lowest pid that's greater
         * than mpid, so we can avoid checking for a while.
         */
        child = allproc;
again:
        for (; child != NULL; child = child->p_nxt) {
            if (child->p_pid == mpid || child->p_pgrp == mpid) {
                mpid++;
                if (mpid >= pidchecked)
                    goto retry;
            }
            if (child->p_pid > mpid && pidchecked > child->p_pid)
                pidchecked = child->p_pid;
            if (child->p_pgrp > mpid && pidchecked > child->p_pgrp)
                pidchecked = child->p_pgrp;
        }
        if (!doingzomb) {
            doingzomb = 1;
            child = zombproc;
            goto again;
        }
    }
    child = freeproc;
    if (child == NULL)
        panic("no procs");

    freeproc = child->p_nxt;            /* off freeproc */

    /*
     * Make a proc table entry for the new process.
     */
    parent = u.u_procp;
    child->p_stat = SIDL;
    child->p_uarea = 0;
    child->p_vmspace = 0;
    child->p_realtimer.it_value = 0;
    child->p_flag = SLOAD | (parent->p_flag & P_SYSTRACE);
    child->p_uid = parent->p_uid;
    child->p_pgrp = parent->p_pgrp;
    child->p_nice = parent->p_nice;
    child->p_pid = mpid;
    child->p_ppid = parent->p_pid;
    child->p_pptr = parent;
    child->p_time = 0;
    child->p_cpu = 0;
    child->p_sigmask = parent->p_sigmask;
    child->p_sigcatch = parent->p_sigcatch;
    child->p_sigignore = parent->p_sigignore;
    /* take along any pending signals like stops? */
#ifdef UCB_METER
    if (isvfork) {
        forkstat.cntvfork++;
        forkstat.sizvfork += (parent->p_dsize + parent->p_ssize) >> 10;
    } else {
        forkstat.cntfork++;
        forkstat.sizfork += (parent->p_dsize + parent->p_ssize) >> 10;
    }
#endif
    child->p_wchan = 0;
    child->p_slptime = 0;
    {
    struct proc **hash = &pidhash [PIDHASH (child->p_pid)];

    child->p_hash = *hash;
    *hash = child;
    }
    /*
     * some shuffling here -- in most UNIX kernels, the allproc assign
     * is done after grabbing the struct off of the freeproc list.  We
     * wait so that if the clock interrupts us and vmtotal walks allproc
     * the text pointer isn't garbage.
     */
    child->p_nxt = allproc;             /* onto allproc */
    child->p_nxt->p_prev = &child->p_nxt;   /*   (allproc is never NULL) */
    child->p_prev = &allproc;
    allproc = child;

    /*
     * Increase reference counts on shared objects.
     */
    for (n = 0; n <= u.u_lastfile; n++) {
        fp = u.u_ofile[n];
        if (fp == NULL)
            continue;
        fp->f_count++;
    }
    u.u_cdir->i_count++;
    if (u.u_rdir)
        u.u_rdir->i_count++;

    error = vmspace_clone(parent->p_vmspace, &child->p_vmspace);
    if (error != 0) {
        newproc_fail(child);
        return -1;
    }

    child->p_uarea = md_uarea_fork(md_curuser, parent == &proc[0]);
    if (child->p_uarea == 0) {
        newproc_fail(child);
        return -1;
    }
    child->p_uarea->u_procp = child;
    child->p_addr = (size_t)child->p_uarea;
    child->p_dsize = parent->p_dsize;
    child->p_dmin = parent->p_dmin;
    child->p_ssize = parent->p_ssize;
    child->p_daddr = parent->p_daddr;
    child->p_saddr = parent->p_saddr;
    child->p_uarea->u_rval = 0;
    child->p_uarea->u_rval2 = 0;
    child->p_uarea->u_error = 0;
    child->p_uarea->u_start = time.tv_sec;
    bzero(&child->p_uarea->u_ru, sizeof(child->p_uarea->u_ru));
    bzero(&child->p_uarea->u_cru, sizeof(child->p_uarea->u_cru));
    child->p_stat = SRUN;
#ifdef N64_TRACE
    printf ("mipsfork: child ready pid=%d uarea=%x\n",
        child->p_pid, child->p_addr);
#endif
    child->p_flag |= SSWAP;
    setrq(child);
    (void)isvfork;       /* VM vfork is deliberately fork-compatible. */
    return(0);
}

static void
fork1 (int isvfork)
{
    register int a;
    register struct proc *p1, *p2;

    a = 0;
    if (u.u_uid != 0) {
        for (p1 = allproc; p1; p1 = p1->p_nxt)
            if ((uid_t)p1->p_uid == u.u_uid)
                a++;
        for (p1 = zombproc; p1; p1 = p1->p_nxt)
            if ((uid_t)p1->p_uid == u.u_uid)
                a++;
    }
    /*
     * Disallow if
     *  No processes at all;
     *  not su and too many procs owned; or
     *  not su and would take last slot.
     */
    p2 = freeproc;
    if (p2==NULL)
        log(LOG_ERR, "proc: table full\n");

    if (p2==NULL || (u.u_uid!=0 && (p2->p_nxt == NULL || a>MAXUPRC))) {
        u.u_error = EAGAIN;
        return;
    }
    a = newproc (isvfork);
    if (a < 0) {
        u.u_error = ENOMEM;
        return;
    }
    /* The child returns through its trapframe; this is the parent path. */
    u.u_rval = p2->p_pid;
}

/*
 * fork system call
 */
void
fork()
{
    fork1 (0);
}

/*
 * vfork system call, fast version of fork
 */
void
vfork()
{
    fork1 (1);
}
