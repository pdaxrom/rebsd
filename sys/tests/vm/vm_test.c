/* Host-side tests for the machine-independent VM bootstrap layer. */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <vm/vm_param.h>
#include <vm/vm_phys.h>

#define CHECK(expr) do {                                                \
    if (!(expr)) {                                                      \
        fprintf(stderr, "%s:%d: check failed: %s\n",                  \
            __FILE__, __LINE__, #expr);                                 \
        return 1;                                                       \
    }                                                                   \
} while (0)

static int
check_region(const struct vm_phys_map *map, unsigned index,
    vm_paddr_t start, vm_paddr_t end, enum vm_phys_region_kind kind,
    const char *name)
{
    const struct vm_phys_region *region;

    region = vm_phys_map_region(map, index);
    CHECK(region != NULL);
    CHECK(region->vpr_start == start);
    CHECK(region->vpr_end == end);
    CHECK(region->vpr_kind == kind);
    CHECK(strcmp(region->vpr_name, name) == 0);
    return 0;
}

static int
test_types_and_geometry(void)
{
    vm_vaddr_t vaddr;
    vm_paddr_t paddr;
    vm_size_t size;
    vm_pfn_t pfn;

    CHECK(VM_KVA_BITS == 32 && VM_UVA_BITS == 32 && VM_PA_BITS == 32);
    CHECK(sizeof(vm_vaddr_t) == 4);
    CHECK(sizeof(vm_uaddr_t) == 4);
    CHECK(sizeof(vm_paddr_t) == 4);
    CHECK(sizeof(vm_size_t) == 4);
    CHECK(sizeof(vm_ooffset_t) == 8);
    CHECK(VM_PAGE_SIZE == 4096u);
    CHECK(VM_PAGE_MASK == 4095u);

    CHECK(vm_vaddr_add(0x1000u, 0x2000u, &vaddr) == 0);
    CHECK(vaddr == 0x3000u);
    CHECK(vm_vaddr_add(VM_VADDR_MAX, 1, &vaddr) == EOVERFLOW);
    CHECK(vm_vaddr_add(0, 0, NULL) == EINVAL);
    CHECK(vm_paddr_add(0xfffff000u, 0x1000u, &paddr) == EOVERFLOW);
    CHECK(vm_size_add(VM_SIZE_MAX - 1, 1, &size) == 0);
    CHECK(size == VM_SIZE_MAX);
    CHECK(vm_size_add(VM_SIZE_MAX, 1, &size) == EOVERFLOW);

    CHECK(vm_vaddr_round_page(0, &vaddr) == 0 && vaddr == 0);
    CHECK(vm_vaddr_round_page(1, &vaddr) == 0 && vaddr == 0x1000u);
    CHECK(vm_vaddr_round_page(0x1000u, &vaddr) == 0 &&
        vaddr == 0x1000u);
    CHECK(vm_vaddr_round_page(0xfffff001u, &vaddr) == EOVERFLOW);
    CHECK(vm_paddr_round_page(0x1234u, &paddr) == 0 &&
        paddr == 0x2000u);
    CHECK(vm_size_round_page(0x2001u, &size) == 0 && size == 0x3000u);
    CHECK(vm_vaddr_trunc_page(0x1fffu) == 0x1000u);
    CHECK(vm_paddr_trunc_page(0x2abcu) == 0x2000u);
    CHECK(vm_vaddr_page_aligned(0x3000u));
    CHECK(!vm_vaddr_page_aligned(0x3001u));
    CHECK(vm_paddr_page_aligned(0));
    CHECK(vm_size_page_aligned(0x4000u));

    CHECK(vm_paddr_to_pfn(0x12345000u, &pfn) == 0);
    CHECK(pfn == 0x12345u);
    CHECK(vm_paddr_to_pfn(0x12345001u, &pfn) == EINVAL);
    CHECK(vm_pfn_to_paddr(0x12345u, &paddr) == 0);
    CHECK(paddr == 0x12345000u);
    CHECK(vm_pfn_to_paddr((VM_PADDR_MAX >> VM_PAGE_SHIFT) + 1,
        &paddr) == EOVERFLOW);
    CHECK(vm_size_to_pages(0, &pfn) == 0 && pfn == 0);
    CHECK(vm_size_to_pages(1, &pfn) == 0 && pfn == 1);
    CHECK(vm_size_to_pages(0x2000u, &pfn) == 0 && pfn == 2);
    CHECK(vm_size_to_pages(VM_SIZE_MAX, &pfn) == EOVERFLOW);
    return 0;
}

static int
test_ram_insertion_and_coalescing(void)
{
    struct vm_phys_map map;
    char ram_name[] = "ram";

    vm_phys_map_init(&map);
    CHECK(vm_phys_map_count(&map) == 0);
    CHECK(vm_phys_map_add_ram(&map, 0x4000u, 0x2000u, "ram") == 0);
    CHECK(vm_phys_map_add_ram(&map, 0x1000u, 0x3000u, ram_name) == 0);
    CHECK(vm_phys_map_add_ram(&map, 0x6000u, 0x2000u, "ram") == 0);
    CHECK(vm_phys_map_count(&map) == 1);
    CHECK(check_region(&map, 0, 0x1000u, 0x8000u,
        VM_PHYS_AVAILABLE, "ram") == 0);

    CHECK(vm_phys_map_add_ram(&map, 0xa000u, 0x1000u, "bank1") == 0);
    CHECK(vm_phys_map_add_ram(&map, 0xb000u, 0x1000u, "bank2") == 0);
    CHECK(vm_phys_map_count(&map) == 3);
    CHECK(check_region(&map, 1, 0xa000u, 0xb000u,
        VM_PHYS_AVAILABLE, "bank1") == 0);
    CHECK(check_region(&map, 2, 0xb000u, 0xc000u,
        VM_PHYS_AVAILABLE, "bank2") == 0);

    CHECK(vm_phys_map_add_ram(&map, 0x7000u, 0x2000u, "overlap") ==
        EBUSY);
    CHECK(vm_phys_map_add_ram(&map, 1, 0x1000u, "bad") == EINVAL);
    CHECK(vm_phys_map_add_ram(&map, 0xd000u, 1, "bad") == EINVAL);
    CHECK(vm_phys_map_add_ram(&map, 0xfffff000u, 0x2000u, "bad") ==
        EOVERFLOW);
    CHECK(vm_phys_map_add_ram(&map, 0xd000u, 0x1000u, "") == EINVAL);
    CHECK(vm_phys_map_validate(&map) == 0);
    return 0;
}

static int
test_reservation_splitting(void)
{
    struct vm_phys_map map;
    vm_size_t total;

    vm_phys_map_init(&map);
    CHECK(vm_phys_map_add_ram(&map, 0x1000u, 0x8000u, "ram") == 0);
    CHECK(vm_phys_map_reserve(&map, 0x3000u, 0x2000u, "middle") == 0);
    CHECK(vm_phys_map_count(&map) == 3);
    CHECK(check_region(&map, 0, 0x1000u, 0x3000u,
        VM_PHYS_AVAILABLE, "ram") == 0);
    CHECK(check_region(&map, 1, 0x3000u, 0x5000u,
        VM_PHYS_RESERVED, "middle") == 0);
    CHECK(check_region(&map, 2, 0x5000u, 0x9000u,
        VM_PHYS_AVAILABLE, "ram") == 0);

    CHECK(vm_phys_map_reserve(&map, 0x1000u, 0x1000u, "guard") == 0);
    CHECK(vm_phys_map_reserve(&map, 0x2000u, 0x1000u, "guard") == 0);
    CHECK(vm_phys_map_reserve(&map, 0x7000u, 0x2000u, "tail") == 0);
    CHECK(vm_phys_map_count(&map) == 4);
    CHECK(check_region(&map, 0, 0x1000u, 0x3000u,
        VM_PHYS_RESERVED, "guard") == 0);
    CHECK(check_region(&map, 3, 0x7000u, 0x9000u,
        VM_PHYS_RESERVED, "tail") == 0);
    CHECK(vm_phys_map_reserve(&map, 0x4000u, 0x1000u, "overlap") ==
        EBUSY);
    CHECK(vm_phys_map_reserve(&map, 0xa000u, 0x1000u, "outside") ==
        ENOENT);
    CHECK(vm_phys_map_total(&map, VM_PHYS_AVAILABLE, &total) == 0);
    CHECK(total == 0x2000u);
    CHECK(vm_phys_map_total(&map, VM_PHYS_RESERVED, &total) == 0);
    CHECK(total == 0x6000u);
    CHECK(vm_phys_map_finalize(&map) == 0);
    CHECK(map.vpm_finalized == 1);
    CHECK(vm_phys_map_add_ram(&map, 0xa000u, 0x1000u, "late") == EINVAL);
    CHECK(vm_phys_map_reserve(&map, 0x5000u, 0x1000u, "late") ==
        EINVAL);
    CHECK(vm_phys_map_finalize(&map) == EINVAL);
    return 0;
}

static int
test_exact_reservation(void)
{
    struct vm_phys_map map;

    vm_phys_map_init(&map);
    CHECK(vm_phys_map_add_ram(&map, 0, 0x2000u, "ram") == 0);
    CHECK(vm_phys_map_reserve(&map, 0, 0x2000u, "all") == 0);
    CHECK(vm_phys_map_count(&map) == 1);
    CHECK(check_region(&map, 0, 0, 0x2000u,
        VM_PHYS_RESERVED, "all") == 0);
    return 0;
}

static int
test_clipping(void)
{
    struct vm_phys_map map;
    vm_paddr_t start;
    vm_size_t size;

    vm_phys_map_init(&map);
    CHECK(vm_phys_map_add_ram(&map, 0x2000u, 0x4000u, "bank0") == 0);
    CHECK(vm_phys_map_add_ram(&map, 0x8000u, 0x2000u, "bank1") == 0);
    CHECK(vm_phys_map_reserve(&map, 0x3000u, 0x1000u, "reserved") == 0);

    CHECK(vm_phys_map_clip(&map, 0, 0x4000u, &start, &size) == 0);
    CHECK(start == 0x2000u && size == 0x2000u);
    CHECK(vm_phys_map_clip(&map, 0x3000u, 0x3000u, &start, &size) == 0);
    CHECK(start == 0x3000u && size == 0x3000u);
    CHECK(vm_phys_map_clip(&map, 0x5000u, 0x4000u, &start, &size) == 0);
    CHECK(start == 0x5000u && size == 0x1000u);
    CHECK(vm_phys_map_clip(&map, 0x7000u, 0x2000u, &start, &size) == 0);
    CHECK(start == 0x8000u && size == 0x1000u);
    CHECK(vm_phys_map_clip(&map, 0x6000u, 0x2000u, &start, &size) ==
        ENOENT);
    CHECK(vm_phys_map_clip(&map, 0xfffff000u, 0x2000u,
        &start, &size) == EOVERFLOW);
    return 0;
}

static int
test_capacity_and_malformed_maps(void)
{
    struct vm_phys_map map;
    unsigned i;

    vm_phys_map_init(&map);
    for (i = 0; i < VM_PHYS_MAX_REGIONS; ++i)
        CHECK(vm_phys_map_add_ram(&map, i * 0x2000u, 0x1000u,
            "bank") == 0);
    CHECK(vm_phys_map_add_ram(&map, VM_PHYS_MAX_REGIONS * 0x2000u,
        0x1000u, "full") == ENOSPC);
    CHECK(vm_phys_map_validate(&map) == 0);

    vm_phys_map_init(&map);
    map.vpm_count = 1;
    map.vpm_regions[0].vpr_start = 1;
    map.vpm_regions[0].vpr_end = 0x1000u;
    map.vpm_regions[0].vpr_kind = VM_PHYS_AVAILABLE;
    map.vpm_regions[0].vpr_name = "bad alignment";
    CHECK(vm_phys_map_validate(&map) == EINVAL);

    vm_phys_map_init(&map);
    map.vpm_count = 2;
    map.vpm_regions[0].vpr_start = 0;
    map.vpm_regions[0].vpr_end = 0x3000u;
    map.vpm_regions[0].vpr_kind = VM_PHYS_AVAILABLE;
    map.vpm_regions[0].vpr_name = "left";
    map.vpm_regions[1].vpr_start = 0x2000u;
    map.vpm_regions[1].vpr_end = 0x4000u;
    map.vpm_regions[1].vpr_kind = VM_PHYS_RESERVED;
    map.vpm_regions[1].vpr_name = "right";
    CHECK(vm_phys_map_validate(&map) == EINVAL);

    map.vpm_regions[1].vpr_start = 0x3000u;
    map.vpm_regions[1].vpr_kind = 0;
    CHECK(vm_phys_map_validate(&map) == EINVAL);
    map.vpm_regions[1].vpr_kind = VM_PHYS_RESERVED;
    map.vpm_regions[1].vpr_name = NULL;
    CHECK(vm_phys_map_validate(&map) == EINVAL);
    return 0;
}

int
main(void)
{
    if (test_types_and_geometry() != 0)
        return 1;
    if (test_ram_insertion_and_coalescing() != 0)
        return 1;
    if (test_reservation_splitting() != 0)
        return 1;
    if (test_exact_reservation() != 0)
        return 1;
    if (test_clipping() != 0)
        return 1;
    if (test_capacity_and_malformed_maps() != 0)
        return 1;
    puts("vm host tests: ok");
    return 0;
}
