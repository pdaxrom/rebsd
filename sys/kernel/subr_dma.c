/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef REBSD_DMA_HOST_TEST
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/dma.h>
#define DMA_PTR_UINT           uintptr_t
#define DMA_MEMZERO(p, n)      memset((p), 0, (n))
#define DMA_LOCK()             0
#define DMA_UNLOCK(s)          ((void)(s))
#else
#include <sys/param.h>
#include <sys/dma.h>
#include <sys/errno.h>
#include <sys/systm.h>
#define DMA_PTR_UINT           u_int
#define DMA_MEMZERO(p, n)      bzero((caddr_t)(p), (n))
#define DMA_LOCK()             splhigh()
#define DMA_UNLOCK(s)          splx(s)
#endif

#define DMA_MAX_RANGES         (DMA_MAX_ALLOCS + 1)

struct dma_range {
    size_t dr_offset;
    size_t dr_size;
};

struct dma_allocation {
    size_t da_offset;
    size_t da_size;
    unsigned da_cookie;
    unsigned da_used;
};

struct dma_pool {
    unsigned char *dp_vaddr;
    dma_addr_t dp_paddr;
    size_t dp_size;
    unsigned dp_capabilities;
    const struct dma_backend_ops *dp_ops;
    struct dma_range dp_ranges[DMA_MAX_RANGES];
    struct dma_allocation dp_allocs[DMA_MAX_ALLOCS];
    unsigned dp_nranges;
    unsigned dp_next_cookie;
    unsigned dp_initialized;
};

static struct dma_pool dma_pool;

static int
dma_powerof2(size_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

static int
dma_has_active_allocations(void)
{
    unsigned i;

    for (i = 0; i < DMA_MAX_ALLOCS; ++i)
        if (dma_pool.dp_allocs[i].da_used)
            return 1;
    return 0;
}

static int
dma_mem_valid(const struct dma_mem *mem, unsigned *slot)
{
    const struct dma_allocation *alloc;
    size_t offset;
    unsigned i;

    if (mem == 0 || mem->dm_cookie == 0 || mem->dm_vaddr == 0)
        return 0;
    if (!dma_pool.dp_initialized)
        return 0;
    if (mem->dm_paddr < dma_pool.dp_paddr)
        return 0;
    offset = (size_t)(mem->dm_paddr - dma_pool.dp_paddr);
    if (offset > dma_pool.dp_size || mem->dm_size > dma_pool.dp_size - offset)
        return 0;
    if (mem->dm_vaddr != dma_pool.dp_vaddr + offset)
        return 0;

    for (i = 0; i < DMA_MAX_ALLOCS; ++i) {
        alloc = &dma_pool.dp_allocs[i];
        if (alloc->da_used && alloc->da_cookie == mem->dm_cookie &&
            alloc->da_offset == offset && alloc->da_size == mem->dm_size) {
            if (slot != 0)
                *slot = i;
            return 1;
        }
    }
    return 0;
}

static int
dma_take_range(unsigned index, size_t offset, size_t size)
{
    struct dma_range *range;
    size_t before;
    size_t after;
    unsigned i;

    range = &dma_pool.dp_ranges[index];
    before = offset - range->dr_offset;
    after = range->dr_size - before - size;

    if (before != 0 && after != 0) {
        if (dma_pool.dp_nranges >= DMA_MAX_RANGES)
            return ENOMEM;
        for (i = dma_pool.dp_nranges; i > index + 1; --i)
            dma_pool.dp_ranges[i] = dma_pool.dp_ranges[i - 1];
        dma_pool.dp_ranges[index].dr_size = before;
        dma_pool.dp_ranges[index + 1].dr_offset = offset + size;
        dma_pool.dp_ranges[index + 1].dr_size = after;
        ++dma_pool.dp_nranges;
    } else if (before != 0) {
        range->dr_size = before;
    } else if (after != 0) {
        range->dr_offset = offset + size;
        range->dr_size = after;
    } else {
        for (i = index; i + 1 < dma_pool.dp_nranges; ++i)
            dma_pool.dp_ranges[i] = dma_pool.dp_ranges[i + 1];
        --dma_pool.dp_nranges;
    }
    return 0;
}

static int
dma_return_range(size_t offset, size_t size)
{
    struct dma_range *left;
    struct dma_range *right;
    size_t end;
    unsigned pos;
    unsigned i;
    int join_left;
    int join_right;

    end = offset + size;
    pos = 0;
    while (pos < dma_pool.dp_nranges &&
        dma_pool.dp_ranges[pos].dr_offset < offset)
        ++pos;

    left = pos == 0 ? 0 : &dma_pool.dp_ranges[pos - 1];
    right = pos == dma_pool.dp_nranges ? 0 : &dma_pool.dp_ranges[pos];
    if (left != 0 && left->dr_offset + left->dr_size > offset)
        return EINVAL;
    if (right != 0 && end > right->dr_offset)
        return EINVAL;

    join_left = left != 0 && left->dr_offset + left->dr_size == offset;
    join_right = right != 0 && end == right->dr_offset;
    if (join_left && join_right) {
        left->dr_size += size + right->dr_size;
        for (i = pos; i + 1 < dma_pool.dp_nranges; ++i)
            dma_pool.dp_ranges[i] = dma_pool.dp_ranges[i + 1];
        --dma_pool.dp_nranges;
    } else if (join_left) {
        left->dr_size += size;
    } else if (join_right) {
        right->dr_offset = offset;
        right->dr_size += size;
    } else {
        if (dma_pool.dp_nranges >= DMA_MAX_RANGES)
            return ENOMEM;
        for (i = dma_pool.dp_nranges; i > pos; --i)
            dma_pool.dp_ranges[i] = dma_pool.dp_ranges[i - 1];
        dma_pool.dp_ranges[pos].dr_offset = offset;
        dma_pool.dp_ranges[pos].dr_size = size;
        ++dma_pool.dp_nranges;
    }
    return 0;
}

int
dma_pool_init(void *vaddr, dma_addr_t paddr, size_t size,
    unsigned capabilities, const struct dma_backend_ops *ops)
{
    dma_addr_t maxaddr;
    int s;

    maxaddr = (dma_addr_t)~0u;
    if (vaddr == 0 || size == 0)
        return EINVAL;
    if ((capabilities & ~DMA_CAPABILITY_MASK) != 0 ||
        (capabilities & DMA_CONTIGUOUS) == 0)
        return EINVAL;
    if (size - 1 > (size_t)(maxaddr - paddr))
        return EINVAL;

    s = DMA_LOCK();
    if (dma_pool.dp_initialized && dma_has_active_allocations()) {
        DMA_UNLOCK(s);
        return EBUSY;
    }
    DMA_MEMZERO(&dma_pool, sizeof(dma_pool));
    dma_pool.dp_vaddr = (unsigned char *)vaddr;
    dma_pool.dp_paddr = paddr;
    dma_pool.dp_size = size;
    dma_pool.dp_capabilities = capabilities;
    dma_pool.dp_ops = ops;
    dma_pool.dp_ranges[0].dr_offset = 0;
    dma_pool.dp_ranges[0].dr_size = size;
    dma_pool.dp_nranges = 1;
    dma_pool.dp_next_cookie = 1;
    dma_pool.dp_initialized = 1;
    DMA_UNLOCK(s);
    return 0;
}

int
dma_pool_ready(void)
{
    return dma_pool.dp_initialized != 0;
}

size_t
dma_pool_size(void)
{
    if (!dma_pool.dp_initialized)
        return 0;
    return dma_pool.dp_size;
}

size_t
dma_pool_available(void)
{
    size_t available;
    unsigned i;
    int s;

    s = DMA_LOCK();
    available = 0;
    if (dma_pool.dp_initialized)
        for (i = 0; i < dma_pool.dp_nranges; ++i)
            available += dma_pool.dp_ranges[i].dr_size;
    DMA_UNLOCK(s);
    return available;
}

int
dma_alloc(struct dma_mem *mem, size_t size, size_t alignment, unsigned flags)
{
    struct dma_range *range;
    struct dma_allocation *alloc;
    dma_addr_t start;
    dma_addr_t aligned;
    dma_addr_t maxaddr;
    size_t offset;
    size_t padding;
    unsigned alloc_slot;
    unsigned range_slot;
    unsigned cookie;
    unsigned requirements;
    int error;
    int found;
    int s;

    if (mem == 0 || size == 0 || !dma_powerof2(alignment))
        return EINVAL;
    if (mem->dm_cookie != 0)
        return EBUSY;
    if ((flags & ~DMA_FLAG_MASK) != 0)
        return EINVAL;

    s = DMA_LOCK();
    if (!dma_pool.dp_initialized) {
        DMA_UNLOCK(s);
        return ENXIO;
    }
    requirements = flags & DMA_CAPABILITY_MASK;
    if ((requirements & ~dma_pool.dp_capabilities) != 0) {
        DMA_UNLOCK(s);
        return EOPNOTSUPP;
    }

    found = 0;
    alloc_slot = 0;
    for (alloc_slot = 0; alloc_slot < DMA_MAX_ALLOCS; ++alloc_slot)
        if (!dma_pool.dp_allocs[alloc_slot].da_used) {
            found = 1;
            break;
        }
    if (!found) {
        DMA_UNLOCK(s);
        return ENOMEM;
    }

    maxaddr = (dma_addr_t)~0u;
    error = ENOMEM;
    offset = 0;
    for (range_slot = 0; range_slot < dma_pool.dp_nranges; ++range_slot) {
        range = &dma_pool.dp_ranges[range_slot];
        start = dma_pool.dp_paddr + range->dr_offset;
        if (alignment - 1 > (size_t)maxaddr ||
            start > maxaddr - (dma_addr_t)(alignment - 1))
            continue;
        aligned = (start + (dma_addr_t)(alignment - 1)) &
            ~(dma_addr_t)(alignment - 1);
        padding = (size_t)(aligned - start);
        if (padding > range->dr_size || size > range->dr_size - padding)
            continue;
        offset = range->dr_offset + padding;
        if (((DMA_PTR_UINT)(dma_pool.dp_vaddr + offset) &
            (alignment - 1)) != 0)
            continue;
        error = dma_take_range(range_slot, offset, size);
        if (error == 0)
            break;
    }
    if (error != 0) {
        DMA_UNLOCK(s);
        return error;
    }

    cookie = dma_pool.dp_next_cookie++;
    if (cookie == 0) {
        cookie = dma_pool.dp_next_cookie++;
        if (cookie == 0)
            cookie = 1;
    }
    alloc = &dma_pool.dp_allocs[alloc_slot];
    alloc->da_offset = offset;
    alloc->da_size = size;
    alloc->da_cookie = cookie;
    alloc->da_used = 1;

    mem->dm_vaddr = dma_pool.dp_vaddr + offset;
    mem->dm_paddr = dma_pool.dp_paddr + offset;
    mem->dm_size = size;
    mem->dm_align = alignment;
    mem->dm_flags = flags | dma_pool.dp_capabilities;
    mem->dm_cookie = cookie;
    DMA_UNLOCK(s);

    if (flags & DMA_ZERO)
        DMA_MEMZERO(mem->dm_vaddr, mem->dm_size);
    return 0;
}

int
dma_free(struct dma_mem *mem)
{
    struct dma_allocation *alloc;
    unsigned slot;
    int error;
    int s;

    s = DMA_LOCK();
    if (!dma_mem_valid(mem, &slot)) {
        DMA_UNLOCK(s);
        return EINVAL;
    }
    alloc = &dma_pool.dp_allocs[slot];
    error = dma_return_range(alloc->da_offset, alloc->da_size);
    if (error == 0)
        DMA_MEMZERO(alloc, sizeof(*alloc));
    DMA_UNLOCK(s);
    if (error == 0)
        DMA_MEMZERO(mem, sizeof(*mem));
    return error;
}

static int
dma_sync(struct dma_mem *mem, size_t offset, size_t length,
    enum dma_direction direction, int for_device)
{
    const struct dma_backend_ops *ops;
    int s;

    if (direction != DMA_TO_DEVICE && direction != DMA_FROM_DEVICE &&
        direction != DMA_BIDIRECTIONAL)
        return EINVAL;

    s = DMA_LOCK();
    if (!dma_mem_valid(mem, 0) || offset > mem->dm_size ||
        length > mem->dm_size - offset) {
        DMA_UNLOCK(s);
        return EINVAL;
    }
    ops = dma_pool.dp_ops;
    if (length != 0 && ops != 0) {
        if (for_device && ops->dbo_sync_for_device != 0)
            ops->dbo_sync_for_device(mem, offset, length, direction);
        if (!for_device && ops->dbo_sync_for_cpu != 0)
            ops->dbo_sync_for_cpu(mem, offset, length, direction);
    }
    DMA_UNLOCK(s);
    return 0;
}

int
dma_sync_for_device(struct dma_mem *mem, size_t offset, size_t length,
    enum dma_direction direction)
{
    return dma_sync(mem, offset, length, direction, 1);
}

int
dma_sync_for_cpu(struct dma_mem *mem, size_t offset, size_t length,
    enum dma_direction direction)
{
    return dma_sync(mem, offset, length, direction, 0);
}
