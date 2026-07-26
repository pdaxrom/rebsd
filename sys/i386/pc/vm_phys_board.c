#include <sys/errno.h>
#include <vm/vm_phys.h>

#include "memory.h"

static const char i386_vm_ram_name[] = "e820 ram";
static const char i386_vm_bootstrap_name[] = "i386 bootstrap";

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

int
vm_phys_board_register(struct vm_phys_map *map, vm_size_t ram_size)
{
    const struct i386_phys_range *range;
    i386_u32 allocated_end;
    unsigned index;
    int error;

    if (map == (struct vm_phys_map *)0 ||
        ram_size !=
        (vm_size_t)i386_memory_total_pages() * I386_PAGE_SIZE)
        return EINVAL;

    for (index = 0; index < i386_memory_range_count(); ++index) {
        range = i386_memory_range(index);
        if (range == (const struct i386_phys_range *)0)
            return EINVAL;
        error = vm_phys_map_add_ram(map, range->start,
            range->end - range->start, i386_vm_ram_name);
        if (error != 0)
            return error;
    }

    for (index = 0; index < i386_memory_range_count(); ++index) {
        range = i386_memory_range(index);
        allocated_end = i386_memory_allocated_end(index);
        if (range == (const struct i386_phys_range *)0 ||
            allocated_end < range->start ||
            allocated_end > range->end)
            return EINVAL;
        if (allocated_end > range->start) {
            error = vm_phys_map_reserve(map,
                range->start, allocated_end - range->start,
                i386_vm_bootstrap_name);
            if (error != 0)
                return error;
        }
    }
    return 0;
}
