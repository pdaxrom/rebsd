/*
 * i686 coherent DMA-pool attachment for the machine-independent allocator.
 */

#include <sys/dma.h>
#include <sys/errno.h>
#include <sys/systm.h>

#include <machine/layout.h>
#include <vm/vmspace.h>

#include "memory.h"

#define I386_DMA_POOL_BYTES     (512u * 1024u)
#define I386_DMA_POOL_ALIGN     4096u

static unsigned char i386_dma_storage[I386_DMA_POOL_BYTES]
    __attribute__((aligned(I386_DMA_POOL_ALIGN)));
static int i386_dma_attached;

static int
i386_dma_map_add(struct dma_map *map, dma_addr_t address, size_t length)
{
    struct dma_segment *segment;
    size_t boundary_left;
    size_t chunk;
    unsigned index;

    while (length != 0) {
        chunk = length;
        if (chunk > map->dm_max_segment_size)
            chunk = map->dm_max_segment_size;
        if (map->dm_boundary != 0) {
            boundary_left = map->dm_boundary -
                ((size_t)address & (map->dm_boundary - 1u));
            if (chunk > boundary_left)
                chunk = boundary_left;
        }
        if (address > map->dm_max_address || chunk == 0 ||
            chunk - 1 > (size_t)(map->dm_max_address - address))
            return EFBIG;

        index = map->dm_segment_count;
        if (index != 0) {
            segment = &map->dm_segments[index - 1u];
            if (segment->ds_addr + segment->ds_len == address &&
                chunk <= map->dm_max_segment_size - segment->ds_len &&
                (map->dm_boundary == 0 ||
                ((size_t)segment->ds_addr &
                ~(map->dm_boundary - 1u)) ==
                ((size_t)address & ~(map->dm_boundary - 1u)))) {
                segment->ds_len += chunk;
                length -= chunk;
                if (length != 0 &&
                    chunk > (size_t)(map->dm_max_address - address))
                    return EFBIG;
                address += (dma_addr_t)chunk;
                continue;
            }
        }
        if (index >= map->dm_max_segments)
            return EFBIG;
        map->dm_segments[index].ds_addr = address;
        map->dm_segments[index].ds_len = chunk;
        ++map->dm_segment_count;
        length -= chunk;
        if (length != 0 &&
            chunk > (size_t)(map->dm_max_address - address))
            return EFBIG;
        address += (dma_addr_t)chunk;
    }
    return 0;
}

static int
i386_dma_map_load(struct dma_map *map, void *vaddr, size_t size,
    enum dma_direction direction)
{
    struct vmspace *vmspace;
    vm_vaddr_t address;
    vm_size_t offset;
    vm_size_t chunk;
    vm_size_t remaining;
    unsigned page_count;
    unsigned page_index;
    int error;

    address = (vm_vaddr_t)(u_int)vaddr;
    if (address < I386_USER_VADDR_START) {
        if (size > I386_USER_VADDR_START - address)
            return EFAULT;
        return i386_dma_map_add(map, (dma_addr_t)address, size);
    }
    if (address >= I386_KERNEL_BASE) {
        address -= I386_KERNEL_BASE;
        if (address >= I386_DIRECT_MAP_SIZE ||
            size > I386_DIRECT_MAP_SIZE - address)
            return EFAULT;
        return i386_dma_map_add(map, (dma_addr_t)address, size);
    }
    if (address >= I386_USER_VADDR_END ||
        size > I386_USER_VADDR_END - address)
        return EFAULT;
    vmspace = vmspace_current();
    if (vmspace == 0)
        return EFAULT;
    error = vmspace_pin_pages(vmspace, address, (vm_size_t)size,
        direction == DMA_TO_DEVICE ? VM_PROT_READ :
        (direction == DMA_FROM_DEVICE ? VM_PROT_WRITE :
        VM_PROT_READ | VM_PROT_WRITE),
        map->dm_backend_pages, DMA_MAP_MAX_SEGMENTS, &page_count);
    if (error != 0)
        return error;
    map->dm_backend_cookie = vmspace;
    map->dm_backend_page_count = page_count;
    offset = address & VM_PAGE_MASK;
    remaining = (vm_size_t)size;
    for (page_index = 0; page_index < page_count; ++page_index) {
        chunk = VM_PAGE_SIZE - offset;
        if (chunk > remaining)
            chunk = remaining;
        error = i386_dma_map_add(map,
            (dma_addr_t)(map->dm_backend_pages[page_index]->vmp_paddr +
            offset), chunk);
        if (error != 0) {
            (void)vmspace_unpin_pages(map->dm_backend_pages,
                page_count);
            map->dm_backend_cookie = 0;
            map->dm_backend_page_count = 0;
            map->dm_segment_count = 0;
            return error;
        }
        remaining -= chunk;
        offset = 0;
    }
    if (remaining == 0)
        return 0;
    (void)vmspace_unpin_pages(map->dm_backend_pages, page_count);
    map->dm_backend_cookie = 0;
    map->dm_backend_page_count = 0;
    map->dm_segment_count = 0;
    return EFAULT;
}

static void
i386_dma_map_unload(struct dma_map *map)
{
    if (map->dm_backend_page_count != 0)
        (void)vmspace_unpin_pages(map->dm_backend_pages,
            map->dm_backend_page_count);
    map->dm_backend_cookie = 0;
    map->dm_backend_page_count = 0;
}

static const struct dma_backend_ops i386_dma_ops = {
    0,
    0,
    i386_dma_map_load,
    i386_dma_map_unload,
    0,
    0,
};

int
i386_dma_attach(void)
{
    dma_addr_t paddr;
    int error;

    if (i386_dma_attached)
        return 0;
    paddr = (dma_addr_t)(unsigned long)i386_dma_storage;
    error = dma_pool_init(i386_dma_storage, paddr,
        sizeof(i386_dma_storage),
        DMA_32BIT | DMA_COHERENT | DMA_CONTIGUOUS, &i386_dma_ops);
    if (error != 0) {
        printf("dma: i686 pool initialization failed, error=%d\n",
            error);
        return error;
    }
    i386_dma_attached = 1;
    printf("dma: i686 coherent pool phys=%x size=%u align=%u\n",
        paddr, (unsigned)sizeof(i386_dma_storage),
        I386_DMA_POOL_ALIGN);
    return 0;
}

void
dmaattach(int unit)
{
    (void)unit;
    (void)i386_dma_attach();
}
