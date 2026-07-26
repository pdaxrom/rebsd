/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <sys/param.h>
#include <sys/user.h>
#include <sys/fs.h>
#include <sys/mount.h>
#include <sys/map.h>
#include <sys/proc.h>
#include <sys/ioctl.h>
#include <sys/inode.h>
#include <sys/conf.h>
#include <sys/buf.h>
#include <sys/fcntl.h>
#include <sys/vm.h>
#include <sys/clist.h>
#include <sys/reboot.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/namei.h>
#include <sys/stat.h>
#include <sys/kconfig.h>
#include <vm/vm_phys.h>
#include <vm/vm_page.h>
#include <vm/pmap.h>
#include <vm/vm_object.h>
#include <vm/vmspace.h>
#ifdef N64
#include <machine/video.h>
#endif

u_int   swapstart, nswap;   /* start and size of swap space */
size_t  physmem;            /* total amount of physical memory */
int     boothowto;          /* reboot flags, from boot */

/*
 * Initialize clist by freeing all character blocks, then count
 * number of character devices. (Once-only routine)
 */
static void
cinit()
{
    register int ccp;
    register struct cblock *cp;

    ccp = (int)cfree;
    ccp = (ccp + CROUND) & ~CROUND;
    for (cp = (struct cblock *)ccp; cp <= &cfree[NCLIST - 1]; cp++) {
        cp->c_next = cfreelist;
        cfreelist = cp;
        cfreecount += CBSIZE;
    }
}

/*
 * Finish constructing process 1 on its fresh kernel stack, then enter the
 * small user bootstrap which execs /sbin/init.  This is a separate entry
 * point so process creation never has to copy main()'s live C stack.
 */
void
md_init_process(void)
{
    struct proc *p;
    vm_vaddr_t data_start;
    vm_vaddr_t data_end;
    vm_vaddr_t stack_start;

#if defined(N64_TRACE) || defined(MIPS_TRACE)
    printf ("mipsboot: proc1 trampoline pid=%d\n", u.u_procp->p_pid);
#endif
    (void)splhigh();
    p = u.u_procp;
    p->p_dsize = icodeend - icode;
    p->p_dmin = p->p_dsize;
    p->p_daddr = USER_DATA_START;
    p->p_ssize = 1024;
    p->p_saddr = USER_DATA_END - p->p_ssize;

    data_start = vm_vaddr_trunc_page(USER_DATA_START);
    if (vm_vaddr_round_page(USER_DATA_START + p->p_dsize,
        &data_end) != 0)
        panic("init data range");
    stack_start = vm_vaddr_trunc_page(p->p_saddr);
    if (vmspace_map_anon(p->p_vmspace, data_start,
        data_end - data_start, VM_PROT_ALL, VM_MAP_EXECUTABLE) != 0 ||
        vmspace_map_anon(p->p_vmspace, stack_start,
        USER_DATA_END - stack_start, VM_PROT_READ | VM_PROT_WRITE,
        VM_MAP_STACK) != 0 ||
        vmspace_write(p->p_vmspace, USER_DATA_START, icode,
        icodeend - icode) != 0)
        panic("init vmspace");

    if (boothowto & RB_SINGLE) {
        vm_vaddr_t flag_address = USER_DATA_START +
            (initflags - icode) + 1;
        char flag = 's';

        if (vmspace_write(p->p_vmspace, flag_address, &flag, 1) != 0)
            panic("init flags");
    }
    pmap_md_legacy_user_disable();
    if (vmspace_activate(p->p_vmspace) != 0)
        panic("init pmap");
#if defined(N64_TRACE) || defined(MIPS_TRACE)
    printf ("mipsboot: entering proc1 user bootstrap\n");
#endif
    md_user_enter(USER_DATA_START, USER_DATA_END);
    panic("init user return");
}

/*
 * Initialization code.
 * Called from cold start routine as
 * soon as a stack and segmentation
 * have been established.
 * Functions:
 *  clear and free user core
 *  turn on clock
 *  hand craft 0th process
 *  call all initialization routines
 *  fork - process 0 to schedule
 *       - process 1 execute bootstrap
 */
int
main()
{
    register struct proc *p;
    register int i;
    register struct fs *fs = NULL;
    int error;
    int s __attribute__((unused));

    md_uarea_guard_init(md_curuser);
    startup();
    printf ("\n%s\n", version);
    kconfig();
    error = vm_phys_bootstrap((vm_size_t)physmem);
    if (error != 0)
        panic("invalid physical memory map");
    vm_phys_bootstrap_summary();
    vm_page_bootstrap_summary();
    error = vm_page_bootstrap_selftest();
    if (error != 0)
        panic("vm page self-test failed");
    printf("vm page: self-test ok\n");
    error = pmap_system_init(&vm_page_boot_allocator);
    if (error != 0)
        panic("pmap bootstrap failed");
    error = pmap_bootstrap_selftest();
    if (error != 0)
        panic("pmap self-test failed");
    printf("pmap: self-test ok\n");
    error = vmspace_system_init(&vm_page_boot_allocator);
    if (error != 0)
        panic("vmspace bootstrap failed");
#if defined(N64) && defined(VIDEO_ENABLED)
    n64_video_attach();
#endif

    /*
     * Set up system process 0 (swapper).
     */
    p = &proc[0];
    p->p_uarea = md_curuser;
    p->p_addr = (size_t)p->p_uarea;
    error = vmspace_create(&p->p_vmspace);
    if (error != 0 || vmspace_activate(p->p_vmspace) != 0)
        panic("proc0 vmspace");
    p->p_stat = SRUN;
    p->p_flag |= SLOAD | SSYS;
    p->p_nice = NZERO;

    u.u_procp = p;          /* init user structure */
    u.u_cmask = CMASK;
    u.u_lastfile = -1;
    for (i = 1; i < NGROUPS; i++)
        u.u_groups[i] = NOGROUP;
    for (i = 0; i < sizeof(u.u_rlimit)/sizeof(u.u_rlimit[0]); i++)
        u.u_rlimit[i].rlim_cur = u.u_rlimit[i].rlim_max =
            RLIM_INFINITY;
#ifdef N64
    /*
     * The first N64 systems use a small volatile /var RAM disk.  Keep core
     * dumps disabled by default so a crashing tool does not consume it.
     */
    u.u_rlimit[RLIMIT_CORE].rlim_cur = 0;
#endif

    /* Initialize signal state for process 0 */
    siginit (p);

    /*
     * Initialize tables, protocols, and set up well-known inodes.
     */
#ifdef LOG_ENABLED
    loginit();
#endif
    coutinit();
    cinit();
    pqinit();
    ihinit();
    bioinit();
    nchinit();
    clkstart();

    pipedev = rootdev;

    /* Attach services. */
    struct conf_service *svc;
    for (svc = conf_service_init; svc->svc_attach != NULL; svc++)
        (*svc->svc_attach)();

    /* Mount a root filesystem. */
    s = spl0();
    if (vfs_mountroot(MOUNT_UFS, rootdev,
        (boothowto & RB_RDONLY) ? MNT_RDONLY : 0, &rootdir) != 0)
        panic ("No root filesystem found!");
    fs = rootdir->i_fs;
    mount_updname (fs, "/", "root", 1, 4);
    time.tv_sec = fs->fs_time;
    boottime = time;

    /* Find a swap file. */
    swapstart = 1;
    (*bdevsw[major(swapdev)].d_open)(swapdev, FREAD|FWRITE, S_IFBLK);
    nswap = (*bdevsw[major(swapdev)].d_psize)(swapdev);
    if (nswap <= 0)
        panic ("zero swap size");   /* don't want to panic, but what ? */
    mfree (swapmap, nswap, swapstart);
    error = vm_pager_swap_init();
    if (error != 0)
        panic("swap pager init");

    printf ("phys mem  = %u kbytes\n", physmem / 1024);
    printf ("user mem  = %u kbytes\n", MAXMEM / 1024);
    printf ("root dev  = (%d,%d)\n", major(rootdev), minor(rootdev));
    printf ("swap dev  = (%d,%d)\n", major(swapdev), minor(swapdev));
    printf ("root size = %u kbytes\n", fs->fs_fsize * DEV_BSIZE / 1024);
    printf ("swap size = %u kbytes\n", nswap * DEV_BSIZE / 1024);

    /* Kick off timeout driven events by calling first time. */
#if defined(N64_TRACE) || defined(MIPS_TRACE)
    printf ("mipsboot: before schedcpu\n");
#endif
    schedcpu (0);
#if defined(N64_TRACE) || defined(MIPS_TRACE)
    printf ("mipsboot: after schedcpu\n");
#endif

    /* Set up the root file system. */
#if defined(N64_TRACE) || defined(MIPS_TRACE)
    printf ("mipsboot: before rootdir iget\n");
#endif
#if defined(N64_TRACE) || defined(MIPS_TRACE)
    printf ("mipsboot: after rootdir iget\n");
#endif
    u.u_cdir = iget (rootdev, &mount[0].m_filsys, (ino_t) ROOTINO);
    iunlock (u.u_cdir);
    u.u_rdir = NULL;
#if defined(N64_TRACE) || defined(MIPS_TRACE)
    printf ("mipsboot: before newproc\n");
#endif

    /*
     * Make init process.
     */
    if (newproc (0) != 0)
        panic("cannot create init");
    /* Process 0 supplies the idle scheduler context. */
#ifdef USB_ENABLED
    printf("usb0: deferred task runner uses proc0\n");
#endif
#if defined(N64_TRACE) || defined(MIPS_TRACE)
    printf ("mipsboot: proc0 entering sched\n");
#endif
    sched();
    return 0;                       /* NOTREACHED */
}
