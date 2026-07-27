/* Host-side regression tests for the legacy resource-map implementation. */

#include <sys/types.h>
#include <sys/map.h>
#include <stdio.h>
#include <string.h>

#define TEST_SWAP_BLOCKS       4096u
#define TEST_PAGE_BLOCKS       4u
#define TEST_PAGE_SLOTS        (TEST_SWAP_BLOCKS / TEST_PAGE_BLOCKS)
#define TEST_MAP_ENTRIES       ((TEST_PAGE_SLOTS + 1u) / 2u + 1u)
#define TEST_GUARD_SIZE        0x13579bdfu
#define TEST_GUARD_ADDR        0x2468ace0u

#define CHECK(expr) do {                                                \
    if (!(expr)) {                                                      \
        fprintf(stderr, "%s:%d: check failed: %s\n",                  \
            __FILE__, __LINE__, #expr);                                 \
        return 1;                                                       \
    }                                                                   \
} while (0)

void
panic(char *message)
{
    fprintf(stderr, "unexpected panic: %s\n", message);
    __builtin_trap();
}

static size_t
maximum_free_extents(size_t total, size_t allocation_unit)
{
    size_t allocations;
    size_t maximum;
    unsigned long pattern;
    unsigned long patterns;

    allocations = total / allocation_unit;
    patterns = 1ul << allocations;
    maximum = 0;
    for (pattern = 0; pattern < patterns; ++pattern) {
        size_t extents;
        size_t index;
        int previous_free;

        extents = 0;
        previous_free = 0;
        for (index = 0; index < allocations; ++index) {
            int current_free;

            current_free = (pattern & (1ul << index)) == 0;
            if (current_free && !previous_free)
                ++extents;
            previous_free = current_free;
        }
        if (total % allocation_unit != 0 && !previous_free)
            ++extents;
        if (extents > maximum)
            maximum = extents;
    }
    return maximum;
}

static int
test_runtime_sizing(void)
{
    size_t allocation_unit;
    size_t expected;
    size_t total;

    CHECK(rmap_required_entries(0, 4) == 0);
    CHECK(rmap_required_entries(4096, 0) == 0);
    CHECK(rmap_required_entries(4096, 4) == 513);
    CHECK(rmap_required_entries(32768, 4) == 4097);

    /*
     * Exhaust every allocation/free layout for small maps, including
     * devices whose reported size is not a multiple of the page unit.
     */
    for (allocation_unit = 1; allocation_unit <= 6; ++allocation_unit) {
        for (total = 1; total <= 12; ++total) {
            expected = maximum_free_extents(total,
                allocation_unit) + 1;
            CHECK(rmap_required_entries(total,
                allocation_unit) == expected);
        }
    }
    return 0;
}

static int
test_maximum_swap_fragmentation(void)
{
    struct {
        struct mapent entries[TEST_MAP_ENTRIES];
        struct mapent guard;
    } storage;
    struct map map;
    size_t slots[TEST_PAGE_SLOTS];
    size_t index;

    memset(&storage, 0, sizeof(storage));
    storage.guard.m_size = TEST_GUARD_SIZE;
    storage.guard.m_addr = TEST_GUARD_ADDR;
    map.m_map = storage.entries;
    map.m_limit = &storage.entries[TEST_MAP_ENTRIES];
    map.m_name = "test swapmap";

    mfree(&map, TEST_SWAP_BLOCKS, 1);
    for (index = 0; index < TEST_PAGE_SLOTS; ++index) {
        slots[index] = malloc(&map, TEST_PAGE_BLOCKS);
        CHECK(slots[index] == 1 + index * TEST_PAGE_BLOCKS);
    }
    CHECK(storage.entries[0].m_size == 0);

    for (index = 0; index < TEST_PAGE_SLOTS; index += 2)
        mfree(&map, TEST_PAGE_BLOCKS, slots[index]);
    CHECK(storage.entries[TEST_MAP_ENTRIES - 1].m_size == 0);
    CHECK(storage.guard.m_size == TEST_GUARD_SIZE);
    CHECK(storage.guard.m_addr == TEST_GUARD_ADDR);

    for (index = 1; index < TEST_PAGE_SLOTS; index += 2)
        mfree(&map, TEST_PAGE_BLOCKS, slots[index]);
    CHECK(storage.entries[0].m_addr == 1);
    CHECK(storage.entries[0].m_size == TEST_SWAP_BLOCKS);
    CHECK(storage.entries[1].m_size == 0);
    CHECK(storage.guard.m_size == TEST_GUARD_SIZE);
    CHECK(storage.guard.m_addr == TEST_GUARD_ADDR);
    return 0;
}

static int
test_overflow_does_not_write_past_limit(void)
{
    struct {
        struct mapent entries[3];
        struct mapent guard;
    } storage;
    struct map map;

    memset(&storage, 0, sizeof(storage));
    storage.guard.m_size = TEST_GUARD_SIZE;
    storage.guard.m_addr = TEST_GUARD_ADDR;
    map.m_map = storage.entries;
    map.m_limit = &storage.entries[3];
    map.m_name = "test map";

    mfree(&map, 4, 1);
    mfree(&map, 4, 9);
    mfree(&map, 4, 17);
    CHECK(storage.guard.m_size == TEST_GUARD_SIZE);
    CHECK(storage.guard.m_addr == TEST_GUARD_ADDR);
    return 0;
}

int
main(void)
{
    if (test_runtime_sizing() != 0)
        return 1;
    if (test_maximum_swap_fragmentation() != 0)
        return 1;
    if (test_overflow_does_not_write_past_limit() != 0)
        return 1;
    puts("resource map tests: ok");
    return 0;
}
