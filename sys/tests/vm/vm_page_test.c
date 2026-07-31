/* Host-side tests for the physical-page allocator. */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vm/vm_page.h>

#define TEST_RAM_BASE       0x00010000u
#define TEST_PAGE_COUNT     64u
#define TEST_RAM_SIZE       (TEST_PAGE_COUNT * VM_PAGE_SIZE)

#define CHECK(expr) do {                                                \
    if (!(expr)) {                                                      \
        fprintf(stderr, "%s:%d: check failed: %s\n",                  \
            __FILE__, __LINE__, #expr);                                 \
        return 1;                                                       \
    }                                                                   \
} while (0)

struct poison_memory {
    vm_paddr_t base;
    vm_size_t size;
    uint8_t *bytes;
};

static uint8_t simulated_ram[TEST_RAM_SIZE];
static struct vm_page metadata[TEST_PAGE_COUNT];

static int
test_poison(void *arg, vm_paddr_t paddr, uint8_t pattern, int verify)
{
    struct poison_memory *memory;
    vm_size_t offset;
    vm_size_t i;

    memory = (struct poison_memory *)arg;
    if (paddr < memory->base || paddr - memory->base >
        memory->size - VM_PAGE_SIZE)
        return EFAULT;
    offset = paddr - memory->base;
    if (verify) {
        for (i = 0; i < VM_PAGE_SIZE; ++i) {
            if (memory->bytes[offset + i] != pattern)
                return EFAULT;
        }
    } else {
        memset(&memory->bytes[offset], pattern, VM_PAGE_SIZE);
    }
    return 0;
}

static int
build_allocator(struct vm_phys_map *map,
    struct vm_page_allocator *allocator, struct poison_memory *memory)
{
    vm_paddr_t metadata_start;
    vm_size_t metadata_size;
    vm_pfn_t page_count;

    memset(simulated_ram, 0, sizeof(simulated_ram));
    memset(metadata, 0, sizeof(metadata));
    vm_phys_map_init(map);
    CHECK(vm_phys_map_add_ram(map, TEST_RAM_BASE, TEST_RAM_SIZE,
        "test ram") == 0);
    CHECK(vm_phys_map_reserve(map, TEST_RAM_BASE + VM_PAGE_SIZE,
        VM_PAGE_SIZE, "device") == 0);
    CHECK(vm_page_metadata_reserve(map, &metadata_start, &metadata_size,
        &page_count) == 0);
    CHECK(metadata_start == TEST_RAM_BASE + 2u * VM_PAGE_SIZE);
    CHECK(metadata_size == VM_PAGE_SIZE);
    CHECK(page_count == TEST_PAGE_COUNT);
    CHECK(vm_phys_map_finalize(map) == 0);
    CHECK(vm_page_allocator_init(allocator, map, metadata,
        sizeof(metadata)) == 0);
    memory->base = TEST_RAM_BASE;
    memory->size = TEST_RAM_SIZE;
    memory->bytes = simulated_ram;
    vm_page_allocator_set_poison(allocator, test_poison, memory);
    return 0;
}

static int
test_metadata_and_reserved_pages(void)
{
    struct vm_page_allocator allocator;
    struct vm_page_stats stats;
    struct poison_memory memory;
    struct vm_phys_map map;
    struct vm_page short_metadata[TEST_PAGE_COUNT - 1];
    struct vm_phys_map reserved_map;
    struct vm_page *page;
    vm_paddr_t metadata_start;
    vm_size_t metadata_size;
    vm_pfn_t page_count;

    CHECK(build_allocator(&map, &allocator, &memory) == 0);
    CHECK(vm_page_allocator_stats(&allocator, &stats) == 0);
    CHECK(stats.vps_total == TEST_PAGE_COUNT);
    CHECK(stats.vps_free == TEST_PAGE_COUNT - 2);
    CHECK(stats.vps_reserved == 2);
    CHECK(stats.vps_bad == 0);
    page = vm_page_lookup(&allocator, TEST_RAM_BASE + VM_PAGE_SIZE);
    CHECK(page != NULL && page->vmp_state == VM_PAGE_RESERVED);
    CHECK(vm_page_free_paddr(&allocator, page->vmp_paddr, 1) == EPERM);
    CHECK(vm_page_free_paddr(&allocator, TEST_RAM_BASE - VM_PAGE_SIZE,
        1) == ENOENT);
    CHECK(vm_page_lookup(&allocator, TEST_RAM_BASE + 1) == NULL);
    CHECK(vm_page_allocator_init(&allocator, &map, short_metadata,
        sizeof(short_metadata)) == ENOSPC);

    vm_phys_map_init(&reserved_map);
    CHECK(vm_phys_map_add_ram(&reserved_map, 0, 2u * VM_PAGE_SIZE,
        "ram") == 0);
    CHECK(vm_phys_map_reserve(&reserved_map, 0, 2u * VM_PAGE_SIZE,
        "all reserved") == 0);
    CHECK(vm_page_metadata_reserve(&reserved_map, &metadata_start,
        &metadata_size, &page_count) == ENOSPC);
    return 0;
}

static int
test_single_page_and_poison(void)
{
    struct vm_page_allocator allocator;
    struct vm_page_request request;
    struct vm_page_stats before;
    struct vm_page_stats after;
    struct poison_memory memory;
    struct vm_phys_map map;
    struct vm_page *page;
    vm_paddr_t paddr;
    vm_size_t offset;

    CHECK(build_allocator(&map, &allocator, &memory) == 0);
    CHECK(vm_page_allocator_stats(&allocator, &before) == 0);
    memset(simulated_ram, 0xa5, VM_PAGE_SIZE);
    vm_page_request_init(&request);
    CHECK(vm_page_alloc(&allocator, &request, &page) == 0);
    CHECK(page->vmp_state == VM_PAGE_ACTIVE);
    paddr = page->vmp_paddr;
    CHECK(paddr == TEST_RAM_BASE);
    offset = paddr - memory.base;
    CHECK(memory.bytes[offset] == 0 &&
        memory.bytes[offset + VM_PAGE_SIZE - 1] == 0);
    CHECK(vm_page_free(&allocator, page, 1) == 0);
    CHECK((page->vmp_flags & VM_PAGE_FLAG_POISONED) != 0);
    CHECK(vm_page_free(&allocator, page, 1) == EALREADY);

    memory.bytes[offset + 7] ^= 1;
    request.vpr_max_address = paddr + VM_PAGE_MASK;
    CHECK(vm_page_alloc(&allocator, &request, &page) == EFAULT);
    memset(&memory.bytes[offset], VM_PAGE_FREE_POISON, VM_PAGE_SIZE);
    CHECK(vm_page_alloc(&allocator, &request, &page) == 0);
    CHECK(page->vmp_paddr == paddr);
    CHECK(memory.bytes[offset] == 0 && memory.bytes[offset + 7] == 0 &&
        memory.bytes[offset + VM_PAGE_SIZE - 1] == 0);

    CHECK(vm_page_counter_dec(&allocator, page,
        VM_PAGE_COUNTER_HOLD) == EINVAL);
    CHECK(vm_page_counter_inc(&allocator, page,
        VM_PAGE_COUNTER_HOLD) == 0);
    CHECK(vm_page_free(&allocator, page, 1) == EBUSY);
    CHECK(vm_page_counter_dec(&allocator, page,
        VM_PAGE_COUNTER_HOLD) == 0);
    page->vmp_wire_count = UINT16_MAX;
    CHECK(vm_page_counter_inc(&allocator, page,
        VM_PAGE_COUNTER_WIRE) == EOVERFLOW);
    page->vmp_wire_count = 0;
    CHECK(vm_page_set_state(&allocator, page, VM_PAGE_WIRED) == EBUSY);
    CHECK(vm_page_counter_inc(&allocator, page,
        VM_PAGE_COUNTER_WIRE) == 0);
    CHECK(vm_page_set_state(&allocator, page, VM_PAGE_WIRED) == 0);
    CHECK(vm_page_counter_dec(&allocator, page,
        VM_PAGE_COUNTER_WIRE) == 0);
    CHECK(vm_page_set_state(&allocator, page, VM_PAGE_FREE) == EINVAL);
    CHECK(vm_page_free(&allocator, page, 1) == 0);
    CHECK(vm_page_allocator_validate(&allocator, &map) == 0);
    CHECK(vm_page_allocator_stats(&allocator, &after) == 0);
    CHECK(after.vps_free == before.vps_free);
    CHECK(after.vps_poison_failures == 1);
    CHECK(after.vps_allocation_failures == 1);
    return 0;
}

static int
test_constraints_and_contiguous_runs(void)
{
    struct vm_page_allocator allocator;
    struct vm_page_request request;
    struct poison_memory memory;
    struct vm_phys_map map;
    struct vm_page *first;
    struct vm_page *second;

    CHECK(build_allocator(&map, &allocator, &memory) == 0);
    vm_page_request_init(&request);
    request.vpr_npages = 2;
    request.vpr_alignment = 4u * VM_PAGE_SIZE;
    request.vpr_boundary = 8u * VM_PAGE_SIZE;
    request.vpr_max_address = TEST_RAM_BASE + 6u * VM_PAGE_SIZE - 1;
    allocator.vpa_free_hint = 5;
    CHECK(vm_page_alloc(&allocator, &request, &first) == 0);
    CHECK(first->vmp_paddr == TEST_RAM_BASE + 4u * VM_PAGE_SIZE);
    CHECK(vm_page_free(&allocator, first, 2) == 0);
    CHECK(vm_page_alloc(&allocator, &request, &first) == 0);
    CHECK((first->vmp_paddr & (request.vpr_alignment - 1)) == 0);
    CHECK(first->vmp_paddr == TEST_RAM_BASE + 4u * VM_PAGE_SIZE);
    CHECK(first[1].vmp_paddr == first->vmp_paddr + VM_PAGE_SIZE);

    request.vpr_alignment = VM_PAGE_SIZE;
    request.vpr_boundary = 0;
    request.vpr_max_address = VM_PADDR_MAX;
    CHECK(vm_page_alloc(&allocator, &request, &second) == 0);
    CHECK(second->vmp_paddr > first[1].vmp_paddr);
    CHECK(vm_page_free(&allocator, first, 2) == 0);
    CHECK(vm_page_free(&allocator, second, 2) == 0);

    vm_page_request_init(&request);
    request.vpr_color_mask = 3u * VM_PAGE_SIZE;
    request.vpr_color = 2u * VM_PAGE_SIZE;
    CHECK(vm_page_alloc(&allocator, &request, &first) == 0);
    CHECK((first->vmp_paddr & request.vpr_color_mask) ==
        request.vpr_color);
    CHECK(first->vmp_paddr == TEST_RAM_BASE + 6u * VM_PAGE_SIZE);
    CHECK(vm_page_free(&allocator, first, 1) == 0);

    vm_page_request_init(&request);
    request.vpr_npages = 3;
    request.vpr_boundary = 2u * VM_PAGE_SIZE;
    CHECK(vm_page_alloc(&allocator, &request, &first) == EINVAL);
    request.vpr_npages = 1;
    request.vpr_boundary = 3u * VM_PAGE_SIZE;
    CHECK(vm_page_alloc(&allocator, &request, &first) == EINVAL);
    request.vpr_boundary = 0;
    request.vpr_alignment = VM_PAGE_SIZE + 1;
    CHECK(vm_page_alloc(&allocator, &request, &first) == EINVAL);
    request.vpr_alignment = VM_PAGE_SIZE;
    request.vpr_color_mask = VM_PAGE_MASK;
    CHECK(vm_page_alloc(&allocator, &request, &first) == EINVAL);
    request.vpr_color_mask = 2u * VM_PAGE_SIZE;
    request.vpr_color = VM_PAGE_SIZE;
    CHECK(vm_page_alloc(&allocator, &request, &first) == EINVAL);
    request.vpr_color_mask = 0;
    request.vpr_color = 0;
    request.vpr_npages = 0;
    CHECK(vm_page_alloc(&allocator, &request, &first) == EINVAL);
    CHECK(vm_page_allocator_validate(&allocator, &map) == 0);
    return 0;
}

static int
test_discontinuous_runs(void)
{
    struct vm_page_allocator allocator;
    struct vm_page_request request;
    struct vm_phys_map map;
    struct vm_page discontinuous_metadata[6];
    struct vm_page *run;

    memset(&allocator, 0, sizeof(allocator));
    memset(discontinuous_metadata, 0, sizeof(discontinuous_metadata));
    vm_phys_map_init(&map);
    CHECK(vm_phys_map_add_ram(&map, TEST_RAM_BASE,
        2u * VM_PAGE_SIZE, "first ram") == 0);
    CHECK(vm_phys_map_add_ram(&map, TEST_RAM_BASE + 4u * VM_PAGE_SIZE,
        4u * VM_PAGE_SIZE, "second ram") == 0);
    CHECK(vm_phys_map_finalize(&map) == 0);
    CHECK(vm_page_allocator_init(&allocator, &map,
        discontinuous_metadata, sizeof(discontinuous_metadata)) == 0);
    vm_page_request_init(&request);
    request.vpr_npages = 3;
    CHECK(vm_page_alloc(&allocator, &request, &run) == 0);
    CHECK(run->vmp_paddr == TEST_RAM_BASE + 4u * VM_PAGE_SIZE);
    CHECK(vm_page_free(&allocator, run, 3) == 0);
    CHECK(vm_page_allocator_validate(&allocator, &map) == 0);
    return 0;
}

static int
test_low_memory_and_fragmentation(void)
{
    struct vm_page_allocator allocator;
    struct vm_page_request request;
    struct vm_page_stats baseline;
    struct vm_page_stats exhausted;
    struct poison_memory memory;
    struct vm_phys_map map;
    struct vm_page *allocated[TEST_PAGE_COUNT];
    struct vm_page *run;
    vm_pfn_t count;
    vm_pfn_t i;

    CHECK(build_allocator(&map, &allocator, &memory) == 0);
    CHECK(vm_page_allocator_stats(&allocator, &baseline) == 0);
    vm_page_request_init(&request);
    count = 0;
    while (count < TEST_PAGE_COUNT &&
        vm_page_alloc(&allocator, &request, &allocated[count]) == 0)
        ++count;
    CHECK(count == baseline.vps_free);
    CHECK(vm_page_allocator_stats(&allocator, &exhausted) == 0);
    CHECK(exhausted.vps_free == 0);
    CHECK(exhausted.vps_allocation_failures == 1);
    CHECK(vm_page_alloc(&allocator, &request, &run) == ENOMEM);

    for (i = 0; i < count; i += 2)
        CHECK(vm_page_free(&allocator, allocated[i], 1) == 0);
    request.vpr_npages = 2;
    CHECK(vm_page_alloc(&allocator, &request, &run) == ENOMEM);
    for (i = 1; i < count; i += 2)
        CHECK(vm_page_free(&allocator, allocated[i], 1) == 0);
    CHECK(vm_page_allocator_stats(&allocator, &exhausted) == 0);
    CHECK(exhausted.vps_free == baseline.vps_free);
    CHECK(vm_page_allocator_validate(&allocator, &map) == 0);
    return 0;
}

static int
test_device_owned_run(void)
{
    struct vm_page_allocator allocator;
    struct vm_page_request request;
    struct poison_memory memory;
    struct vm_phys_map map;
    struct vm_page *pages;

    CHECK(build_allocator(&map, &allocator, &memory) == 0);
    vm_page_request_init(&request);
    request.vpr_npages = 2;
    request.vpr_state = VM_PAGE_WIRED;
    CHECK(vm_page_alloc(&allocator, &request, &pages) == 0);
    CHECK(vm_page_device_claim(&allocator, pages, 2) == 0);
    CHECK(vm_page_device_claim(&allocator, pages, 2) == EALREADY);
    CHECK((pages[0].vmp_flags & VM_PAGE_FLAG_DEVICE) != 0);
    CHECK((pages[1].vmp_flags & VM_PAGE_FLAG_DEVICE) != 0);
    CHECK(vm_page_free(&allocator, pages, 2) == EBUSY);
    CHECK(vm_page_device_release(&allocator, pages, 2) == 0);
    CHECK(vm_page_device_release(&allocator, pages, 2) == EINVAL);
    CHECK(vm_page_counter_dec(&allocator, pages,
        VM_PAGE_COUNTER_WIRE) == 0);
    CHECK(vm_page_counter_dec(&allocator, pages + 1,
        VM_PAGE_COUNTER_WIRE) == 0);
    CHECK(vm_page_free(&allocator, pages, 2) == 0);
    CHECK(vm_page_allocator_validate(&allocator, &map) == 0);
    return 0;
}

static int
test_bad_page_quarantine(void)
{
    struct vm_page_allocator allocator;
    struct vm_page_request request;
    struct vm_page_stats before;
    struct vm_page_stats after;
    struct poison_memory memory;
    struct vm_phys_map map;
    struct vm_page *allocated[TEST_PAGE_COUNT];
    struct vm_page *page;
    vm_paddr_t bad_paddr;
    vm_pfn_t count;

    CHECK(build_allocator(&map, &allocator, &memory) == 0);
    CHECK(vm_page_allocator_stats(&allocator, &before) == 0);
    page = vm_page_lookup(&allocator, TEST_RAM_BASE);
    CHECK(page != NULL && page->vmp_state == VM_PAGE_FREE);
    bad_paddr = page->vmp_paddr;
    CHECK(vm_page_mark_bad(&allocator, page) == 0);
    CHECK(vm_page_mark_bad(&allocator, page) == EALREADY);
    CHECK(vm_page_free(&allocator, page, 1) == EPERM);
    CHECK(vm_page_mark_bad(&allocator,
        vm_page_lookup(&allocator, TEST_RAM_BASE + VM_PAGE_SIZE)) == EPERM);
    CHECK(vm_page_allocator_validate(&allocator, &map) == 0);
    CHECK(vm_page_allocator_stats(&allocator, &after) == 0);
    CHECK(after.vps_free == before.vps_free - 1);
    CHECK(after.vps_bad == 1);

    vm_page_request_init(&request);
    count = 0;
    while (count < TEST_PAGE_COUNT &&
        vm_page_alloc(&allocator, &request, &allocated[count]) == 0) {
        CHECK(allocated[count]->vmp_paddr != bad_paddr);
        ++count;
    }
    CHECK(count == after.vps_free);
    return 0;
}

int
main(void)
{
    if (test_metadata_and_reserved_pages() != 0)
        return 1;
    if (test_single_page_and_poison() != 0)
        return 1;
    if (test_constraints_and_contiguous_runs() != 0)
        return 1;
    if (test_discontinuous_runs() != 0)
        return 1;
    if (test_low_memory_and_fragmentation() != 0)
        return 1;
    if (test_device_owned_run() != 0)
        return 1;
    if (test_bad_page_quarantine() != 0)
        return 1;
    puts("vm page allocator tests: ok");
    return 0;
}
