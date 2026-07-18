/* Host-side tests for the shared 32-bit MIPS pmap. */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <vm/pmap.h>
#include <vm/vm_object.h>
#include <vm/vm_shm.h>
#include <vm/vm_sysv_shm.h>
#include <vm/vmspace.h>

#define TEST_RAM_SIZE   (64u * VM_PAGE_SIZE)
#define TEST_VADDR      0x10000000u
#define TEST_VADDR2     0x10002000u
#define TEST_SHARED     0x10004000u
#define TEST_FILE       0x10008000u
#define TEST_SHARED_FILE 0x1000c000u
#define TEST_DEVICE     0x10010000u
#define TEST_DEVICE_PADDR (TEST_RAM_SIZE - VM_PAGE_SIZE)
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

struct test_object_pager {
    unsigned references;
    unsigned releases;
    unsigned pageins;
};

static void
test_object_pager_reference(void *cookie)
{
    struct test_object_pager *pager = cookie;

    ++pager->references;
}

static void
test_object_pager_release(void *cookie)
{
    struct test_object_pager *pager = cookie;

    --pager->references;
    ++pager->releases;
}

static int
test_object_pager_pagein(void *cookie, vm_ooffset_t offset, void *buffer,
    vm_size_t size)
{
    struct test_object_pager *pager = cookie;

    ++pager->pageins;
    if (offset == 0x4000u)
        return ENXIO;
    if (offset != 0x2000u && offset != 0x3000u)
        return EINVAL;
    memset(buffer, offset == 0x2000u ? 0xa1 : 0xb2, size);
    return 0;
}

static const struct vm_object_pager_ops test_object_pager_ops = {
    test_object_pager_reference,
    test_object_pager_release,
    test_object_pager_pagein,
    0,
    0
};

struct test_shared_pager {
    unsigned char data[3 * VM_PAGE_SIZE];
    unsigned references;
    unsigned releases;
    unsigned pageins;
    unsigned pageouts;
    unsigned syncs;
    int fail_pageout;
};

static void
test_shared_pager_reference(void *cookie)
{
    ++((struct test_shared_pager *)cookie)->references;
}

static void
test_shared_pager_release(void *cookie)
{
    struct test_shared_pager *pager = cookie;

    --pager->references;
    ++pager->releases;
}

static int
test_shared_pager_pagein(void *cookie, vm_ooffset_t offset, void *buffer,
    vm_size_t size)
{
    struct test_shared_pager *pager = cookie;

    if (offset > sizeof(pager->data) ||
        size > sizeof(pager->data) - offset)
        return ENXIO;
    memcpy(buffer, &pager->data[(unsigned)offset], size);
    ++pager->pageins;
    return 0;
}

static int
test_shared_pager_pageout(void *cookie, vm_ooffset_t offset,
    const void *buffer, vm_size_t size, unsigned flags)
{
    struct test_shared_pager *pager = cookie;

    (void)flags;
    if (offset > sizeof(pager->data) ||
        size > sizeof(pager->data) - offset)
        return ENXIO;
    if (pager->fail_pageout)
        return EIO;
    memcpy(&pager->data[(unsigned)offset], buffer, size);
    ++pager->pageouts;
    return 0;
}

static int
test_shared_pager_sync(void *cookie, unsigned flags)
{
    (void)flags;
    ++((struct test_shared_pager *)cookie)->syncs;
    return 0;
}

static const struct vm_object_pager_ops test_shared_pager_ops = {
    test_shared_pager_reference,
    test_shared_pager_release,
    test_shared_pager_pagein,
    test_shared_pager_pageout,
    test_shared_pager_sync
};

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
    struct vm_page *device_page;
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
    CHECK(vm_phys_map_reserve(&map, TEST_DEVICE_PADDR, VM_PAGE_SIZE,
        "test device") == 0);
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
    device_page = vm_page_lookup(&allocator, TEST_DEVICE_PADDR);
    CHECK(device_page != 0 &&
        device_page->vmp_state == VM_PAGE_RESERVED);

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

    CHECK(pmap_enter_device(pmap3, TEST_DEVICE, page1->vmp_paddr,
        VM_PROT_READ, PMAP_CACHE_CACHED) == EBUSY);
    CHECK(pmap_enter_device(pmap3, TEST_DEVICE,
        device_page->vmp_paddr, VM_PROT_READ | VM_PROT_WRITE,
        PMAP_CACHE_UNCACHED) == 0);
    CHECK(pmap_validate(pmap3) == 0);
    CHECK(pmap_fault(pmap3, TEST_DEVICE, VM_PROT_READ, 1) == 0);
    CHECK(pmap_fault(pmap3, TEST_DEVICE, VM_PROT_WRITE, 1) == 0);
    CHECK(device_page->vmp_hold_count == 0 &&
        device_page->vmp_reference_count == 0 &&
        device_page->vmp_dirty_count == 0);
    CHECK(pmap_extract(pmap3, TEST_DEVICE + 19, &paddr) == 0 &&
        paddr == device_page->vmp_paddr + 19);
    CHECK(pmap_protect(pmap3, TEST_DEVICE,
        TEST_DEVICE + VM_PAGE_SIZE,
        VM_PROT_READ | VM_PROT_EXECUTE) == EACCES);
    CHECK(pmap_validate(pmap3) == 0);
    CHECK(pmap_clear_reference(pmap3, TEST_DEVICE) == 0);
    CHECK(pmap_clear_modify(pmap3, TEST_DEVICE) == 0);
    CHECK(pmap_remove(pmap3, TEST_DEVICE,
        TEST_DEVICE + VM_PAGE_SIZE) == 0);

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
    CHECK(stats.pms_tlb_refills == 6);
    CHECK(stats.pms_tlb_modified == 2);
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
    struct vm_object *file_object;
    struct vm_object *shared_file_object;
    struct test_object_pager file_pager;
    struct test_shared_pager shared_file_pager;
    struct vm_object_stats object_stats;
    const struct vm_map_entry *map_entry;
    struct vm_page *wired_page;
    struct vm_page *wired_after;
    struct vm_page *device_page;
    unsigned char input[32];
    unsigned char output[32];
    int resident;
    vm_paddr_t source_paddr;
    vm_paddr_t child_paddr;
    vm_vaddr_t any_address;
    vm_pfn_t free_before;
    unsigned i;

    memset(test_ram, 0, sizeof(test_ram));
    memset(&allocator, 0, sizeof(allocator));
    vm_phys_map_init(&map);
    CHECK(vm_phys_map_add_ram(&map, 0, TEST_RAM_SIZE, "test ram") == 0);
    CHECK(vm_phys_map_reserve(&map, TEST_DEVICE_PADDR, VM_PAGE_SIZE,
        "test device") == 0);
    CHECK(vm_phys_map_finalize(&map) == 0);
    CHECK(vm_page_allocator_init(&allocator, &map, metadata,
        sizeof(metadata)) == 0);
    free_before = allocator.vpa_free_count;
    CHECK(pmap_system_init(&allocator) == 0);
    CHECK(vmspace_system_init(&allocator) == 0);
    CHECK(vmspace_create(&source) == 0);
    device_page = vm_page_lookup(&allocator, TEST_DEVICE_PADDR);
    CHECK(device_page != 0 &&
        device_page->vmp_state == VM_PAGE_RESERVED);
    memset(&file_pager, 0, sizeof(file_pager));
    CHECK(vm_object_create_paged(3 * VM_PAGE_SIZE,
        &test_object_pager_ops, &file_pager, 0x2000u,
        &file_object) == 0);
    CHECK(file_pager.references == 1);
    CHECK(vmspace_map_object(source, TEST_FILE, 3 * VM_PAGE_SIZE,
        VM_PROT_READ | VM_PROT_WRITE, VM_PROT_ALL, 0,
        file_object, 0) == 0);
    output[0] = 0;
    CHECK(vmspace_read_context(source, TEST_FILE + 7, output, 1,
        VM_FAULT_INTERRUPT) == EWOULDBLOCK);
    CHECK(file_pager.pageins == 0);
    CHECK(vmspace_read(source, TEST_FILE + 7, output, 1) == 0);
    CHECK(output[0] == 0xa1 && file_pager.pageins == 1);
    output[0] = 0;
    CHECK(vmspace_read_context(source, TEST_FILE + 7, output, 1,
        VM_FAULT_INTERRUPT) == 0 && output[0] == 0xa1);
    CHECK(vmspace_read_context(source, TEST_FILE + 7, output, 1,
        0) == EINVAL);
    CHECK(vmspace_read(source, TEST_FILE + 2 * VM_PAGE_SIZE,
        output, 1) == ENXIO);
    CHECK(vmspace_mincore(source, TEST_FILE + 2 * VM_PAGE_SIZE,
        &resident) == 0 && resident == 0);
    memset(&shared_file_pager, 0, sizeof(shared_file_pager));
    memset(shared_file_pager.data, 0x63,
        sizeof(shared_file_pager.data));
    CHECK(vm_object_create_paged(3 * VM_PAGE_SIZE,
        &test_shared_pager_ops, &shared_file_pager, 0,
        &shared_file_object) == 0);
    CHECK(vmspace_map_object(source, TEST_SHARED_FILE, VM_PAGE_SIZE,
        VM_PROT_READ | VM_PROT_WRITE, VM_PROT_ALL, VM_MAP_SHARED,
        shared_file_object, VM_PAGE_SIZE) == 0);
    CHECK(vmspace_map_device(source, TEST_DEVICE, VM_PAGE_SIZE,
        VM_PROT_READ | VM_PROT_WRITE, VM_PROT_READ | VM_PROT_WRITE,
        device_page->vmp_paddr, PMAP_CACHE_UNCACHED) == 0);
    CHECK(vmspace_mincore(source, TEST_DEVICE, &resident) == 0 &&
        resident == 1);
    output[0] = 0x44;
    CHECK(vmspace_write(source, TEST_DEVICE + 7, output, 1) == 0);
    CHECK(test_ram[device_page->vmp_paddr + 7] == 0x44);
    CHECK(device_page->vmp_hold_count == 0 &&
        device_page->vmp_reference_count == 0 &&
        device_page->vmp_dirty_count == 0);
    CHECK(vmspace_wire(source, TEST_DEVICE, VM_PAGE_SIZE, 1) == 0);
    CHECK(device_page->vmp_wire_count == 0);
    CHECK(vmspace_wire(source, TEST_DEVICE, VM_PAGE_SIZE, 0) == 0);
    output[0] = 0;
    CHECK(vmspace_read(source, TEST_SHARED_FILE, output, 1) == 0 &&
        output[0] == 0x63 && shared_file_pager.pageins == 1);
    CHECK(vmspace_map_anon(source, TEST_VADDR, 2 * VM_PAGE_SIZE,
        VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE, 0) == 0);
    CHECK(pmap_extract(source->vms_pmap, TEST_VADDR, &source_paddr) ==
        ENOENT);
    CHECK(vmspace_read_context(source, TEST_VADDR, output, 1,
        VM_FAULT_COPY) == EWOULDBLOCK);
    CHECK(pmap_extract(source->vms_pmap, TEST_VADDR, &source_paddr) ==
        ENOENT);
    CHECK(vmspace_map_anon(source, TEST_SHARED, VM_PAGE_SIZE,
        VM_PROT_READ | VM_PROT_WRITE, VM_MAP_SHARED) == 0);
    CHECK(vmspace_map_anon_any(source, TEST_VADDR, VM_PAGE_SIZE,
        VM_PROT_READ | VM_PROT_WRITE, 0, &any_address) == 0);
    CHECK(any_address == TEST_VADDR2);
    output[0] = 0x71u;
    CHECK(vmspace_write(source, TEST_VADDR + VM_PAGE_SIZE,
        output, 1) == 0);
    CHECK(vmspace_map_anon_fixed(source, TEST_VADDR + VM_PAGE_SIZE,
        VM_PAGE_SIZE, VM_PROT_READ | VM_PROT_WRITE, 0) == 0);
    output[0] = 0xffu;
    CHECK(vmspace_read(source, TEST_VADDR + VM_PAGE_SIZE,
        output, 1) == 0);
    CHECK(output[0] == 0);
    CHECK(vmspace_map_anon_fixed(source, TEST_VADDR + 1,
        VM_PAGE_SIZE, VM_PROT_READ | VM_PROT_WRITE, 0) == EINVAL);
    CHECK(vmspace_wire(source, any_address, VM_PAGE_SIZE, 1) == 0);
    map_entry = vm_map_lookup(&source->vms_map, any_address);
    CHECK(map_entry != 0 && (map_entry->vme_flags & VM_MAP_WIRED) != 0);
    wired_page = vm_object_resident_page(map_entry->vme_object,
        map_entry->vme_offset);
    CHECK(wired_page != 0 && wired_page->vmp_wire_count == 1);
    CHECK(vmspace_unmap(source, any_address, VM_PAGE_SIZE) == 0);
    CHECK(wired_page->vmp_state == VM_PAGE_FREE &&
        wired_page->vmp_wire_count == 0);
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
    CHECK(vmspace_wire(source, TEST_VADDR, 2 * VM_PAGE_SIZE, 1) == 0);
    CHECK(vmspace_wire(source, TEST_VADDR, 2 * VM_PAGE_SIZE, 1) == 0);
    map_entry = vm_map_lookup(&source->vms_map, TEST_VADDR);
    wired_page = vm_object_resident_page(map_entry->vme_object,
        map_entry->vme_offset);
    CHECK(wired_page != 0 && wired_page->vmp_wire_count == 1);

    CHECK(vmspace_clone(source, &child) == 0);
    CHECK(file_pager.references == 2);
    CHECK(shared_file_pager.references == 1);
    CHECK((vm_map_lookup(&child->vms_map, TEST_VADDR)->vme_flags &
        VM_MAP_WIRED) == 0);
    CHECK(pmap_extract(child->vms_pmap, TEST_DEVICE, &child_paddr) ==
        ENOENT);
    output[0] = 0;
    CHECK(vmspace_read(child, TEST_DEVICE + 7, output, 1) == 0 &&
        output[0] == 0x44);
    output[0] = 0x55;
    CHECK(vmspace_write(child, TEST_DEVICE + 7, output, 1) == 0);
    output[0] = 0;
    CHECK(vmspace_read(source, TEST_DEVICE + 7, output, 1) == 0 &&
        output[0] == 0x55);
    CHECK(vmspace_protect(child, TEST_DEVICE, VM_PAGE_SIZE,
        VM_PROT_READ) == 0);
    CHECK(vmspace_write(child, TEST_DEVICE, input, 1) == EFAULT);
    CHECK(pmap_extract(source->vms_pmap, TEST_VADDR, &source_paddr) == 0);
    CHECK(pmap_extract(child->vms_pmap, TEST_VADDR, &child_paddr) == 0);
    CHECK(source_paddr == child_paddr);
    CHECK(vmspace_read(child, TEST_VADDR + VM_PAGE_SIZE - 16,
        output, sizeof(output)) == 0);
    CHECK(memcmp(input, output, sizeof(input)) == 0);
    map_entry = vm_map_lookup(&source->vms_map,
        TEST_VADDR + VM_PAGE_SIZE);
    wired_page = vm_object_resident_page(map_entry->vme_object,
        map_entry->vme_offset);
    output[0] = 0x29u;
    CHECK(vmspace_write(source, TEST_VADDR + VM_PAGE_SIZE + 64,
        output, 1) == 0);
    map_entry = vm_map_lookup(&source->vms_map,
        TEST_VADDR + VM_PAGE_SIZE);
    wired_after = vm_object_resident_page(map_entry->vme_object,
        map_entry->vme_offset);
    CHECK(wired_after != 0 && wired_after != wired_page);
    CHECK(wired_after->vmp_wire_count == 1 &&
        wired_page->vmp_wire_count == 0);
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
    output[0] = 0;
    CHECK(vmspace_read(child, TEST_FILE + VM_PAGE_SIZE,
        output, 1) == 0 && output[0] == 0xb2);
    output[0] = 0x5cu;
    CHECK(vmspace_write(child, TEST_FILE, output, 1) == 0);
    output[0] = 0;
    CHECK(vmspace_read(source, TEST_FILE, output, 1) == 0 &&
        output[0] == 0xa1);
    output[0] = 0x79;
    CHECK(vmspace_write(child, TEST_SHARED_FILE, output, 1) == 0);
    output[0] = 0;
    CHECK(vmspace_read(source, TEST_SHARED_FILE, output, 1) == 0 &&
        output[0] == 0x79);
    shared_file_pager.fail_pageout = 1;
    CHECK(vmspace_sync(child, TEST_SHARED_FILE, VM_PAGE_SIZE,
        VM_PAGER_IO_SYNC) == EIO);
    shared_file_pager.fail_pageout = 0;
    output[0] = 0;
    CHECK(vmspace_read(source, TEST_SHARED_FILE, output, 1) == 0 &&
        output[0] == 0x79);
    CHECK(vmspace_sync(child, TEST_SHARED_FILE, VM_PAGE_SIZE,
        VM_PAGER_IO_SYNC) == 0);
    CHECK(shared_file_pager.data[VM_PAGE_SIZE] == 0x79 &&
        shared_file_pager.pageouts == 1 &&
        shared_file_pager.syncs == 1);
    output[0] = 0x2a;
    CHECK(vm_object_update(shared_file_object, VM_PAGE_SIZE,
        output, 1) == 0);
    output[0] = 0;
    CHECK(vmspace_read(source, TEST_SHARED_FILE, output, 1) == 0 &&
        output[0] == 0x2a);

    CHECK(vmspace_protect(child, TEST_VADDR + VM_PAGE_SIZE,
        VM_PAGE_SIZE, VM_PROT_READ) == 0);
    CHECK(vmspace_write(child, TEST_VADDR + VM_PAGE_SIZE,
        input, 1) == EFAULT);
    CHECK(vmspace_destroy(child) == 0);
    CHECK(file_pager.references == 1 && file_pager.releases == 1);
    CHECK(shared_file_pager.references == 1 &&
        shared_file_pager.releases == 0);
    CHECK(vmspace_wire(source, TEST_VADDR, 2 * VM_PAGE_SIZE, 0) == 0);
    CHECK(wired_after->vmp_wire_count == 0);
    CHECK(vmspace_destroy(source) == 0);
    CHECK(file_pager.references == 0 && file_pager.releases == 2);
    CHECK(shared_file_pager.references == 0 &&
        shared_file_pager.releases == 1);
    CHECK(allocator.vpa_free_count == free_before);
    CHECK(vm_object_get_stats(&object_stats) == 0);
    CHECK(object_stats.vos_objects == 0);
    CHECK(object_stats.vos_anon_pages == 0);
    CHECK(object_stats.vos_resident_pages == 0);
    CHECK(object_stats.vos_faults != 0);
    CHECK(object_stats.vos_fault_wouldblocks >= 2);
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
    struct test_shared_pager shared_pager;
    struct vm_object *shared_object;
    struct vm_object_stats stats;
    struct vmspace *space;
    vm_paddr_t paddr;
    vm_vaddr_t evicted;
    unsigned written;
    unsigned char value;
    unsigned index;
    int error;

    CHECK(test_pager_reset(&allocator, &map, metadata, 0) == 0);
    CHECK(vmspace_create(&space) == 0);
    memset(&shared_pager, 0, sizeof(shared_pager));
    memset(shared_pager.data, 0x41, sizeof(shared_pager.data));
    CHECK(vm_object_create_paged(VM_PAGE_SIZE, &test_shared_pager_ops,
        &shared_pager, 0, &shared_object) == 0);
    CHECK(vmspace_map_object(space, TEST_SHARED_FILE, VM_PAGE_SIZE,
        VM_PROT_READ | VM_PROT_WRITE, VM_PROT_ALL, VM_MAP_SHARED,
        shared_object, 0) == 0);
    value = 0;
    CHECK(vmspace_read(space, TEST_SHARED_FILE, &value, 1) == 0 &&
        value == 0x41);
    value = 0x5e;
    CHECK(vmspace_write(space, TEST_SHARED_FILE, &value, 1) == 0);
    CHECK(vmspace_map_anon(space, TEST_PRESSURE,
        TEST_PRESSURE_PAGES * VM_PAGE_SIZE,
        VM_PROT_READ | VM_PROT_WRITE, 0) == 0);
    error = 0;
    for (index = 0; index < TEST_PRESSURE_PAGES; ++index) {
        value = (unsigned char)(index * 17u + 3u);
        error = vmspace_write(space,
            TEST_PRESSURE + index * VM_PAGE_SIZE, &value, 1);
        if (error != 0)
            break;
    }
    CHECK(error == ENOMEM && index != 0);
    CHECK(shared_pager.pageouts != 0 && shared_pager.data[0] == 0x5e);
    CHECK(pmap_extract(space->vms_pmap, TEST_SHARED_FILE, &paddr) ==
        ENOENT);
    CHECK(vmspace_unmap(space, TEST_PRESSURE, VM_PAGE_SIZE) == 0);
    value = 0;
    CHECK(vmspace_read(space, TEST_SHARED_FILE, &value, 1) == 0 &&
        value == 0x5e && shared_pager.pageins >= 2);
    CHECK(vmspace_destroy(space) == 0);
    CHECK(shared_pager.references == 0 && shared_pager.releases == 1);
    CHECK(allocator.vpa_free_count == TEST_RAM_SIZE / VM_PAGE_SIZE);

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
    CHECK(stats.vos_pageouts != 0 && stats.vos_reclaim_attempts != 0);
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
    written = index;
    CHECK(vm_object_get_stats(&stats) == 0);
    CHECK(stats.vos_swap_failures != 0 && stats.vos_pageouts == 0 &&
        stats.vos_reclaim_failures != 0);
    vm_pager_debug_fail_io(0, 0);
    for (index = 0; index < written; ++index) {
        value = 0xff;
        CHECK(vmspace_read(space,
            TEST_PRESSURE + index * VM_PAGE_SIZE, &value, 1) == 0);
        CHECK(value == (unsigned char)index);
    }
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

static int
test_shm(void)
{
    struct vm_page_allocator allocator;
    struct vm_phys_map map;
    struct vm_page metadata[TEST_RAM_SIZE / VM_PAGE_SIZE];
    struct vm_object_stats object_stats;
    struct vm_shm_stats shm_stats;
    struct vm_sysv_shm_stats sysv_stats;
    struct vm_sysv_shm_info sysv_info;
    struct vm_shm_info info;
    struct vm_object *object;
    struct vm_sysv_shm *sysv_replacement;
    struct vm_sysv_shm *sysv_segment;
    struct vm_sysv_shm *sysv_segments[VM_SYSV_SHM_MAX_SEGMENTS];
    struct vmspace *child;
    struct vmspace *inherited;
    struct vm_shm *replacement;
    struct vm_shm *shm;
    struct vm_shm *found;
    struct vmspace *space;
    unsigned index;
    unsigned char value;

    memset(test_ram, 0, sizeof(test_ram));
    memset(&allocator, 0, sizeof(allocator));
    vm_phys_map_init(&map);
    CHECK(vm_phys_map_add_ram(&map, 0, TEST_RAM_SIZE, "test ram") == 0);
    CHECK(vm_phys_map_finalize(&map) == 0);
    CHECK(vm_page_allocator_init(&allocator, &map, metadata,
        sizeof(metadata)) == 0);
    CHECK(pmap_system_init(&allocator) == 0);
    CHECK(vmspace_system_init(&allocator) == 0);
    CHECK(vmspace_create(&space) == 0);

    CHECK(vm_shm_create("/host-test", 12, 34, 0640, &shm) == 0);
    CHECK(vm_shm_create("/host-test", 12, 34, 0640, &found) == EEXIST);
    CHECK(vm_shm_lookup("/host-test", &found) == 0 && found == shm);
    CHECK(vm_shm_get_info(shm, &info) == 0 && info.vsi_size == 0 &&
        info.vsi_owner == 12 && info.vsi_group == 34 &&
        info.vsi_mode == 0640 && info.vsi_open_count == 1 &&
        info.vsi_linked);
    CHECK(vm_shm_truncate(shm, 2 * VM_PAGE_SIZE + 17) == 0);
    CHECK(vm_shm_object_reference(shm, &object) == 0);
    CHECK(vmspace_map_object(space, TEST_VADDR, 3 * VM_PAGE_SIZE,
        VM_PROT_READ | VM_PROT_WRITE, VM_PROT_READ | VM_PROT_WRITE,
        VM_MAP_SHARED, object, 0) == 0);

    value = 0x5a;
    CHECK(vmspace_write(space, TEST_VADDR + 2 * VM_PAGE_SIZE + 100,
        &value, 1) == 0);
    CHECK(vm_shm_truncate(shm, 2 * VM_PAGE_SIZE + 200) == 0);
    value = 0xff;
    CHECK(vmspace_read(space, TEST_VADDR + 2 * VM_PAGE_SIZE + 100,
        &value, 1) == 0 && value == 0);
    value = 0x6b;
    CHECK(vmspace_write(space, TEST_VADDR + 2 * VM_PAGE_SIZE + 5,
        &value, 1) == 0);
    CHECK(vm_shm_truncate(shm, VM_PAGE_SIZE + 5) == 0);
    CHECK(vm_shm_truncate(shm, 3 * VM_PAGE_SIZE) == 0);
    value = 0xff;
    CHECK(vmspace_read(space, TEST_VADDR + 2 * VM_PAGE_SIZE + 5,
        &value, 1) == 0 && value == 0);

    CHECK(vm_shm_retain(shm) == 0);
    CHECK(vm_shm_unlink(shm) == 0);
    CHECK(vm_shm_lookup("/host-test", &found) == ENOENT);
    CHECK(vm_shm_close(shm) == 0);
    CHECK(vm_shm_close(shm) == 0);

    CHECK(vm_shm_create("/host-test", 56, 78, 0600,
        &replacement) == 0);
    CHECK(vm_shm_truncate(replacement, VM_PAGE_SIZE) == 0);
    CHECK(vm_shm_object_reference(replacement, &object) == 0);
    CHECK(vmspace_map_object(space, TEST_SHARED, VM_PAGE_SIZE,
        VM_PROT_READ | VM_PROT_WRITE, VM_PROT_READ | VM_PROT_WRITE,
        VM_MAP_SHARED, object, 0) == 0);
    value = 0xff;
    CHECK(vmspace_read(space, TEST_SHARED, &value, 1) == 0 && value == 0);
    value = 0x7c;
    CHECK(vmspace_write(space, TEST_VADDR, &value, 1) == 0);
    value = 0;
    CHECK(vmspace_read(space, TEST_VADDR, &value, 1) == 0 && value == 0x7c);
    CHECK(vm_shm_unlink(replacement) == 0);
    CHECK(vm_shm_close(replacement) == 0);

    CHECK(vm_shm_get_stats(&shm_stats) == 0 &&
        shm_stats.vss_objects == 0 && shm_stats.vss_open_files == 0);

    CHECK(vm_sysv_shm_create(0x1234, 2 * VM_PAGE_SIZE + 17,
        12, 34, 0640, 77, 100, &sysv_segment) == 0);
    CHECK(vm_sysv_shm_get_info(sysv_segment, &sysv_info) == 0 &&
        sysv_info.vssi_key == 0x1234 &&
        sysv_info.vssi_size == 2 * VM_PAGE_SIZE + 17 &&
        sysv_info.vssi_owner == 12 && sysv_info.vssi_group == 34 &&
        sysv_info.vssi_mode == 0640 && sysv_info.vssi_creator_pid == 77 &&
        sysv_info.vssi_attach_count == 0);
    CHECK(vm_sysv_shm_lookup_id(sysv_info.vssi_id,
        &sysv_replacement) == 0 && sysv_replacement == sysv_segment);
    CHECK(vm_sysv_shm_object_reference(sysv_segment, &object) == 0);
    CHECK(vmspace_map_object(space, 0x30000000u, 3 * VM_PAGE_SIZE,
        VM_PROT_READ | VM_PROT_WRITE, VM_PROT_READ | VM_PROT_WRITE,
        VM_MAP_SHARED | VM_MAP_SYSV_SHM, object, 0) == 0);
    CHECK(vmspace_sysv_attach(space, sysv_segment, 0x30000000u,
        3 * VM_PAGE_SIZE, 77, 101) == 0);
    value = 0x4d;
    CHECK(vmspace_write(space, 0x30000000u + VM_PAGE_SIZE,
        &value, 1) == 0);
    CHECK(vmspace_clone(space, &child) == 0);
    CHECK(vm_sysv_shm_get_info(sysv_segment, &sysv_info) == 0 &&
        sysv_info.vssi_attach_count == 2);
    value = 0;
    CHECK(vmspace_read(child, 0x30000000u + VM_PAGE_SIZE,
        &value, 1) == 0 && value == 0x4d);
    CHECK(vm_sysv_shm_mark_remove(sysv_segment, 77, 102) == 0);
    CHECK(vm_sysv_shm_lookup_key(0x1234, &sysv_replacement) == ENOENT);
    CHECK(vm_sysv_shm_lookup_id(sysv_info.vssi_id,
        &sysv_replacement) == EINVAL);
    CHECK(vmspace_clone(space, &inherited) == 0);
    CHECK(vm_sysv_shm_get_info(sysv_segment, &sysv_info) == 0 &&
        sysv_info.vssi_attach_count == 3);
    CHECK(vmspace_destroy(inherited) == 0);
    CHECK(vmspace_destroy(child) == 0);
    CHECK(vm_sysv_shm_get_info(sysv_segment, &sysv_info) == 0 &&
        sysv_info.vssi_removed && sysv_info.vssi_attach_count == 1);
    CHECK(vmspace_sysv_detach(space, 0x30000000u, 77, 103) == 0);
    CHECK(vm_sysv_shm_get_stats(&sysv_stats) == 0 &&
        sysv_stats.vsss_segments == 0 && sysv_stats.vsss_attachments == 0);
    CHECK(vm_sysv_shm_create(0x1234, VM_PAGE_SIZE, 56, 78, 0600,
        88, 104, &sysv_replacement) == 0);
    CHECK(vm_sysv_shm_get_info(sysv_replacement, &sysv_info) == 0 &&
        vm_sysv_shm_mark_remove(sysv_replacement, 88, 105) == 0);
    CHECK(vm_sysv_shm_lookup_id(sysv_info.vssi_id,
        &sysv_segment) == EINVAL);
    CHECK(vm_sysv_shm_get_stats(&sysv_stats) == 0 &&
        sysv_stats.vsss_segments == 0);

    for (index = 0; index < VM_SYSV_SHM_MAX_SEGMENTS; ++index) {
        CHECK(vm_sysv_shm_create(0x2000 + (int)index, 1,
            56, 78, 0600, 88, 106, &sysv_segments[index]) == 0);
    }
    CHECK(vm_sysv_shm_create(0x3000, 1, 56, 78, 0600, 88, 106,
        &sysv_segment) == ENOSPC);
    CHECK(vm_sysv_shm_get_stats(&sysv_stats) == 0 &&
        sysv_stats.vsss_segments == VM_SYSV_SHM_MAX_SEGMENTS &&
        sysv_stats.vsss_pages == VM_SYSV_SHM_MAX_SEGMENTS);
    for (index = 0; index < VM_SYSV_SHM_MAX_SEGMENTS; ++index)
        CHECK(vm_sysv_shm_mark_remove(sysv_segments[index], 88, 107) == 0);
    CHECK(vm_sysv_shm_get_stats(&sysv_stats) == 0 &&
        sysv_stats.vsss_segments == 0);

    CHECK(vmspace_destroy(space) == 0);
    CHECK(vm_object_get_stats(&object_stats) == 0 &&
        object_stats.vos_objects == 0 && object_stats.vos_anon_pages == 0 &&
        object_stats.vos_resident_pages == 0);
    CHECK(allocator.vpa_free_count == TEST_RAM_SIZE / VM_PAGE_SIZE);
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
    if (test_shm() != 0)
        return 1;
    puts("MIPS pmap/vmspace tests: ok");
    return 0;
}
