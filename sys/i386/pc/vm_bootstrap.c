#include <sys/errno.h>
#include <vm/vm_page.h>
#include <vm/vm_phys.h>

#include "memory.h"
#include "vm_bootstrap.h"

static const char i386_vm_ram_name[] = "e820 ram";
static const char i386_vm_bootstrap_name[] = "i386 bootstrap";
static struct vm_page_stats i386_vm_stats;

void *
vm_page_md_direct_map(vm_paddr_t paddr, vm_size_t size)
{
    if (size == 0 || paddr >= I386_PHYS_LIMIT ||
        size > I386_PHYS_LIMIT - paddr)
        return (void *)0;
    return (void *)(I386_KERNEL_BASE + paddr);
}

int
vm_page_md_poison(void *argument, vm_paddr_t paddr, uint8_t value,
    int check_only)
{
    volatile uint8_t *bytes;
    unsigned index;

    (void)argument;
    bytes = (volatile uint8_t *)paddr;
    for (index = 0; index < VM_PAGE_SIZE; ++index) {
        if (check_only) {
            if (bytes[index] != value)
                return EFAULT;
        } else {
            bytes[index] = value;
        }
    }
    return 0;
}

static int
i386_vm_bootstrap_pages_reserved(void)
{
    const struct i386_phys_range *range;
    struct vm_page *page;
    i386_u32 allocated_end;
    unsigned index;

    for (index = 0; index < i386_memory_range_count(); ++index) {
        range = i386_memory_range(index);
        allocated_end = i386_memory_allocated_end(index);
        if (range == (const struct i386_phys_range *)0 ||
            allocated_end < range->start ||
            allocated_end > range->end)
            return 0;
        if (allocated_end == range->start)
            continue;
        page = vm_page_lookup(&vm_page_boot_allocator, range->start);
        if (page == (struct vm_page *)0 ||
            page->vmp_state != VM_PAGE_RESERVED)
            return 0;
        page = vm_page_lookup(&vm_page_boot_allocator,
            allocated_end - VM_PAGE_SIZE);
        if (page == (struct vm_page *)0 ||
            page->vmp_state != VM_PAGE_RESERVED)
            return 0;
    }
    return 1;
}

int
i386_vm_bootstrap_init(void)
{
    const struct i386_phys_range *range;
    i386_u32 allocated_end;
    vm_paddr_t metadata_start;
    vm_size_t metadata_size;
    vm_pfn_t metadata_pages;
    unsigned index;
    int error;

    vm_phys_map_init(&vm_phys_boot_map);
    for (index = 0; index < i386_memory_range_count(); ++index) {
        range = i386_memory_range(index);
        if (range == (const struct i386_phys_range *)0)
            return EINVAL;
        error = vm_phys_map_add_ram(&vm_phys_boot_map, range->start,
            range->end - range->start, i386_vm_ram_name);
        if (error != 0)
            return error;
    }

    for (index = 0; index < i386_memory_range_count(); ++index) {
        range = i386_memory_range(index);
        allocated_end = i386_memory_allocated_end(index);
        if (allocated_end > range->start) {
            error = vm_phys_map_reserve(&vm_phys_boot_map,
                range->start, allocated_end - range->start,
                i386_vm_bootstrap_name);
            if (error != 0)
                return error;
        }
    }

    error = vm_page_metadata_reserve(&vm_phys_boot_map, &metadata_start,
        &metadata_size, &metadata_pages);
    if (error != 0 || metadata_pages == 0)
        return error != 0 ? error : EINVAL;
    error = vm_phys_map_finalize(&vm_phys_boot_map);
    if (error != 0)
        return error;
    error = vm_page_bootstrap_init(&vm_phys_boot_map, metadata_start,
        metadata_size);
    if (error != 0)
        return error;
    if (!i386_vm_bootstrap_pages_reserved())
        return EFAULT;
    error = vm_page_bootstrap_stats(&i386_vm_stats);
    if (error != 0)
        return error;

    i386_memory_handoff();
    return 0;
}

int
i386_vm_bootstrap_selftest(void)
{
    int error;

    error = vm_page_bootstrap_selftest();
    if (error != 0)
        return error;
    return vm_page_bootstrap_stats(&i386_vm_stats);
}

i386_u32
i386_vm_total_pages(void)
{
    return i386_vm_stats.vps_total;
}

i386_u32
i386_vm_free_pages(void)
{
    return i386_vm_stats.vps_free;
}

i386_u32
i386_vm_reserved_pages(void)
{
    return i386_vm_stats.vps_reserved;
}
