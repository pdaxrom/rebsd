/* Host-side tests for machine-independent virtual address maps. */

#include <errno.h>
#include <stdio.h>
#include <vm/vm_map.h>

#define CHECK(expr) do {                                                \
    if (!(expr)) {                                                      \
        fprintf(stderr, "%s:%d: check failed: %s\n",                  \
            __FILE__, __LINE__, #expr);                                 \
        return 1;                                                       \
    }                                                                   \
} while (0)

int
main(void)
{
    struct vm_map full;
    struct vm_map map;
    const struct vm_map_entry *entry;
    vm_vaddr_t address;
    vm_vaddr_t start;
    unsigned index;

    CHECK(vm_map_init(&map, 0x1000u, 0x80000000u) == 0);
    CHECK(vm_map_insert(&map, 0x10000000u, 0x10002000u,
        VM_PROT_READ | VM_PROT_WRITE, VM_PROT_ALL, VM_MAP_ANON) == 0);
    CHECK(vm_map_insert(&map, 0x20000000u, 0x20001000u,
        VM_PROT_READ, VM_PROT_READ, VM_MAP_ANON) == 0);
    CHECK(vm_map_insert(&map, 0x10002000u, 0x10004000u,
        VM_PROT_READ | VM_PROT_WRITE, VM_PROT_ALL, VM_MAP_ANON) == 0);
    CHECK(map.vmm_count == 2);
    CHECK(vm_map_findspace(&map, 0x10000000u, VM_PAGE_SIZE,
        &address) == 0);
    CHECK(address == 0x10004000u);
    CHECK(vm_map_findspace_color(&map, 0x10004000u, VM_PAGE_SIZE,
        3u * VM_PAGE_SIZE, 2u * VM_PAGE_SIZE, &address) == 0);
    CHECK(address == 0x10006000u);
    CHECK(vm_map_findspace_color(&map, 0x10004000u, VM_PAGE_SIZE,
        VM_PAGE_MASK, 0, &address) == EINVAL);
    CHECK(vm_map_findspace_color(&map, 0x10004000u, VM_PAGE_SIZE,
        2u * VM_PAGE_SIZE, VM_PAGE_SIZE, &address) == EINVAL);
    CHECK(vm_map_findspace(&map, 0x7ffff000u, 2 * VM_PAGE_SIZE,
        &address) == ENOMEM);
    CHECK(vm_map_lookup(&map, 0x10003fffu)->vme_start == 0x10000000u);
    CHECK(vm_map_insert(&map, 0x10001000u, 0x10003000u,
        VM_PROT_READ, VM_PROT_ALL, VM_MAP_ANON) == EEXIST);
    CHECK(vm_map_validate(&map) == 0);
    CHECK(vm_map_check(&map, 0x10000ff0u, 32,
        VM_PROT_READ | VM_PROT_WRITE) == 0);
    CHECK(vm_map_check(&map, 0x10003ff0u, 32, VM_PROT_READ) == EFAULT);
    CHECK(vm_map_check(&map, 0x20000000u, 1, VM_PROT_WRITE) == EFAULT);
    CHECK(vm_map_check(&map, 0xffffffffu, 1, VM_PROT_NONE) == EINVAL);
    CHECK(vm_map_check(&map, 0xfffff000u, VM_PAGE_SIZE,
        VM_PROT_NONE) == EINVAL);
    CHECK(vm_map_insert(&map, 0x80000000u, 0x80001000u,
        VM_PROT_READ, VM_PROT_READ, VM_MAP_ANON) == EINVAL);

    CHECK(vm_map_protect(&map, 0x10001000u, 0x10002000u,
        VM_PROT_READ) == 0);
    CHECK(map.vmm_count == 4);
    entry = vm_map_lookup(&map, 0x10001000u);
    CHECK(entry != 0 && entry->vme_protection == VM_PROT_READ);
    CHECK(vm_map_validate(&map) == 0);

    CHECK(vm_map_set_flags(&map, 0x10000000u, 0x10002000u,
        VM_MAP_WIRED, 0) == 0);
    CHECK((vm_map_lookup(&map, 0x10000000u)->vme_flags &
        VM_MAP_WIRED) != 0);
    CHECK(vm_map_set_flags(&map, 0x10000000u, 0x10001000u,
        0, VM_MAP_WIRED) == 0);
    CHECK((vm_map_lookup(&map, 0x10000000u)->vme_flags &
        VM_MAP_WIRED) == 0);
    CHECK((vm_map_lookup(&map, 0x10001000u)->vme_flags &
        VM_MAP_WIRED) != 0);

    CHECK(vm_map_remove(&map, 0x10000800u, 0x10001000u) == EINVAL);
    CHECK(vm_map_remove(&map, 0x10001000u, 0x10002000u) == 0);
    CHECK(vm_map_lookup(&map, 0x10001000u) == 0);
    CHECK(vm_map_validate(&map) == 0);

    CHECK(vm_map_init(&full, 0x1000u, 0x80000000u) == 0);
    for (index = 0; index < VM_MAP_MAX_ENTRIES; ++index) {
        start = 0x01000000u + index * 2 * VM_PAGE_SIZE;
        CHECK(vm_map_insert(&full, start, start + VM_PAGE_SIZE,
            VM_PROT_READ | VM_PROT_WRITE, VM_PROT_ALL,
            VM_MAP_ANON) == 0);
    }
    CHECK(full.vmm_count == VM_MAP_MAX_ENTRIES);
    start = 0x01000000u +
        (VM_MAP_MAX_ENTRIES - 1) * 2 * VM_PAGE_SIZE;
    CHECK(vm_map_insert(&full, start + VM_PAGE_SIZE,
        start + 2 * VM_PAGE_SIZE, VM_PROT_READ | VM_PROT_WRITE,
        VM_PROT_ALL, VM_MAP_ANON) == 0);
    CHECK(full.vmm_count == VM_MAP_MAX_ENTRIES);
    entry = vm_map_lookup(&full, start + VM_PAGE_SIZE);
    CHECK(entry != 0 && entry->vme_start == start &&
        entry->vme_end == start + 2 * VM_PAGE_SIZE);
    CHECK(vm_map_validate(&full) == 0);
    puts("VM map tests: ok");
    return 0;
}
