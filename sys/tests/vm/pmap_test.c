/* Host-side tests for the shared 32-bit MIPS pmap. */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <vm/pmap.h>
#include <vm/vm_object.h>
#include <vm/vmspace.h>

#define TEST_RAM_SIZE   (64u * VM_PAGE_SIZE)
#define TEST_VADDR      0x10000000u
#define TEST_VADDR2     0x10002000u
#define TEST_SHARED     0x10004000u
#define TEST_PRESSURE   0x20000000u
#define TEST_PRESSURE_PAGES 70u

#define CHECK(expr) do {                                                \
    if (!(expr)) {                                                      \
        fprintf(stderr, "%s:%d: check failed: %s\n",                  \
            __FILE__, __LINE__, #expr);                                 \
        return 1;                                                       \
    }                                                                   \
} while (0)

static unsigned char test_ram[TEST_RAM_SIZE];
static unsigned test_asid;
static unsigned test_entryhi;
static unsigned test_entrylo0;
static unsigned test_entrylo1;
static unsigned test_tlb_updates;
static unsigned test_tlb_invalidations;
static unsigned test_tlb_flushes;
static unsigned test_syncs;

unsigned
pmap_md_tlb_entries(void)
{
    return 16;
}

unsigned
pmap_md_wired_entries(void)
{
    return 2;
}

void
pmap_md_activate(unsigned asid)
{
    test_asid = asid;
}

void
pmap_md_tlb_update(unsigned entryhi, unsigned entrylo0, unsigned entrylo1)
{
    test_entryhi = entryhi;
    test_entrylo0 = entrylo0;
    test_entrylo1 = entrylo1;
    ++test_tlb_updates;
}

int
pmap_md_tlb_invalidate(unsigned entryhi)
{
    (void)entryhi;
    ++test_tlb_invalidations;
    return 1;
}

void
pmap_md_tlb_flush(void)
{
    ++test_tlb_flushes;
}

void *
pmap_md_direct_map(vm_paddr_t paddr, vm_size_t size,
    enum pmap_cache cache)
{
    (void)cache;
    if (size == 0 || paddr > TEST_RAM_SIZE ||
        size > TEST_RAM_SIZE - paddr)
        return NULL;
    return &test_ram[paddr];
}

int
pmap_md_page_sync(vm_paddr_t paddr, unsigned operations)
{
    if (!vm_paddr_page_aligned(paddr) || paddr >= TEST_RAM_SIZE ||
        operations == 0)
        return EINVAL;
    ++test_syncs;
    return 0;
}

static int
test_page_alloc(struct vm_page_allocator *allocator,
    struct vm_page **result)
{
    struct vm_page_request request;

    vm_page_request_init(&request);
    request.vpr_state = VM_PAGE_ACTIVE;
    return vm_page_alloc(allocator, &request, result);
}

static int
test_pmap(void)
{
    struct vm_page_allocator allocator;
    struct vm_phys_map map;
    struct pmap_stats stats;
    struct vm_page *page1;
    struct vm_page *page2;
    struct vm_page *page3;
    struct pmap *pmap1;
    struct pmap *pmap2;
    struct pmap *pmap3;
    struct vm_page metadata[TEST_RAM_SIZE / VM_PAGE_SIZE];
    vm_paddr_t paddr;
    vm_pfn_t free_before;
    unsigned asid1;
    unsigned asid2;

    memset(test_ram, 0, sizeof(test_ram));
    memset(&allocator, 0, sizeof(allocator));
    vm_phys_map_init(&map);
    CHECK(vm_phys_map_add_ram(&map, 0, TEST_RAM_SIZE, "test ram") == 0);
    CHECK(vm_phys_map_finalize(&map) == 0);
    CHECK(vm_page_allocator_init(&allocator, &map, metadata,
        sizeof(metadata)) == 0);
    free_before = allocator.vpa_free_count;

    CHECK(pmap_system_init(&allocator) == 0);
    CHECK(test_tlb_flushes == 1);
    CHECK(test_asid == 0);
    CHECK(pmap_create(&pmap1) == 0);
    CHECK(pmap_create(&pmap2) == 0);
    CHECK(pmap_create(&pmap3) == 0);
    CHECK(test_page_alloc(&allocator, &page1) == 0);
    CHECK(test_page_alloc(&allocator, &page2) == 0);
    CHECK(test_page_alloc(&allocator, &page3) == 0);

    CHECK(pmap_enter(pmap1, TEST_VADDR, page1,
        VM_PROT_READ | VM_PROT_WRITE, PMAP_CACHE_CACHED) == 0);
    CHECK(pmap_enter(pmap2, TEST_VADDR, page2, VM_PROT_READ,
        PMAP_CACHE_UNCACHED) == 0);
    CHECK(pmap_enter(pmap1, TEST_VADDR2, page3,
        VM_PROT_READ | VM_PROT_EXECUTE, PMAP_CACHE_CACHED) == 0);
    CHECK(test_syncs == 1);
    CHECK(pmap_validate(pmap1) == 0);
    CHECK(pmap_validate(pmap2) == 0);

    CHECK(pmap_extract(pmap1, TEST_VADDR + 37, &paddr) == 0);
    CHECK(paddr == page1->vmp_paddr + 37);
    CHECK(pmap_extract(pmap1, TEST_VADDR + VM_PAGE_SIZE, &paddr) ==
        ENOENT);

    CHECK(pmap_activate(pmap1) == 0);
    asid1 = test_asid;
    CHECK(asid1 != 0);
    CHECK(pmap_fault(pmap1, TEST_VADDR, VM_PROT_READ, 1) == 0);
    CHECK(test_tlb_updates == 1);
    CHECK((test_entryhi & 0xffu) == asid1);
    CHECK(test_entrylo0 != 0 && test_entrylo1 == 0);
    CHECK(pmap_is_referenced(pmap1, TEST_VADDR));
    CHECK(page1->vmp_reference_count == 1);

    CHECK(pmap_fault_active(TEST_VADDR, VM_PROT_WRITE, 1) == 0);
    CHECK(pmap_is_modified(pmap1, TEST_VADDR));
    CHECK(page1->vmp_dirty_count == 1);
    CHECK((test_entrylo0 & 0x4u) != 0);
    CHECK(pmap_clear_reference(pmap1, TEST_VADDR) == 0);
    CHECK(!pmap_is_referenced(pmap1, TEST_VADDR));
    CHECK(pmap_clear_modify(pmap1, TEST_VADDR) == 0);
    CHECK(!pmap_is_modified(pmap1, TEST_VADDR));

    CHECK(pmap_protect(pmap1, TEST_VADDR,
        TEST_VADDR + VM_PAGE_SIZE, VM_PROT_READ) == 0);
    CHECK(pmap_fault(pmap1, TEST_VADDR, VM_PROT_WRITE, 1) == EACCES);
    CHECK(pmap_fault(pmap1, TEST_VADDR, VM_PROT_READ, 1) == 0);

    CHECK(pmap_activate(pmap2) == 0);
    asid2 = test_asid;
    CHECK(asid2 != 0 && asid2 != asid1);
    CHECK(pmap_fault(pmap2, TEST_VADDR, VM_PROT_READ, 1) == 0);
    CHECK((test_entryhi & 0xffu) == asid2);
    CHECK((test_entrylo0 & (7u << 3)) == (2u << 3));

    pmap_debug_force_asid_rollover();
    CHECK(pmap_activate(pmap3) == 0);
    CHECK(test_tlb_flushes == 2);
    CHECK(test_asid == 1);

    CHECK(pmap_remove(pmap2, TEST_VADDR,
        TEST_VADDR + VM_PAGE_SIZE) == 0);
    CHECK(pmap_extract(pmap2, TEST_VADDR, &paddr) == ENOENT);
    CHECK(test_tlb_invalidations != 0);
    CHECK(pmap_page_sync(page1, PMAP_SYNC_DATA) == 0);
    CHECK(pmap_page_direct_map(page1, PMAP_CACHE_CACHED) ==
        &test_ram[page1->vmp_paddr]);

    CHECK(pmap_get_stats(&stats) == 0);
    CHECK(stats.pms_mappings == 2);
    CHECK(stats.pms_resident_pages == 2);
    CHECK(stats.pms_tlb_refills == 4);
    CHECK(stats.pms_tlb_modified == 1);
    CHECK(stats.pms_protection_faults == 1);
    CHECK(stats.pms_full_flushes == 2);
    CHECK(stats.pms_asid_rollovers == 1);

    CHECK(pmap_destroy(pmap1) == 0);
    CHECK(pmap_destroy(pmap2) == 0);
    CHECK(pmap_destroy(pmap3) == 0);
    CHECK(vm_page_free(&allocator, page1, 1) == 0);
    CHECK(vm_page_free(&allocator, page2, 1) == 0);
    CHECK(vm_page_free(&allocator, page3, 1) == 0);
    CHECK(allocator.vpa_free_count == free_before);
    CHECK(pmap_fault_active(TEST_VADDR, VM_PROT_READ, 1) == ENOENT);
    return 0;
}

static int
test_vmspace(void)
{
    struct vm_page_allocator allocator;
    struct vm_phys_map map;
    struct vm_page metadata[TEST_RAM_SIZE / VM_PAGE_SIZE];
    struct vmspace *source;
    struct vmspace *child;
    struct vm_object_stats object_stats;
    unsigned char input[32];
    unsigned char output[32];
    vm_paddr_t source_paddr;
    vm_paddr_t child_paddr;
    vm_pfn_t free_before;
    unsigned i;

    memset(test_ram, 0, sizeof(test_ram));
    memset(&allocator, 0, sizeof(allocator));
    vm_phys_map_init(&map);
    CHECK(vm_phys_map_add_ram(&map, 0, TEST_RAM_SIZE, "test ram") == 0);
    CHECK(vm_phys_map_finalize(&map) == 0);
    CHECK(vm_page_allocator_init(&allocator, &map, metadata,
        sizeof(metadata)) == 0);
    free_before = allocator.vpa_free_count;
    CHECK(pmap_system_init(&allocator) == 0);
    CHECK(vmspace_system_init(&allocator) == 0);
    CHECK(vmspace_create(&source) == 0);
    CHECK(vmspace_map_anon(source, TEST_VADDR, 2 * VM_PAGE_SIZE,
        VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE, 0) == 0);
    CHECK(pmap_extract(source->vms_pmap, TEST_VADDR, &source_paddr) ==
        ENOENT);
    CHECK(vmspace_map_anon(source, TEST_SHARED, VM_PAGE_SIZE,
        VM_PROT_READ | VM_PROT_WRITE, VM_MAP_SHARED) == 0);
    for (i = 0; i < sizeof(input); ++i)
        input[i] = (unsigned char)(0x80u + i);
    CHECK(vmspace_write(source, TEST_VADDR + VM_PAGE_SIZE - 16,
        input, sizeof(input)) == 0);
    memset(output, 0, sizeof(output));
    CHECK(vmspace_read(source, TEST_VADDR + VM_PAGE_SIZE - 16,
        output, sizeof(output)) == 0);
    CHECK(memcmp(input, output, sizeof(input)) == 0);
    CHECK(vmspace_validate(source) == 0);
    output[0] = 0x6du;
    CHECK(vmspace_write(source, TEST_SHARED, output, 1) == 0);

    CHECK(vmspace_clone(source, &child) == 0);
    CHECK(pmap_extract(source->vms_pmap, TEST_VADDR, &source_paddr) == 0);
    CHECK(pmap_extract(child->vms_pmap, TEST_VADDR, &child_paddr) == 0);
    CHECK(source_paddr == child_paddr);
    CHECK(vmspace_read(child, TEST_VADDR + VM_PAGE_SIZE - 16,
        output, sizeof(output)) == 0);
    CHECK(memcmp(input, output, sizeof(input)) == 0);
    output[0] ^= 0xffu;
    CHECK(vmspace_write(child, TEST_VADDR + VM_PAGE_SIZE - 16,
        output, 1) == 0);
    CHECK(pmap_extract(child->vms_pmap, TEST_VADDR, &child_paddr) == 0);
    CHECK(source_paddr != child_paddr);
    CHECK(vmspace_read(source, TEST_VADDR + VM_PAGE_SIZE - 16,
        output, 1) == 0);
    CHECK(output[0] == input[0]);
    output[0] = 0x3cu;
    CHECK(vmspace_write(child, TEST_SHARED, output, 1) == 0);
    output[0] = 0;
    CHECK(vmspace_read(source, TEST_SHARED, output, 1) == 0);
    CHECK(output[0] == 0x3cu);

    CHECK(vmspace_protect(child, TEST_VADDR + VM_PAGE_SIZE,
        VM_PAGE_SIZE, VM_PROT_READ) == 0);
    CHECK(vmspace_write(child, TEST_VADDR + VM_PAGE_SIZE,
        input, 1) == EFAULT);
    CHECK(vmspace_destroy(child) == 0);
    CHECK(vmspace_destroy(source) == 0);
    CHECK(allocator.vpa_free_count == free_before);
    CHECK(vm_object_get_stats(&object_stats) == 0);
    CHECK(object_stats.vos_objects == 0);
    CHECK(object_stats.vos_anon_pages == 0);
    CHECK(object_stats.vos_resident_pages == 0);
    return 0;
}

static int
test_pager_reset(struct vm_page_allocator *allocator,
    struct vm_phys_map *map, struct vm_page *metadata,
    unsigned swap_pages)
{
    memset(test_ram, 0, sizeof(test_ram));
    memset(allocator, 0, sizeof(*allocator));
    vm_phys_map_init(map);
    if (vm_phys_map_add_ram(map, 0, TEST_RAM_SIZE, "test ram") != 0 ||
        vm_phys_map_finalize(map) != 0 ||
        vm_page_allocator_init(allocator, map, metadata,
        TEST_RAM_SIZE / VM_PAGE_SIZE * sizeof(*metadata)) != 0 ||
        pmap_system_init(allocator) != 0 ||
        vmspace_system_init(allocator) != 0 ||
        vm_pager_debug_swap_configure(swap_pages) != 0)
        return 1;
    return 0;
}

static int
test_pager(void)
{
    struct vm_page_allocator allocator;
    struct vm_phys_map map;
    struct vm_page metadata[TEST_RAM_SIZE / VM_PAGE_SIZE];
    struct vm_object_stats stats;
    struct vmspace *space;
    vm_paddr_t paddr;
    vm_vaddr_t evicted;
    unsigned char value;
    unsigned index;
    int error;

    CHECK(test_pager_reset(&allocator, &map, metadata, 16) == 0);
    CHECK(vmspace_create(&space) == 0);
    CHECK(vmspace_map_anon(space, TEST_PRESSURE,
        TEST_PRESSURE_PAGES * VM_PAGE_SIZE,
        VM_PROT_READ | VM_PROT_WRITE, 0) == 0);
    for (index = 0; index < TEST_PRESSURE_PAGES; ++index) {
        value = (unsigned char)(index * 29u + 7u);
        CHECK(vmspace_write(space,
            TEST_PRESSURE + index * VM_PAGE_SIZE, &value, 1) == 0);
    }
    CHECK(vm_object_get_stats(&stats) == 0);
    CHECK(stats.vos_pageouts != 0);
    evicted = 0;
    for (index = 0; index < TEST_PRESSURE_PAGES; ++index) {
        if (pmap_extract(space->vms_pmap,
            TEST_PRESSURE + index * VM_PAGE_SIZE, &paddr) == ENOENT) {
            evicted = TEST_PRESSURE + index * VM_PAGE_SIZE;
            break;
        }
    }
    CHECK(evicted != 0);
    vm_pager_debug_fail_io(1, 0);
    CHECK(vmspace_read(space, evicted, &value, 1) == EIO);
    vm_pager_debug_fail_io(0, 0);
    for (index = TEST_PRESSURE_PAGES; index-- != 0;) {
        value = 0;
        CHECK(vmspace_read(space,
            TEST_PRESSURE + index * VM_PAGE_SIZE, &value, 1) == 0);
        CHECK(value == (unsigned char)(index * 29u + 7u));
    }
    CHECK(vm_object_get_stats(&stats) == 0);
    CHECK(stats.vos_pageins != 0);
    CHECK(stats.vos_swap_failures != 0);
    CHECK(vmspace_destroy(space) == 0);
    CHECK(allocator.vpa_free_count == TEST_RAM_SIZE / VM_PAGE_SIZE);
    CHECK(vm_object_get_stats(&stats) == 0);
    CHECK(stats.vos_objects == 0 && stats.vos_anon_pages == 0 &&
        stats.vos_resident_pages == 0 && stats.vos_swapped_pages == 0);

    CHECK(test_pager_reset(&allocator, &map, metadata, 16) == 0);
    CHECK(vmspace_create(&space) == 0);
    CHECK(vmspace_map_anon(space, TEST_PRESSURE,
        TEST_PRESSURE_PAGES * VM_PAGE_SIZE,
        VM_PROT_READ | VM_PROT_WRITE, 0) == 0);
    vm_pager_debug_fail_io(0, 1);
    error = 0;
    for (index = 0; index < TEST_PRESSURE_PAGES; ++index) {
        value = (unsigned char)index;
        error = vmspace_write(space,
            TEST_PRESSURE + index * VM_PAGE_SIZE, &value, 1);
        if (error != 0)
            break;
    }
    CHECK(error == ENOMEM);
    CHECK(vm_object_get_stats(&stats) == 0);
    CHECK(stats.vos_swap_failures != 0 && stats.vos_pageouts == 0);
    vm_pager_debug_fail_io(0, 0);
    CHECK(vmspace_destroy(space) == 0);
    CHECK(allocator.vpa_free_count == TEST_RAM_SIZE / VM_PAGE_SIZE);

    CHECK(test_pager_reset(&allocator, &map, metadata, 2) == 0);
    CHECK(vmspace_create(&space) == 0);
    CHECK(vmspace_map_anon(space, TEST_PRESSURE,
        TEST_PRESSURE_PAGES * VM_PAGE_SIZE,
        VM_PROT_READ | VM_PROT_WRITE, 0) == 0);
    error = 0;
    for (index = 0; index < TEST_PRESSURE_PAGES; ++index) {
        value = (unsigned char)index;
        error = vmspace_write(space,
            TEST_PRESSURE + index * VM_PAGE_SIZE, &value, 1);
        if (error != 0)
            break;
    }
    CHECK(error == ENOMEM);
    CHECK(vm_object_get_stats(&stats) == 0);
    CHECK(stats.vos_swapped_pages == 2);
    CHECK(vmspace_destroy(space) == 0);
    CHECK(allocator.vpa_free_count == TEST_RAM_SIZE / VM_PAGE_SIZE);
    CHECK(vm_object_get_stats(&stats) == 0);
    CHECK(stats.vos_objects == 0 && stats.vos_anon_pages == 0 &&
        stats.vos_resident_pages == 0 && stats.vos_swapped_pages == 0);
    return 0;
}

int
main(void)
{
    if (test_pmap() != 0)
        return 1;
    if (test_vmspace() != 0)
        return 1;
    if (test_pager() != 0)
        return 1;
    puts("MIPS pmap/vmspace tests: ok");
    return 0;
}
