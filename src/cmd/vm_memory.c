/*
 * Shared physical-VM accounting for userland status commands.
 */
#include <sys/param.h>
#include <sys/sysctl.h>

#include "vm_memory.h"

static int
sysctl_long2(int top, int leaf, long *value)
{
    int mib[2];
    size_t size;

    mib[0] = top;
    mib[1] = leaf;
    size = sizeof(*value);
    return sysctl(mib, 2, value, &size, NULL, 0);
}

static long
pages_to_kb(long pages, long page_size)
{
    if (pages <= 0 || page_size <= 0)
        return 0;
    if (page_size >= 1024)
        return pages * (page_size / 1024);
    return pages / (1024 / page_size);
}

int
vm_memory_read(struct vm_memory_info *info)
{
    long page_size;
    long page_total;
    long page_free;
    long page_reserved;
    long page_bad;
    long page_active;
    long page_usable;
    long phys_bytes;

    if (info == NULL)
        return -1;
    if (sysctl_long2(CTL_HW, HW_PHYSMEM, &phys_bytes) < 0 ||
        sysctl_long2(CTL_HW, HW_PAGESIZE, &page_size) < 0 ||
        sysctl_long2(CTL_VM, VM_PHYSPAGES, &page_total) < 0 ||
        sysctl_long2(CTL_VM, VM_FREEPAGES, &page_free) < 0 ||
        sysctl_long2(CTL_VM, VM_RESERVEDPAGES, &page_reserved) < 0 ||
        sysctl_long2(CTL_VM, VM_BADPAGES, &page_bad) < 0 ||
        sysctl_long2(CTL_VM, VM_OBJECTRESIDENT, &page_active) < 0)
        return -1;

    page_usable = page_total - page_reserved - page_bad;
    if (page_usable < 0)
        page_usable = 0;
    if (page_free < 0)
        page_free = 0;
    if (page_free > page_usable)
        page_free = page_usable;
    if (page_active < 0)
        page_active = 0;
    if (page_active > page_usable - page_free)
        page_active = page_usable - page_free;

    info->vmi_phys_kb = phys_bytes / 1024;
    info->vmi_total_kb = pages_to_kb(page_usable, page_size);
    info->vmi_free_kb = pages_to_kb(page_free, page_size);
    info->vmi_used_kb = info->vmi_total_kb - info->vmi_free_kb;
    info->vmi_active_kb = pages_to_kb(page_active, page_size);
    return 0;
}
