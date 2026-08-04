/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/map.h>
#include <sys/ioctl.h>
#include <sys/disk.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/swap.h>
#include <sys/systm.h>
#include <sys/vm.h>
#include <sys/uio.h>
#include <vm/pmap.h>
#include <vm/swap_linux.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>

struct map swapmap[1] = {
    { 0, 0, "swapmap" },
};

static unsigned char swap_header[SWAP_LINUX_PAGE_BYTES]
    __attribute__((aligned(VM_PAGE_SIZE)));
static struct vm_page *swapmap_storage_pages;
static vm_pfn_t swapmap_storage_page_count;

static void
swapmap_storage_release(void)
{
    vm_pfn_t index;

    if (swapmap_storage_pages == 0)
        return;
    for (index = 0; index < swapmap_storage_page_count; ++index)
        if (vm_page_counter_dec(&vm_page_boot_allocator,
            swapmap_storage_pages + index, VM_PAGE_COUNTER_WIRE) != 0)
            panic("swap map unwire");
    if (vm_page_free(&vm_page_boot_allocator, swapmap_storage_pages,
        swapmap_storage_page_count) != 0)
        panic("swap map free");
    swapmap_storage_pages = 0;
    swapmap_storage_page_count = 0;
    swapmap[0].m_map = 0;
    swapmap[0].m_limit = 0;
}

static int
swapmap_storage_alloc(size_t total, size_t allocation_unit)
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

    if (swapmap_storage_pages != 0)
        return EBUSY;
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
    swapmap_storage_pages = pages;
    swapmap_storage_page_count = page_count;
    swapmap[0].m_map = entries;
    swapmap[0].m_limit = entries + entry_count;
    return 0;
}

static size_t
swapmap_add_extent(unsigned first_page, unsigned end_page,
    size_t page_blocks)
{
    size_t blocks;
    size_t start;

    if (first_page >= end_page)
        return 0;
    start = (size_t)first_page * page_blocks;
    blocks = (size_t)(end_page - first_page) * page_blocks;
    mfree(swapmap, blocks, start);
    return blocks;
}

static int
swapmap_linux_init(const struct swap_linux_info *linux_swap,
    size_t page_blocks, size_t *usable_blocks)
{
    unsigned current;
    unsigned badpage;
    unsigned index;
    int error;

    current = 1;
    *usable_blocks = 0;
    for (index = 0; index < linux_swap->sli_badpages; ++index) {
        error = swap_linux_badpage(swap_header, linux_swap, index,
            &badpage);
        if (error != 0)
            return error;
        *usable_blocks += swapmap_add_extent(current, badpage,
            page_blocks);
        current = badpage + 1u;
    }
    *usable_blocks += swapmap_add_extent(current,
        linux_swap->sli_last_page + 1u, page_blocks);
    return *usable_blocks != 0 ? 0 : ENOSPC;
}

/*
 * swap I/O
 */
int
swap (size_t blkno, size_t coreaddr, int count, int rdflg)
{
    register struct buf *bp;
    int s;
    int error = 0;
#ifdef N64_TRACE
    static int n64_swap_trace;
#endif

//printf ("swap (%u, %08x, %d, %s)\n", blkno, coreaddr, count, rdflg ? "R" : "W");
#ifdef N64_TRACE
    if (n64_swap_trace < 16) {
        printf ("n64swapio: blk=%u addr=%x count=%d %s\n",
            blkno, coreaddr, count, rdflg ? "read" : "write");
        n64_swap_trace++;
    }
#endif
#ifdef UCB_METER
    if (rdflg) {
        cnt.v_kbin += (count + 1023) / 1024;
    } else {
        cnt.v_kbout += (count + 1023) / 1024;
    }
#endif
    bp = geteblk();         /* allocate a buffer header */

    while (count) {
        int n;

        bp->b_flags = B_BUSY | B_PHYS | B_INVAL | rdflg;
        bp->b_dev = swapdev;
        bp->b_bcount = count;
        bp->b_blkno = blkno;
        bp->b_addr = (caddr_t) coreaddr;
        (*bdevsw[major(swapdev)].d_strategy) (bp);
#ifdef N64_TRACE
        if (n64_swap_trace < 16) {
            printf ("n64swapio: strategy flags=%x resid=%d\n",
                bp->b_flags, bp->b_resid);
            n64_swap_trace++;
        }
#endif
        s = splbio();
        while ((bp->b_flags & B_DONE) == 0)
            sleep ((caddr_t)bp, PSWP);
        splx (s);
        if ((bp->b_flags & B_ERROR) || bp->b_resid) {
            error = (bp->b_flags & B_ERROR) ? geterror(bp) : EIO;
            break;
        }
        n = bp->b_bcount;
        count -= n;
        coreaddr += n;
        blkno += btod (n);
    }
    brelse(bp);
    return error;
}

int
swap_configure(dev_t dev, int flags, struct swap_config_info *info)
{
    struct swap_linux_info linux_swap;
    dev_t previous_swapdev;
    size_t page_blocks;
    daddr_t blocks;
    int error;

    if (info == 0 || dev == NODEV || major(dev) >= nblkdev)
        return EINVAL;
    bzero((caddr_t)info, sizeof(*info));
    if (nswap != 0 || swapmap_storage_pages != 0)
        return EBUSY;
    if (VM_PAGE_SIZE < DEV_BSIZE || VM_PAGE_SIZE % DEV_BSIZE != 0 ||
        VM_PAGE_SIZE != SWAP_LINUX_PAGE_BYTES)
        return EINVAL;

    previous_swapdev = swapdev;
    swapdev = dev;
    error = (*bdevsw[major(dev)].d_open)(dev,
        FREAD | FWRITE, S_IFBLK);
    if (error != 0)
        goto fail_unopened;
    blocks = (*bdevsw[major(dev)].d_psize)(dev);
    if (blocks <= 0) {
        error = ENOSPC;
        goto fail;
    }
    if ((daddr_t)(u_int)blocks != blocks) {
        error = EOVERFLOW;
        goto fail;
    }
    nswap = (u_int)blocks;
    page_blocks = VM_PAGE_SIZE / DEV_BSIZE;
    error = swap(0, (size_t)swap_header, VM_PAGE_SIZE, B_READ);
    if (error != 0)
        goto fail;
    error = swap_linux_parse(swap_header, sizeof(swap_header),
        nswap / page_blocks, &linux_swap);
    if (error == 0) {
        nswap = (linux_swap.sli_last_page + 1u) * page_blocks;
        swapstart = page_blocks;
        info->sci_linux_format = 1;
        info->sci_badpages = linux_swap.sli_badpages;
    } else if (error == ENOENT && (flags & SWAP_CONFIG_ALLOW_RAW) != 0) {
        swapstart = 1;
    } else {
        if (error == ENOENT)
            error = EINVAL;
        goto fail;
    }

    error = swapmap_storage_alloc(nswap, page_blocks);
    if (error != 0)
        goto fail;
    if (info->sci_linux_format) {
        error = swapmap_linux_init(&linux_swap, page_blocks,
            &info->sci_usable_blocks);
    } else if (nswap > swapstart) {
        info->sci_usable_blocks = nswap - swapstart;
        mfree(swapmap, info->sci_usable_blocks, swapstart);
        error = 0;
    } else {
        error = ENOSPC;
    }
    if (error != 0)
        goto fail;
    error = vm_pager_swap_init();
    if (error != 0)
        goto fail;

    if (info->sci_linux_format) {
        printf("swap: Linux v1, %u usable kbytes, %u bad pages\n",
            (unsigned)(info->sci_usable_blocks * DEV_BSIZE / 1024),
            info->sci_badpages);
    } else {
        printf("swap: raw, %u usable kbytes\n",
            (unsigned)(info->sci_usable_blocks * DEV_BSIZE / 1024));
    }
    return 0;

fail:
    swapmap_storage_release();
    (void)(*bdevsw[major(dev)].d_close)(dev,
        FREAD | FWRITE, S_IFBLK);
fail_unopened:
    swapstart = 0;
    nswap = 0;
    swapdev = previous_swapdev;
    bzero((caddr_t)info, sizeof(*info));
    return error;
}

/*
 * Enable a validated Linux swap v1 block device at run time.  Embedded
 * boards which deliberately configure raw RAM swap call swap_configure()
 * during boot with SWAP_CONFIG_ALLOW_RAW instead.
 */
void
swapon(void)
{
    struct a {
        char *special;
    } *uap;
    struct swap_config_info info;
    dev_t dev;
    int error;

    if (!suser())
        return;
    uap = (struct a *)u.u_arg;
    error = getmdev(&dev, uap->special);
    if (error == 0)
        error = swap_configure(dev, 0, &info);
    u.u_error = error;
    if (error == 0)
        u.u_rval = 0;
}

void
swap_discard(size_t blkno, size_t nblocks)
{
    struct disk_discard range;
    const struct bdevsw *device;
    int error;

    if (swapdev == NODEV || nblocks == 0)
        return;
    device = &bdevsw[major(swapdev)];
    if ((device->d_flags & BDEV_DISCARD) == 0)
        return;
    range.dd_offset = (disk_sector_t)blkno * (DEV_BSIZE / 512);
    range.dd_length = (disk_sector_t)nblocks * (DEV_BSIZE / 512);
    error = (*device->d_ioctl)(swapdev, DIOCDISCARD,
        (caddr_t)&range, FWRITE);
    if (error != 0)
        panic("swap discard");
}

/*
 * Raw I/O. The arguments are
 *  The strategy routine for the device
 *  A buffer, which may be a special buffer header
 *    owned exclusively by the device for this purpose or
 *    NULL if one is to be allocated.
 *  The device number
 *  Read/write flag
 * Essentially all the work is computing physical addresses and
 * validating them.
 *
 * rewritten to use the iov/uio mechanism from 4.3bsd.  the physbuf routine
 * was inlined.  essentially the chkphys routine performs the same task
 * as the useracc routine on a 4.3 system. 3/90 sms
 *
 * If the buffer pointer is NULL then one is allocated "dynamically" from
 * the system cache.  the 'invalid' flag is turned on so that the brelse()
 * done later doesn't place the buffer back in the cache.  the 'phys' flag
 * is left on so that the address of the buffer is recalcuated in getnewbuf().
 * The BYTE/WORD stuff began to be removed after testing proved that either
 * 1) the underlying hardware gives an error or 2) nothing bad happens.
 * besides, 4.3BSD doesn't do the byte/word check and noone could remember
 * why the byte/word check was added in the first place - likely historical
 * paranoia.  chkphys() inlined.  5/91 sms
 *
 * Refined (and streamlined) the flow by using a 'for' construct
 * (a la 4.3Reno).  Avoid allocating/freeing the buffer for each iovec
 * element (i must have been confused at the time).  6/91-sms
 *
 * Finished removing the BYTE/WORD code as part of implementing the common
 * raw read&write routines , systems had been running fine for several
 * months with it ifdef'd out.  9/91-sms
 */
static int
physio_shift(void (*strat) (struct buf*), struct buf *bp, dev_t dev, int rw,
    struct uio *uio, unsigned block_shift)
{
    int error = 0, s, c, allocbuf = 0;
    register struct iovec *iov;

    if (! bp) {
        allocbuf++;
        bp = geteblk();
    }
    u.u_procp->p_flag |= SLOCK;
    for ( ; uio->uio_iovcnt; uio->uio_iov++, uio->uio_iovcnt--) {
        iov = uio->uio_iov;
        if (iov->iov_base >= iov->iov_base + iov->iov_len) {
            error = EFAULT;
            break;
        }
        /*
         * Check that transfer is either entirely in the
         * data or in the stack: that is, either
         * the end is in the data or the start is in the stack
         * (remember wraparound was already checked).
         */
        if (baduaddr (iov->iov_base) ||
            baduaddr (iov->iov_base + iov->iov_len - 1)) {
            error = EFAULT;
            break;
        }
        if (! allocbuf) {
            s = splbio();
            while (bp->b_flags & B_BUSY) {
                bp->b_flags |= B_WANTED;
                sleep((caddr_t)bp, PRIBIO+1);
            }
            splx(s);
        }
        bp->b_error = 0;
        while (iov->iov_len) {
            bp->b_flags = B_BUSY | B_PHYS | B_INVAL | rw;
            bp->b_dev = dev;
            bp->b_addr = iov->iov_base;
            bp->b_blkno = (blkno_t)(uio->uio_offset >> block_shift);
            bp->b_bcount = iov->iov_len;
            c = bp->b_bcount;
            (*strat)(bp);
            s = splbio();
            while ((bp->b_flags & B_DONE) == 0)
                sleep((caddr_t)bp, PRIBIO);
            if (bp->b_flags & B_WANTED) /* rare */
                wakeup((caddr_t)bp);
            splx(s);
            c -= bp->b_resid;
            iov->iov_base += c;
            iov->iov_len -= c;
            uio->uio_resid -= c;
            uio->uio_offset += c;
            /* temp kludge for tape drives */
            if (bp->b_resid || (bp->b_flags & B_ERROR))
                break;
        }
        bp->b_flags &= ~(B_BUSY | B_WANTED);
        error = geterror(bp);
        /* temp kludge for tape drives */
        if (bp->b_resid || error)
            break;
    }
    if (allocbuf)
        brelse(bp);
    u.u_procp->p_flag &= ~SLOCK;
    return(error);
}

int
physio(void (*strat) (struct buf*), struct buf *bp, dev_t dev, int rw,
    struct uio *uio)
{
    return physio_shift(strat, bp, dev, rw, uio, DEV_BSHIFT);
}

int
rawrw (dev_t dev, struct uio *uio, int flag)
{
    (void)flag;
    return (physio(cdevsw[major(dev)].d_strategy, (struct buf *)NULL, dev,
        uio->uio_rw == UIO_READ ? B_READ : B_WRITE, uio));
}

/* Raw 512-byte-sector disks cannot use the kernel's 1024-byte DEV_BSIZE. */
int
rawrw512 (dev_t dev, struct uio *uio, int flag)
{
    (void)flag;
    return (physio_shift(cdevsw[major(dev)].d_strategy,
        (struct buf *)NULL, dev,
        uio->uio_rw == UIO_READ ? B_READ : B_WRITE, uio, 9u));
}
