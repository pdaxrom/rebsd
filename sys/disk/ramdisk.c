/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

/* Common writable-memory block driver with optional compression. */

#include <sys/types.h>
#ifdef DISK_HOST_TEST
#define RAMDISK_DEVICE_BLOCK_SHIFT 10
#else
#include <sys/param.h>
#define RAMDISK_DEVICE_BLOCK_SHIFT DEV_BSHIFT
#endif
#include <sys/buf.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/disk.h>
#include <sys/fcntl.h>
#ifdef DISK_HOST_TEST
void bcopy(const void *, void *, size_t);
#else
#include <sys/systm.h>
#include <sys/vm.h>
#include <vm/pmap.h>
#include <vm/vm_page.h>
#endif

#include <disk/ramdisk.h>
#include <disk/ramcomp.h>

static void ramdisk_done_error(struct buf *, int);

static void
ramdisk_zero(void *address, unsigned bytes)
{
    unsigned char *data;

    data = address;
    while (bytes-- != 0)
        *data++ = 0;
}

int
ramdisk_init(struct ramdisk *ramdisk, const struct ramdisk_config *config)
{
    int error;

    if (ramdisk == 0 || config == 0 || config->rdc_backing == 0 ||
        config->rdc_backing_bytes == 0 || config->rdc_media_bytes == 0 ||
        config->rdc_block_shift >= sizeof(unsigned) * 8 ||
        (config->rdc_backing_bytes &
        ((1u << config->rdc_block_shift) - 1)) != 0 ||
        (config->rdc_media_bytes &
        ((1u << config->rdc_block_shift) - 1)) != 0 ||
        (config->rdc_flags & ~RAMDISK_F_COMPRESSION) != 0)
        return EINVAL;
    if ((config->rdc_flags & RAMDISK_F_COMPRESSION) == 0) {
        if (config->rdc_media_bytes != config->rdc_backing_bytes ||
            config->rdc_compression != 0)
            return EINVAL;
    } else {
        if (config->rdc_compression == 0 ||
            config->rdc_block_shift != RAMCOMP_BLOCK_SHIFT)
            return EINVAL;
        error = ramcomp_init(config->rdc_compression,
            config->rdc_backing, config->rdc_backing_bytes,
            config->rdc_media_bytes,
            config->rdc_compression_metadata,
            config->rdc_compression_metadata_bytes);
        if (error != 0)
            return error;
    }
    ramdisk->rd_backing = config->rdc_backing;
    ramdisk->rd_backing_bytes = config->rdc_backing_bytes;
    ramdisk->rd_media_bytes = config->rdc_media_bytes;
    ramdisk->rd_minor = config->rdc_minor;
    ramdisk->rd_block_shift = config->rdc_block_shift;
    ramdisk->rd_flags = config->rdc_flags;
    ramdisk->rd_compression = config->rdc_compression;
    return 0;
}

#ifndef DISK_HOST_TEST
static int
ramdisk_storage_alloc(unsigned bytes, struct vm_page **pagesp,
    unsigned *page_countp, void **addressp)
{
    struct vm_page_request request;
    struct vm_page *pages;
    vm_pfn_t page_count;
    vm_pfn_t index;
    void *address;
    int error;

    if (bytes == 0 || bytes > VM_SIZE_MAX - VM_PAGE_MASK)
        return EINVAL;
    page_count = (bytes + VM_PAGE_MASK) / VM_PAGE_SIZE;
    vm_page_request_init(&request);
    request.vpr_npages = page_count;
    request.vpr_state = VM_PAGE_WIRED;
    error = vm_page_alloc(&vm_page_boot_allocator, &request, &pages);
    if (error != 0)
        return error;
    address = pmap_pages_direct_map(pages, page_count,
        PMAP_CACHE_CACHED);
    if (address == 0) {
        for (index = 0; index < page_count; ++index)
            (void)vm_page_counter_dec(&vm_page_boot_allocator,
                pages + index, VM_PAGE_COUNTER_WIRE);
        (void)vm_page_free(&vm_page_boot_allocator, pages, page_count);
        return EFAULT;
    }
    ramdisk_zero(address, page_count * VM_PAGE_SIZE);
    *pagesp = pages;
    *page_countp = page_count;
    *addressp = address;
    return 0;
}

static void
ramdisk_storage_free(struct vm_page *pages, unsigned page_count)
{
    unsigned index;

    if (pages == 0 || page_count == 0)
        return;
    for (index = 0; index < page_count; ++index)
        if (vm_page_counter_dec(&vm_page_boot_allocator, pages + index,
            VM_PAGE_COUNTER_WIRE) != 0)
            panic("ramdisk unwire");
    if (vm_page_free(&vm_page_boot_allocator, pages, page_count) != 0)
        panic("ramdisk free");
}
#endif

void
ramdisk_controller_init(struct ramdisk_controller *controller)
{
    if (controller != 0)
        ramdisk_zero(controller, sizeof(*controller));
}

int
ramdisk_controller_register_pool(struct ramdisk_controller *controller,
    int minor_number, volatile void *backing, unsigned backing_bytes,
    unsigned flags)
{
    struct ramdisk_slot *slot;

    if (controller == 0 || minor_number < 0 ||
        minor_number >= RAMDISK_MAX_DEVICES ||
        (flags & ~RAMDISK_POOL_DYNAMIC) != 0)
        return EINVAL;
    if ((flags & RAMDISK_POOL_DYNAMIC) != 0) {
        if (backing != 0)
            return EINVAL;
    } else if (backing == 0 || backing_bytes == 0 ||
        (backing_bytes & ((1u << RAMDISK_DEVICE_BLOCK_SHIFT) - 1)) != 0)
        return EINVAL;
    slot = &controller->rc_slot[minor_number];
    if (slot->rs_pool != 0 || slot->rs_pool_flags != 0 ||
        slot->rs_configured || slot->rs_open_count != 0)
        return EBUSY;
    slot->rs_pool = backing;
    slot->rs_pool_bytes = backing_bytes;
    slot->rs_pool_flags = flags;
    return 0;
}

static struct ramdisk_slot *
ramdisk_controller_slot(struct ramdisk_controller *controller, dev_t dev)
{
    unsigned unit;

    if (controller == 0)
        return 0;
    unit = minor(dev);
    if (unit >= RAMDISK_MAX_DEVICES)
        return 0;
    if (controller->rc_slot[unit].rs_pool == 0 &&
        (controller->rc_slot[unit].rs_pool_flags &
        RAMDISK_POOL_DYNAMIC) == 0)
        return 0;
    return &controller->rc_slot[unit];
}

int
ramdisk_controller_open(struct ramdisk_controller *controller, dev_t dev,
    int flag, int mode)
{
    struct ramdisk_slot *slot;

    (void)flag;
    (void)mode;
    slot = ramdisk_controller_slot(controller, dev);
    if (slot == 0)
        return ENXIO;
    ++slot->rs_open_count;
    return 0;
}

int
ramdisk_controller_close(struct ramdisk_controller *controller, dev_t dev,
    int flag, int mode)
{
    struct ramdisk_slot *slot;

    (void)flag;
    (void)mode;
    slot = ramdisk_controller_slot(controller, dev);
    if (slot == 0 || slot->rs_open_count == 0)
        return ENXIO;
    --slot->rs_open_count;
    return 0;
}

static void
ramdisk_slot_release(struct ramdisk_slot *slot)
{
#ifndef DISK_HOST_TEST
    ramdisk_storage_free(slot->rs_metadata_pages,
        slot->rs_metadata_page_count);
    ramdisk_storage_free(slot->rs_backing_pages,
        slot->rs_backing_page_count);
#endif
    ramdisk_zero(&slot->rs_disk, sizeof(slot->rs_disk));
    slot->rs_compression = 0;
    slot->rs_backing_pages = 0;
    slot->rs_backing_page_count = 0;
    slot->rs_metadata_pages = 0;
    slot->rs_metadata_page_count = 0;
    slot->rs_metadata = 0;
    slot->rs_configured = 0;
}

static int
ramdisk_slot_configure(struct ramdisk_slot *slot, int minor_number,
    const struct ramdisk_configure *request)
{
    struct ramdisk_config config;
    volatile void *backing;
    unsigned backing_bytes;
    unsigned media_bytes;
    unsigned metadata_bytes;
#ifndef DISK_HOST_TEST
    unsigned allocation_bytes;
    void *allocation;
#endif
    int error;

    if (request == 0 || request->rdc_reserved != 0 ||
        (request->rdc_flags & ~RAMDISK_CONFIG_COMPRESSION) != 0 ||
        slot->rs_configured || slot->rs_open_count != 1)
        return EINVAL;
    backing_bytes = request->rdc_backing_bytes;
    if (slot->rs_pool_flags & RAMDISK_POOL_DYNAMIC) {
        if (backing_bytes == 0)
            return EINVAL;
#ifdef DISK_HOST_TEST
        return EOPNOTSUPP;
#else
        error = ramdisk_storage_alloc(backing_bytes,
            &slot->rs_backing_pages, &slot->rs_backing_page_count,
            &allocation);
        if (error != 0)
            return error;
        backing = allocation;
#endif
    } else {
        if (backing_bytes == 0)
            backing_bytes = slot->rs_pool_bytes;
        if (backing_bytes > slot->rs_pool_bytes)
            return ENOSPC;
        backing = slot->rs_pool;
        ramdisk_zero((void *)backing, backing_bytes);
    }
    media_bytes = request->rdc_media_bytes;
    if (request->rdc_flags & RAMDISK_CONFIG_COMPRESSION) {
        if (media_bytes == 0 ||
            ramcomp_metadata_bytes(backing_bytes, media_bytes,
            &metadata_bytes) != 0) {
            error = EINVAL;
            goto fail;
        }
        if (metadata_bytes > (unsigned)-1 - sizeof(struct ramcomp)) {
            error = EOVERFLOW;
            goto fail;
        }
#ifdef DISK_HOST_TEST
        error = EOPNOTSUPP;
        goto fail;
#else
        allocation_bytes = sizeof(struct ramcomp) + metadata_bytes;
        error = ramdisk_storage_alloc(allocation_bytes,
            &slot->rs_metadata_pages, &slot->rs_metadata_page_count,
            &allocation);
        if (error != 0)
            goto fail;
        slot->rs_compression = allocation;
        slot->rs_metadata = (void *)(slot->rs_compression + 1);
#endif
    } else {
        if (media_bytes == 0)
            media_bytes = backing_bytes;
        if (media_bytes != backing_bytes) {
            error = EINVAL;
            goto fail;
        }
        metadata_bytes = 0;
    }

    config.rdc_backing = backing;
    config.rdc_backing_bytes = backing_bytes;
    config.rdc_media_bytes = media_bytes;
    config.rdc_minor = minor_number;
    config.rdc_block_shift = RAMDISK_DEVICE_BLOCK_SHIFT;
    config.rdc_flags = request->rdc_flags;
    config.rdc_compression = slot->rs_compression;
    config.rdc_compression_metadata = slot->rs_metadata;
    config.rdc_compression_metadata_bytes = metadata_bytes;
    error = ramdisk_init(&slot->rs_disk, &config);
    if (error != 0)
        goto fail;
    slot->rs_configured = 1;
    return 0;

fail:
    ramdisk_slot_release(slot);
    return error;
}

void
ramdisk_controller_strategy(struct ramdisk_controller *controller,
    struct buf *bp)
{
    struct ramdisk_slot *slot;

    slot = ramdisk_controller_slot(controller, bp->b_dev);
    if (slot != 0 && slot->rs_configured) {
        ramdisk_bdev_strategy(&slot->rs_disk, bp);
        return;
    }
    ramdisk_done_error(bp, ENXIO);
}

daddr_t
ramdisk_controller_size(struct ramdisk_controller *controller, dev_t dev)
{
    struct ramdisk_slot *slot;

    slot = ramdisk_controller_slot(controller, dev);
    if (slot == 0 || !slot->rs_configured)
        return 0;
    return ramdisk_bdev_size(&slot->rs_disk, dev);
}

int
ramdisk_controller_ioctl(struct ramdisk_controller *controller, dev_t dev,
    u_int cmd, caddr_t addr, int flag)
{
    struct ramdisk_slot *slot;
    struct ramdisk_info *info;

    slot = ramdisk_controller_slot(controller, dev);
    if (slot == 0)
        return ENXIO;
    switch (cmd) {
    case RAMDIOCCONFIGURE:
        if ((flag & FWRITE) == 0)
            return EBADF;
        return ramdisk_slot_configure(slot, minor(dev),
            (const struct ramdisk_configure *)addr);
    case RAMDIOCDESTROY:
        if ((flag & FWRITE) == 0)
            return EBADF;
        if (!slot->rs_configured)
            return ENXIO;
        if (slot->rs_open_count != 1)
            return EBUSY;
        ramdisk_slot_release(slot);
        return 0;
    case RAMDIOCGETINFO:
        if (addr == 0)
            return EINVAL;
        info = (struct ramdisk_info *)addr;
        ramdisk_zero(info, sizeof(*info));
        info->rdi_backing_capacity = slot->rs_pool_bytes;
        info->rdi_open_count = slot->rs_open_count;
        info->rdi_configured = slot->rs_configured;
        if (slot->rs_configured) {
            info->rdi_media_bytes = slot->rs_disk.rd_media_bytes;
            info->rdi_backing_bytes = slot->rs_disk.rd_backing_bytes;
            info->rdi_flags = slot->rs_disk.rd_flags;
        }
        return 0;
    default:
        if (!slot->rs_configured)
            return ENXIO;
        return ramdisk_bdev_ioctl(&slot->rs_disk, dev, cmd, addr, flag);
    }
}

int
ramdisk_controller_compression_stats(struct ramdisk_controller *controller,
    int minor_number, struct ramcomp_stats *stats)
{
    struct ramdisk_slot *slot;

    if (controller == 0 || minor_number < 0 ||
        minor_number >= RAMDISK_MAX_DEVICES)
        return EINVAL;
    slot = &controller->rc_slot[minor_number];
    if (!slot->rs_configured ||
        (slot->rs_disk.rd_flags & RAMDISK_F_COMPRESSION) == 0)
        return ENXIO;
    return ramcomp_get_stats(slot->rs_disk.rd_compression, stats);
}

static int
ramdisk_media(const struct ramdisk *ramdisk, unsigned *size)
{
    if (ramdisk == 0 || size == 0)
        return EINVAL;
    if (ramdisk->rd_backing == 0 || ramdisk->rd_backing_bytes == 0 ||
        ramdisk->rd_media_bytes == 0)
        return ENXIO;
    *size = ramdisk->rd_media_bytes;
    return 0;
}

int
ramdisk_bdev_open(const struct ramdisk *ramdisk, dev_t dev, int flag,
    int mode)
{
    unsigned size;

    (void)flag;
    (void)mode;
    if (ramdisk == 0 || minor(dev) != ramdisk->rd_minor)
        return ENXIO;
    return ramdisk_media(ramdisk, &size);
}

int
ramdisk_bdev_close(const struct ramdisk *ramdisk, dev_t dev, int flag,
    int mode)
{
    (void)ramdisk;
    (void)dev;
    (void)flag;
    (void)mode;
    return 0;
}

daddr_t
ramdisk_bdev_size(const struct ramdisk *ramdisk, dev_t dev)
{
    unsigned size;

    if (ramdisk == 0 || minor(dev) != ramdisk->rd_minor)
        return 0;
    if (ramdisk_media(ramdisk, &size) != 0)
        return 0;
    return size >> ramdisk->rd_block_shift;
}

static void
ramdisk_done_error(struct buf *bp, int error)
{
    bp->b_error = error;
    bp->b_flags |= B_ERROR;
    biodone(bp);
}

void
ramdisk_bdev_strategy(const struct ramdisk *ramdisk, struct buf *bp)
{
    unsigned offset;
    unsigned nbytes;
    unsigned size;
    int error;

    error = ramdisk_media(ramdisk, &size);
    if (error != 0) {
        ramdisk_done_error(bp, error);
        return;
    }
    if (minor(bp->b_dev) != ramdisk->rd_minor) {
        ramdisk_done_error(bp, ENXIO);
        return;
    }
    if (bp->b_blkno < 0) {
        ramdisk_done_error(bp, EINVAL);
        return;
    }

    offset = (unsigned)bp->b_blkno << ramdisk->rd_block_shift;
    if (offset >= size) {
        if (offset == size) {
            bp->b_resid = bp->b_bcount;
            biodone(bp);
        } else
            ramdisk_done_error(bp, EINVAL);
        return;
    }

    nbytes = bp->b_bcount;
    bp->b_resid = 0;
    if (nbytes > size - offset) {
        bp->b_resid = nbytes - (size - offset);
        nbytes = size - offset;
        bp->b_bcount = nbytes;
    }

    if (ramdisk->rd_flags & RAMDISK_F_COMPRESSION) {
        if (bp->b_flags & B_READ)
            error = ramcomp_read(ramdisk->rd_compression, offset,
                bp->b_addr, nbytes);
        else
            error = ramcomp_write(ramdisk->rd_compression, offset,
                bp->b_addr, nbytes);
        if (error != 0) {
            ramdisk_done_error(bp, error);
            return;
        }
    } else if (bp->b_flags & B_READ)
        bcopy((const void *)(ramdisk->rd_backing + offset),
            bp->b_addr, nbytes);
    else
        bcopy(bp->b_addr, (void *)(ramdisk->rd_backing + offset),
            nbytes);
    biodone(bp);
}

int
ramdisk_bdev_ioctl(const struct ramdisk *ramdisk, dev_t dev, u_int cmd,
    caddr_t addr, int flag)
{
    (void)flag;
    switch (cmd) {
    case DIOCGETMEDIASIZE:
        *(int *)addr = ramdisk_bdev_size(ramdisk, dev);
        return 0;
    case DIOCDISCARD: {
        const struct disk_discard *range;
        disk_sector_t sectors;
        unsigned offset;
        unsigned bytes;

        range = (const struct disk_discard *)addr;
        if (range == 0)
            return EINVAL;
        sectors = ramdisk->rd_media_bytes / 512u;
        if (range->dd_offset > sectors ||
            range->dd_length > sectors - range->dd_offset)
            return EINVAL;
        if (range->dd_length == 0)
            return 0;
        offset = (unsigned)range->dd_offset * 512u;
        bytes = (unsigned)range->dd_length * 512u;
        if (ramdisk->rd_flags & RAMDISK_F_COMPRESSION) {
            if ((offset | bytes) & RAMCOMP_BLOCK_MASK)
                return EINVAL;
            ramcomp_discard(ramdisk->rd_compression,
                offset >> RAMCOMP_BLOCK_SHIFT,
                bytes >> RAMCOMP_BLOCK_SHIFT);
        } else
            ramdisk_zero((void *)(ramdisk->rd_backing + offset), bytes);
        return 0;
    }
    default:
        return EINVAL;
    }
}
