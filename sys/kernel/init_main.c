/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <sys/param.h>
#include <sys/user.h>
#include <sys/fs.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/ioctl.h>
#include <sys/inode.h>
#include <sys/conf.h>
#include <sys/buf.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/vm.h>
#include <sys/clist.h>
#include <sys/reboot.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/namei.h>
#include <sys/stat.h>
#include <sys/kconfig.h>
#include <sys/swap.h>
#include <sys/todr.h>
#include <vm/vm_phys.h>
#include <vm/vm_page.h>
#include <vm/pmap.h>
#include <vm/vm_object.h>
#include <vm/vmspace.h>
#ifdef N64
#include <machine/video.h>
#endif

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
    register struct fs *fs = NULL;
    int error;
    int s __attribute__((unused));

    md_uarea_guard_init(md_curuser);
    loginit();
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

    error = proc0_bootstrap(md_curuser);
    if (error != 0)
        panic("proc0 bootstrap");
#ifdef N64
    /*
     * The first N64 systems use a small volatile /var RAM disk.  Keep core
     * dumps disabled by default so a crashing tool does not consume it.
     */
    u.u_rlimit[RLIMIT_CORE].rlim_cur = 0;
#endif

    /*
     * Initialize tables, protocols, and set up well-known inodes.
     */
    coutinit();
    cinit();
    ihinit();
    bioinit();
    nchinit();
    clkstart();

    pipedev = rootdev;

    /* Attach services. */
    struct conf_service *svc;
    for (svc = conf_service_init; svc->svc_attach != NULL; svc++)
        (*svc->svc_attach)(0);

    /* Mount a root filesystem. */
    s = spl0();
    if (vfs_mountroot(MOUNT_UFS, rootdev,
        (boothowto & RB_RDONLY) ? MNT_RDONLY : 0, &rootdir) != 0)
        panic ("No root filesystem found!");
    fs = rootdir->i_fs;
    mount_updname (fs, "/", "root", 1, 4);
    inittodr(fs->fs_time);

    /* Swap devices are formatted and attached by userland. */
    swapdev = NODEV;
    swapstart = 0;
    nswap = 0;

    printf ("phys mem  = %u kbytes\n", physmem / 1024);
    printf ("user mem  = %u kbytes\n", MAXMEM / 1024);
    printf ("root dev  = (%d,%d)\n", major(rootdev), minor(rootdev));
    printf ("swap devices = %u\n", swap_device_count());
    if (swapdev == NODEV || nswap == 0)
        printf ("swap dev  = none\n");
    else
        printf ("swap dev  = (%d,%d)\n",
            major(swapdev), minor(swapdev));
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
