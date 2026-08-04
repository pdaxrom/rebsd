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

#define SWAP_DEVICE_DRAINING 0x01u

struct swap_device {
    struct swap_device *sd_next;
    struct vm_page     *sd_storage_pages;
    vm_pfn_t            sd_storage_page_count;
    dev_t               sd_dev;
    size_t              sd_base_page;
    size_t              sd_total_pages;
    size_t              sd_usable_pages;
    size_t              sd_used_pages;
    size_t              sd_alloc_hint;
    size_t              sd_page_blocks;
    u_char             *sd_bitmap;
    unsigned            sd_flags;
    unsigned            sd_linux_format;
    unsigned            sd_badpages;
};

static unsigned char swap_header[SWAP_LINUX_PAGE_BYTES]
    __attribute__((aligned(VM_PAGE_SIZE)));
static struct swap_device *swap_devices;
static struct swap_device *swap_alloc_device;

static void
swap_device_storage_release(struct swap_device *device)
{
    vm_pfn_t index;

    if (device == 0 || device->sd_storage_pages == 0)
        return;
    for (index = 0; index < device->sd_storage_page_count; ++index)
        if (vm_page_counter_dec(&vm_page_boot_allocator,
            device->sd_storage_pages + index,
            VM_PAGE_COUNTER_WIRE) != 0)
            panic("swap device unwire");
    if (vm_page_free(&vm_page_boot_allocator, device->sd_storage_pages,
        device->sd_storage_page_count) != 0)
        panic("swap device free");
}

static int
swap_device_storage_alloc(size_t total_pages,
    struct swap_device **result)
{
    struct vm_page_request request;
    struct vm_page *pages;
    struct swap_device *device;
    size_t bitmap_bytes;
    vm_size_t bytes;
    vm_size_t storage_size;
    vm_pfn_t page_count;
    vm_pfn_t index;
    int error;

    if (result == 0 || total_pages < 2)
        return EINVAL;
    if (total_pages > VM_SIZE_MAX - 7)
        return EOVERFLOW;
    bitmap_bytes = (total_pages + 7) >> 3;
    if (bitmap_bytes > VM_SIZE_MAX - sizeof(*device))
        return EOVERFLOW;
    bytes = sizeof(*device) + bitmap_bytes;
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
    device = pmap_pages_direct_map(pages, page_count,
        PMAP_CACHE_CACHED);
    if (device == 0) {
        for (index = 0; index < page_count; ++index)
            (void)vm_page_counter_dec(&vm_page_boot_allocator,
                pages + index, VM_PAGE_COUNTER_WIRE);
        (void)vm_page_free(&vm_page_boot_allocator, pages, page_count);
        return EFAULT;
    }

    bzero((caddr_t)device, storage_size);
    device->sd_storage_pages = pages;
    device->sd_storage_page_count = page_count;
    device->sd_bitmap = (u_char *)(device + 1);
    device->sd_total_pages = total_pages;
    *result = device;
    return 0;
}

static int
swap_bitmap_test(const struct swap_device *device, size_t page)
{
    return (device->sd_bitmap[page >> 3] &
        (1u << (page & 7))) != 0;
}

static void
swap_bitmap_set(struct swap_device *device, size_t page, int allocated)
{
    if (allocated)
        device->sd_bitmap[page >> 3] |= 1u << (page & 7);
    else
        device->sd_bitmap[page >> 3] &= ~(1u << (page & 7));
}

static int
swap_device_bitmap_init(struct swap_device *device,
    const struct swap_linux_info *linux_swap)
{
    unsigned badpage;
    unsigned index;
    size_t bitmap_bytes;
    int error;

    bitmap_bytes = (device->sd_total_pages + 7) >> 3;
    bzero((caddr_t)device->sd_bitmap, bitmap_bytes);
    swap_bitmap_set(device, 0, 1);
    device->sd_usable_pages = device->sd_total_pages - 1;
    for (index = 0; index < linux_swap->sli_badpages; ++index) {
        error = swap_linux_badpage(swap_header, linux_swap, index,
            &badpage);
        if (error != 0)
            return error;
        if (badpage == 0 || badpage >= device->sd_total_pages ||
            swap_bitmap_test(device, badpage))
            return EINVAL;
        swap_bitmap_set(device, badpage, 1);
        --device->sd_usable_pages;
    }
    device->sd_alloc_hint = 1;
    return device->sd_usable_pages != 0 ? 0 : ENOSPC;
}

static struct swap_device *
swap_device_find_dev(dev_t dev)
{
    struct swap_device *device;

    for (device = swap_devices; device != 0; device = device->sd_next)
        if (device->sd_dev == dev)
            return device;
    return 0;
}

static struct swap_device *
swap_device_find_slot(size_t slot, size_t *local_block)
{
    struct swap_device *device;
    size_t page;

    if (slot == 0 || VM_PAGE_SIZE % DEV_BSIZE != 0)
        return 0;
    for (device = swap_devices; device != 0; device = device->sd_next) {
        if (slot % device->sd_page_blocks != 0)
            continue;
        page = slot / device->sd_page_blocks;
        if (page < device->sd_base_page ||
            page >= device->sd_base_page + device->sd_total_pages)
            continue;
        if (local_block != 0)
            *local_block = (page - device->sd_base_page) *
                device->sd_page_blocks;
        return device;
    }
    return 0;
}

static int
swap_device_assign_base(struct swap_device *device)
{
    struct swap_device **link;
    struct swap_device *current;
    size_t base;

    base = 1;
    link = &swap_devices;
    while ((current = *link) != 0) {
        if (base <= SIZE_MAX - device->sd_total_pages &&
            base + device->sd_total_pages <= current->sd_base_page)
            break;
        if (current->sd_base_page >
            SIZE_MAX - current->sd_total_pages)
            return EOVERFLOW;
        base = current->sd_base_page + current->sd_total_pages;
        link = &current->sd_next;
    }
    if (base > SIZE_MAX - device->sd_total_pages ||
        base + device->sd_total_pages >
        SIZE_MAX / device->sd_page_blocks)
        return EOVERFLOW;
    device->sd_base_page = base;
    device->sd_next = current;
    *link = device;
    if (swap_alloc_device == 0)
        swap_alloc_device = device;
    return 0;
}

static int
swap_device_io(dev_t dev, size_t blkno, size_t coreaddr,
    int count, int rdflg)
{
    struct buf *bp;
    int error;
    int s;

    if (major(dev) >= nblkdev)
        return ENXIO;
    bp = geteblk();
    error = 0;
    while (count != 0) {
        int n;

        bp->b_flags = B_BUSY | B_PHYS | B_INVAL | rdflg;
        bp->b_dev = dev;
        bp->b_bcount = count;
        bp->b_blkno = blkno;
        bp->b_addr = (caddr_t)coreaddr;
        (*bdevsw[major(dev)].d_strategy)(bp);
        s = splbio();
        while ((bp->b_flags & B_DONE) == 0)
            sleep((caddr_t)bp, PSWP);
        splx(s);
        if ((bp->b_flags & B_ERROR) != 0 || bp->b_resid != 0) {
            error = (bp->b_flags & B_ERROR) != 0 ?
                geterror(bp) : EIO;
            break;
        }
        n = bp->b_bcount;
        if (n <= 0 || n > count) {
            error = EIO;
            break;
        }
        count -= n;
        coreaddr += n;
        blkno += btod(n);
    }
    brelse(bp);
    return error;
}

/*
 * swap I/O
 */
int
swap (size_t blkno, size_t coreaddr, int count, int rdflg)
{
    struct swap_device *device;
    size_t local_block;
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
    device = swap_device_find_slot(blkno, &local_block);
    if (device == 0 || count <= 0 ||
        (size_t)btod(count) >
        device->sd_total_pages * device->sd_page_blocks - local_block)
        return EINVAL;
    return swap_device_io(device->sd_dev, local_block, coreaddr,
        count, rdflg);
}

int
swap_configure(dev_t dev, struct swap_config_info *info)
{
    struct swap_linux_info linux_swap;
    struct swap_device *device;
    size_t page_blocks;
    daddr_t blocks;
    int error;

    if (info == 0 || dev == NODEV || major(dev) >= nblkdev)
        return EINVAL;
    bzero((caddr_t)info, sizeof(*info));
    if (swap_device_find_dev(dev) != 0)
        return EBUSY;
    if (VM_PAGE_SIZE < DEV_BSIZE || VM_PAGE_SIZE % DEV_BSIZE != 0 ||
        VM_PAGE_SIZE != SWAP_LINUX_PAGE_BYTES)
        return EINVAL;

    error = (*bdevsw[major(dev)].d_open)(dev,
        FREAD | FWRITE, S_IFBLK);
    if (error != 0)
        return error;
    blocks = (*bdevsw[major(dev)].d_psize)(dev);
    if (blocks <= 0) {
        error = ENOSPC;
        goto fail;
    }
    if ((daddr_t)(u_int)blocks != blocks) {
        error = EOVERFLOW;
        goto fail;
    }
    page_blocks = VM_PAGE_SIZE / DEV_BSIZE;
    if ((size_t)blocks / page_blocks < 2) {
        error = ENOSPC;
        goto fail;
    }
    error = swap_device_storage_alloc((size_t)blocks / page_blocks,
        &device);
    if (error != 0)
        goto fail;
    device->sd_dev = dev;
    device->sd_page_blocks = page_blocks;
    error = swap_device_io(dev, 0, (size_t)swap_header,
        VM_PAGE_SIZE, B_READ);
    if (error != 0)
        goto fail_storage;
    error = swap_linux_parse(swap_header, sizeof(swap_header),
        device->sd_total_pages, &linux_swap);
    if (error == 0) {
        device->sd_total_pages = linux_swap.sli_last_page + 1u;
        device->sd_linux_format = 1;
        device->sd_badpages = linux_swap.sli_badpages;
        info->sci_linux_format = 1;
        info->sci_badpages = linux_swap.sli_badpages;
        error = swap_device_bitmap_init(device, &linux_swap);
    } else {
        if (error == ENOENT)
            error = EINVAL;
        goto fail_storage;
    }
    if (error != 0)
        goto fail_storage;
    if (device->sd_usable_pages >
        ((u_int)~0u - nswap) / page_blocks) {
        error = EOVERFLOW;
        goto fail_storage;
    }
    error = swap_device_assign_base(device);
    if (error != 0)
        goto fail_storage;
    info->sci_usable_blocks = device->sd_usable_pages * page_blocks;
    nswap += (u_int)info->sci_usable_blocks;
    swapstart = 0;
    if (swapdev == NODEV)
        swapdev = dev;
    error = vm_pager_swap_init();
    if (error != 0) {
        (void)swap_unconfigure(dev);
        bzero((caddr_t)info, sizeof(*info));
        return error;
    }

    if (info->sci_linux_format) {
        printf("swap: Linux v1, %u usable kbytes, %u bad pages\n",
            (unsigned)(info->sci_usable_blocks * DEV_BSIZE / 1024),
            info->sci_badpages);
    } else {
        printf("swap: raw, %u usable kbytes\n",
            (unsigned)(info->sci_usable_blocks * DEV_BSIZE / 1024));
    }
    return 0;

fail_storage:
    swap_device_storage_release(device);
fail:
    (void)(*bdevsw[major(dev)].d_close)(dev,
        FREAD | FWRITE, S_IFBLK);
    bzero((caddr_t)info, sizeof(*info));
    return error;
}

size_t
swap_slot_alloc(void)
{
    struct swap_device *device;
    struct swap_device *first;
    size_t page;
    size_t pass;

    first = swap_alloc_device != 0 ? swap_alloc_device : swap_devices;
    if (first == 0)
        return 0;
    device = first;
    do {
        if ((device->sd_flags & SWAP_DEVICE_DRAINING) == 0 &&
            device->sd_used_pages < device->sd_usable_pages) {
            for (pass = 0; pass < 2; ++pass) {
                size_t first_page;
                size_t end_page;

                first_page = pass == 0 ? device->sd_alloc_hint : 1;
                end_page = pass == 0 ? device->sd_total_pages :
                    device->sd_alloc_hint;
                for (page = first_page; page < end_page; ++page) {
                    if (swap_bitmap_test(device, page))
                        continue;
                    swap_bitmap_set(device, page, 1);
                    ++device->sd_used_pages;
                    device->sd_alloc_hint = page + 1;
                    if (device->sd_alloc_hint >=
                        device->sd_total_pages)
                        device->sd_alloc_hint = 1;
                    swap_alloc_device = device->sd_next != 0 ?
                        device->sd_next : swap_devices;
                    return (device->sd_base_page + page) *
                        device->sd_page_blocks;
                }
            }
        }
        device = device->sd_next != 0 ? device->sd_next : swap_devices;
    } while (device != first);
    return 0;
}

void
swap_slot_free(size_t slot)
{
    struct swap_device *device;
    size_t local_block;
    size_t page;

    device = swap_device_find_slot(slot, &local_block);
    if (device == 0 || local_block % device->sd_page_blocks != 0)
        panic("swap slot free");
    page = local_block / device->sd_page_blocks;
    if (page == 0 || page >= device->sd_total_pages ||
        !swap_bitmap_test(device, page) || device->sd_used_pages == 0)
        panic("swap slot state");
    swap_bitmap_set(device, page, 0);
    --device->sd_used_pages;
    if (page < device->sd_alloc_hint)
        device->sd_alloc_hint = page;
}

int
swap_slot_draining(size_t slot)
{
    struct swap_device *device;

    device = swap_device_find_slot(slot, 0);
    return device != 0 &&
        (device->sd_flags & SWAP_DEVICE_DRAINING) != 0;
}

size_t
swap_total_blocks(void)
{
    return nswap;
}

size_t
swap_free_blocks(void)
{
    struct swap_device *device;
    size_t blocks;

    blocks = 0;
    for (device = swap_devices; device != 0; device = device->sd_next)
        blocks += (device->sd_usable_pages - device->sd_used_pages) *
            device->sd_page_blocks;
    return blocks;
}

unsigned
swap_device_count(void)
{
    struct swap_device *device;
    unsigned count;

    count = 0;
    for (device = swap_devices; device != 0; device = device->sd_next)
        ++count;
    return count;
}

int
swap_unconfigure(dev_t dev)
{
    struct swap_device **link;
    struct swap_device *device;
    dev_t closing_dev;
    size_t first;
    size_t end;
    size_t blocks;
    int error;

    for (link = &swap_devices; (device = *link) != 0;
        link = &device->sd_next)
        if (device->sd_dev == dev)
            break;
    if (device == 0)
        return EINVAL;
    if ((device->sd_flags & SWAP_DEVICE_DRAINING) != 0)
        return EBUSY;
    device->sd_flags |= SWAP_DEVICE_DRAINING;
    first = device->sd_base_page * device->sd_page_blocks;
    end = (device->sd_base_page + device->sd_total_pages) *
        device->sd_page_blocks;
    error = vm_pager_swapoff(first, end);
    if (error != 0) {
        device->sd_flags &= ~SWAP_DEVICE_DRAINING;
        return error;
    }
    if (device->sd_used_pages != 0)
        panic("swapoff slots");
    blocks = device->sd_usable_pages * device->sd_page_blocks;
    closing_dev = device->sd_dev;
    if (swap_alloc_device == device)
        swap_alloc_device = device->sd_next != 0 ?
            device->sd_next : swap_devices;
    *link = device->sd_next;
    if (swap_alloc_device == device)
        swap_alloc_device = swap_devices;
    if (nswap < blocks)
        panic("swap total");
    nswap -= (u_int)blocks;
    if (swapdev == closing_dev)
        swapdev = swap_devices != 0 ? swap_devices->sd_dev : NODEV;
    if (swap_devices == 0) {
        swap_alloc_device = 0;
        vm_pager_swap_disable();
    }
    (void)(*bdevsw[major(closing_dev)].d_close)(closing_dev,
        FREAD | FWRITE, S_IFBLK);
    swap_device_storage_release(device);
    printf("swap: device (%d,%d) disabled\n",
        major(closing_dev), minor(closing_dev));
    return 0;
}

/* Enable a validated Linux swap v1 block device at run time. */
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
        error = swap_configure(dev, &info);
    u.u_error = error;
    if (error == 0)
        u.u_rval = 0;
}

void
swapoff(void)
{
    struct a {
        char *special;
    } *uap;
    dev_t dev;
    int error;

    if (!suser())
        return;
    uap = (struct a *)u.u_arg;
    error = getmdev(&dev, uap->special);
    if (error == 0)
        error = swap_unconfigure(dev);
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

    struct swap_device *swap_device;
    size_t local_block;

    if (nblocks == 0)
        return;
    swap_device = swap_device_find_slot(blkno, &local_block);
    if (swap_device == 0)
        panic("swap discard slot");
    device = &bdevsw[major(swap_device->sd_dev)];
    if ((device->d_flags & BDEV_DISCARD) == 0)
        return;
    range.dd_offset = (disk_sector_t)local_block * (DEV_BSIZE / 512);
    range.dd_length = (disk_sector_t)nblocks * (DEV_BSIZE / 512);
    error = (*device->d_ioctl)(swap_device->sd_dev, DIOCDISCARD,
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
            if (block_shift == 9u)
                bp->b_flags |= B_SECTOR512;
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
