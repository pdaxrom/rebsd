/*
 * Show a compact memory summary using sysctl.
 */
#include <sys/param.h>
#include <sys/map.h>
#include <sys/sysctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static long
sysctl_long2(int top, int leaf, const char *name)
{
    int mib[2];
    long value;
    size_t size;

    mib[0] = top;
    mib[1] = leaf;
    size = sizeof(value);
    if (sysctl(mib, 2, &value, &size, NULL, 0) < 0) {
        perror(name);
        exit(1);
    }
    return value;
}

static long
read_swap_free_kb(void)
{
    struct mapent *map;
    int mib[2];
    size_t size;
    size_t i;
    size_t nentries;
    long blocks;

    mib[0] = CTL_VM;
    mib[1] = VM_SWAPMAP;
    size = 0;
    if (sysctl(mib, 2, NULL, &size, NULL, 0) < 0) {
        perror("vm.swapmap");
        exit(1);
    }
    if (size == 0)
        return 0;

    map = malloc(size);
    if (map == NULL) {
        perror("malloc");
        exit(1);
    }
    if (sysctl(mib, 2, map, &size, NULL, 0) < 0) {
        perror("vm.swapmap");
        exit(1);
    }

    blocks = 0;
    nentries = size / sizeof(*map);
    for (i = 0; i < nentries && map[i].m_size != 0; ++i)
        blocks += map[i].m_size;
    free(map);
    return blocks * DEV_BSIZE / 1024;
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
main(int argc, char **argv)
{
    long phys_kb, total_kb, used_kb, free_kb, active_kb;
    long page_size, page_total, page_free, page_reserved, page_bad;
    long page_active, page_usable;
    long swap_total_kb, swap_used_kb, swap_free_kb;
    int human = 0;
    int ch;

    while ((ch = getopt(argc, argv, "h")) != EOF) {
        switch (ch) {
        case 'h':
            human = 1;
            break;
        default:
            fprintf(stderr, "usage: free [-h]\n");
            return 1;
        }
    }

    phys_kb = sysctl_long2(CTL_HW, HW_PHYSMEM, "hw.physmem") / 1024;
    page_size = sysctl_long2(CTL_HW, HW_PAGESIZE, "hw.pagesize");
    page_total = sysctl_long2(CTL_VM, VM_PHYSPAGES, "vm.page_total");
    page_free = sysctl_long2(CTL_VM, VM_FREEPAGES, "vm.page_free");
    page_reserved = sysctl_long2(CTL_VM, VM_RESERVEDPAGES,
        "vm.page_reserved");
    page_bad = sysctl_long2(CTL_VM, VM_BADPAGES, "vm.page_bad");
    page_active = sysctl_long2(CTL_VM, VM_OBJECTRESIDENT,
        "vm.object_resident");
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
    total_kb = pages_to_kb(page_usable, page_size);
    free_kb = pages_to_kb(page_free, page_size);
    used_kb = total_kb - free_kb;
    active_kb = pages_to_kb(page_active, page_size);
    swap_total_kb = sysctl_long2(CTL_VM, VM_SWAPTOTAL,
        "vm.swap_total") / 1024;
    swap_free_kb = read_swap_free_kb();
    if (swap_free_kb > swap_total_kb)
        swap_free_kb = swap_total_kb;
    swap_used_kb = swap_total_kb - swap_free_kb;

    if (human) {
        printf("%-6s %6s %6s %6s %6s\n",
            "", "total", "used", "free", "act");
        printf("%-6s %5ldK %5ldK %5ldK %5ldK\n", "Mem:",
            total_kb, used_kb, free_kb, active_kb);
        printf("%-6s %5ldK %5ldK %5ldK\n", "Swap:",
            swap_total_kb, swap_used_kb, swap_free_kb);
        printf("%-6s %5ldK\n", "Phys:", phys_kb);
    } else {
        printf("%-6s %6s %6s %6s %6s\n",
            "", "total", "used", "free", "act");
        printf("%-6s %6ld %6ld %6ld %6ld\n", "Mem:",
            total_kb, used_kb, free_kb, active_kb);
        printf("%-6s %6ld %6ld %6ld\n", "Swap:",
            swap_total_kb, swap_used_kb, swap_free_kb);
        printf("%-6s %6ld\n", "Phys:", phys_kb);
    }
    return 0;
}
