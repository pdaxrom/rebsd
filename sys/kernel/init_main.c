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
#include <vm/vm_phys.h>
#include <vm/vm_page.h>
#include <vm/pmap.h>
#include <vm/vm_object.h>
#include <vm/vmspace.h>
#ifdef N64
#include <machine/video.h>
#endif

struct map swapmap[1] = {
    { 0, 0, "swapmap" },
};

/*
 * The swap device reports its usable size at run time.  Allocate a wired
 * resource map large enough for the worst possible fragmentation of that
 * exact address space instead of reserving a board-specific static table.
 */
static int
swapmap_bootstrap(size_t total, size_t allocation_unit)
{
    struct vm_page_request request;
    struct vm_page *pages;
    struct mapent *entries;
    vm_size_t bytes;
    vm_size_t storage_size;
    vm_pfn_t page_count;
    vm_pfn_t index;
    size_t entry_count;
    int error;

    entry_count = rmap_required_entries(total, allocation_unit);
    if (entry_count == 0 ||
        entry_count > VM_SIZE_MAX / sizeof(*entries))
        return EOVERFLOW;
    bytes = entry_count * sizeof(*entries);
    error = vm_size_round_page(bytes, &storage_size);
    if (error != 0)
        return error;
    page_count = storage_size / VM_PAGE_SIZE;

    vm_page_request_init(&request);
    request.vpr_npages = page_count;
    request.vpr_state = VM_PAGE_WIRED;
    error = vm_page_alloc(&vm_page_boot_allocator, &request, &pages);
    if (error != 0)
        return error;
    entries = pmap_pages_direct_map(pages, page_count,
        PMAP_CACHE_CACHED);
    if (entries == 0) {
        for (index = 0; index < page_count; ++index)
            (void)vm_page_counter_dec(&vm_page_boot_allocator,
                pages + index, VM_PAGE_COUNTER_WIRE);
        (void)vm_page_free(&vm_page_boot_allocator, pages, page_count);
        return EFAULT;
    }

    bzero((caddr_t)entries, storage_size);
    swapmap[0].m_map = entries;
    swapmap[0].m_limit = entries + entry_count;
    return 0;
}

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
    size_t swap_page_blocks;
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
#ifdef LOG_ENABLED
    loginit();
#endif
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
    time.tv_sec = fs->fs_time;
    boottime = time;

    /* Initialize swap when the system configuration supplies a device. */
    if (swapdev != NODEV) {
        swapstart = 1;
        (*bdevsw[major(swapdev)].d_open)(swapdev,
            FREAD|FWRITE, S_IFBLK);
        nswap = (*bdevsw[major(swapdev)].d_psize)(swapdev);
        if (nswap <= 0)
            panic ("zero swap size"); /* don't want to panic, but what ? */
        if (VM_PAGE_SIZE < DEV_BSIZE ||
            VM_PAGE_SIZE % DEV_BSIZE != 0)
            panic("invalid swap allocation unit");
        swap_page_blocks = VM_PAGE_SIZE / DEV_BSIZE;
        error = swapmap_bootstrap(nswap, swap_page_blocks);
        if (error != 0)
            panic("cannot allocate swap map");
        mfree (swapmap, nswap, swapstart);
        error = vm_pager_swap_init();
        if (error != 0)
            panic("swap pager init");
    } else {
        swapstart = 0;
        nswap = 0;
    }

    printf ("phys mem  = %u kbytes\n", physmem / 1024);
    printf ("user mem  = %u kbytes\n", MAXMEM / 1024);
    printf ("root dev  = (%d,%d)\n", major(rootdev), minor(rootdev));
    if (swapdev == NODEV)
        printf ("swap dev  = none\n");
    else
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
