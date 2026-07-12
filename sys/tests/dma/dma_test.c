/*
 * Host-side tests for the target-independent DMA pool allocator.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/dma.h>

#define TEST_POOL_SIZE  (16u * 1024u)
#define TEST_POOL_PADDR 0x01000000u

static unsigned char test_pool[TEST_POOL_SIZE]
    __attribute__((aligned(4096)));
static unsigned device_syncs;
static unsigned cpu_syncs;
static int callback_error;

#define CHECK(expr) do {                                                \
    if (!(expr)) {                                                      \
        fprintf(stderr, "%s:%d: check failed: %s\n",                  \
            __FILE__, __LINE__, #expr);                                 \
        return 1;                                                       \
    }                                                                   \
} while (0)

static void
sync_for_device(const struct dma_mem *mem, size_t offset, size_t length,
    enum dma_direction direction)
{
    if (mem == 0 || offset != 16 || length != 32 ||
        direction != DMA_TO_DEVICE)
        callback_error = 1;
    ++device_syncs;
}

static void
sync_for_cpu(const struct dma_mem *mem, size_t offset, size_t length,
    enum dma_direction direction)
{
    if (mem == 0 || offset != 8 || length != 24 ||
        direction != DMA_FROM_DEVICE)
        callback_error = 1;
    ++cpu_syncs;
}

static const struct dma_backend_ops test_ops = {
    sync_for_device,
    sync_for_cpu,
};

static int
test_validation(void)
{
    struct dma_mem mem;

    memset(&mem, 0, sizeof(mem));
    CHECK(dma_pool_ready() == 0);
    CHECK(dma_pool_size() == 0);
    CHECK(dma_pool_available() == 0);
    CHECK(dma_alloc(&mem, 16, 16, DMA_CONTIGUOUS) == ENXIO);
    CHECK(dma_pool_init(0, TEST_POOL_PADDR, TEST_POOL_SIZE,
        DMA_CONTIGUOUS, 0) == EINVAL);
    CHECK(dma_pool_init(test_pool, TEST_POOL_PADDR, 0,
        DMA_CONTIGUOUS, 0) == EINVAL);
    CHECK(dma_pool_init(test_pool, TEST_POOL_PADDR, TEST_POOL_SIZE,
        0, 0) == EINVAL);
    CHECK(dma_pool_init(test_pool, TEST_POOL_PADDR, TEST_POOL_SIZE,
        DMA_CONTIGUOUS | 0x8000u, 0) == EINVAL);
    return 0;
}

static int
test_allocation_and_sync(void)
{
    struct dma_mem mem;
    struct dma_mem stale;
    unsigned char *bytes;
    size_t i;

    memset(test_pool, 0xa5, sizeof(test_pool));
    memset(&mem, 0, sizeof(mem));
    CHECK(dma_pool_init(test_pool, TEST_POOL_PADDR, TEST_POOL_SIZE,
        DMA_32BIT | DMA_COHERENT | DMA_CONTIGUOUS, &test_ops) == 0);
    CHECK(dma_pool_ready() != 0);
    CHECK(dma_pool_size() == TEST_POOL_SIZE);
    CHECK(dma_pool_available() == TEST_POOL_SIZE);

    CHECK(dma_alloc(0, 64, 16, 0) == EINVAL);
    CHECK(dma_alloc(&mem, 0, 16, 0) == EINVAL);
    CHECK(dma_alloc(&mem, 64, 0, 0) == EINVAL);
    CHECK(dma_alloc(&mem, 64, 24, 0) == EINVAL);
    CHECK(dma_alloc(&mem, 64, 16, 0x8000u) == EINVAL);

    CHECK(dma_alloc(&mem, 257, 128,
        DMA_ZERO | DMA_32BIT | DMA_COHERENT | DMA_CONTIGUOUS) == 0);
    CHECK(((uintptr_t)mem.dm_vaddr & 127u) == 0);
    CHECK((mem.dm_paddr & 127u) == 0);
    CHECK(mem.dm_paddr == TEST_POOL_PADDR +
        (dma_addr_t)((unsigned char *)mem.dm_vaddr - test_pool));
    CHECK(mem.dm_size == 257);
    CHECK(mem.dm_align == 128);
    CHECK(dma_pool_available() == TEST_POOL_SIZE - 257);
    bytes = mem.dm_vaddr;
    for (i = 0; i < mem.dm_size; ++i)
        CHECK(bytes[i] == 0);

    CHECK(dma_alloc(&mem, 16, 16, 0) == EBUSY);
    CHECK(dma_sync_for_device(&mem, 16, 32, DMA_TO_DEVICE) == 0);
    CHECK(dma_sync_for_cpu(&mem, 8, 24, DMA_FROM_DEVICE) == 0);
    CHECK(device_syncs == 1 && cpu_syncs == 1 && callback_error == 0);
    CHECK(dma_sync_for_device(&mem, 258, 0, DMA_TO_DEVICE) == EINVAL);
    CHECK(dma_sync_for_cpu(&mem, 250, 8, DMA_FROM_DEVICE) == EINVAL);
    CHECK(dma_sync_for_cpu(&mem, 0, 1, (enum dma_direction)99) == EINVAL);
    CHECK(dma_pool_init(test_pool, TEST_POOL_PADDR, TEST_POOL_SIZE,
        DMA_CONTIGUOUS, 0) == EBUSY);

    stale = mem;
    CHECK(dma_free(&mem) == 0);
    CHECK(mem.dm_vaddr == 0 && mem.dm_cookie == 0);
    CHECK(dma_pool_available() == TEST_POOL_SIZE);
    CHECK(dma_free(&stale) == EINVAL);
    CHECK(dma_sync_for_device(&stale, 0, 1, DMA_TO_DEVICE) == EINVAL);
    return 0;
}

static int
test_capabilities(void)
{
    struct dma_mem mem;

    memset(&mem, 0, sizeof(mem));
    CHECK(dma_pool_init(test_pool, TEST_POOL_PADDR, TEST_POOL_SIZE,
        DMA_CONTIGUOUS, 0) == 0);
    CHECK(dma_alloc(&mem, 32, 16, DMA_COHERENT) == EOPNOTSUPP);
    CHECK(dma_alloc(&mem, 32, 16, DMA_CONTIGUOUS) == 0);
    CHECK(dma_free(&mem) == 0);
    return 0;
}

static int
test_reuse_and_record_limit(void)
{
    struct dma_mem mem[DMA_MAX_ALLOCS + 1];
    dma_addr_t first_paddr;
    unsigned i;

    memset(mem, 0, sizeof(mem));
    CHECK(dma_pool_init(test_pool, TEST_POOL_PADDR, TEST_POOL_SIZE,
        DMA_32BIT | DMA_COHERENT | DMA_CONTIGUOUS, &test_ops) == 0);

    CHECK(dma_alloc(&mem[0], 64, 64, 0) == 0);
    first_paddr = mem[0].dm_paddr;
    CHECK(dma_free(&mem[0]) == 0);
    CHECK(dma_alloc(&mem[0], 64, 64, 0) == 0);
    CHECK(mem[0].dm_paddr == first_paddr);

    for (i = 1; i < DMA_MAX_ALLOCS; ++i)
        CHECK(dma_alloc(&mem[i], 16, 16, 0) == 0);
    CHECK(dma_alloc(&mem[DMA_MAX_ALLOCS], 16, 16, 0) == ENOMEM);
    for (i = 0; i < DMA_MAX_ALLOCS; ++i)
        CHECK(dma_free(&mem[i]) == 0);
    CHECK(dma_pool_available() == TEST_POOL_SIZE);
    return 0;
}

static int
test_exhaustion(void)
{
    struct dma_mem whole;
    struct dma_mem extra;

    memset(&whole, 0, sizeof(whole));
    memset(&extra, 0, sizeof(extra));
    CHECK(dma_pool_init(test_pool, TEST_POOL_PADDR, TEST_POOL_SIZE,
        DMA_CONTIGUOUS, 0) == 0);
    CHECK(dma_alloc(&whole, TEST_POOL_SIZE, 4096, DMA_CONTIGUOUS) == 0);
    CHECK(dma_pool_available() == 0);
    CHECK(dma_alloc(&extra, 1, 1, 0) == ENOMEM);
    CHECK(dma_free(&whole) == 0);
    CHECK(dma_pool_available() == TEST_POOL_SIZE);
    return 0;
}

int
main(void)
{
    CHECK(test_validation() == 0);
    CHECK(test_allocation_and_sync() == 0);
    CHECK(test_capabilities() == 0);
    CHECK(test_reuse_and_record_limit() == 0);
    CHECK(test_exhaustion() == 0);
    puts("dma_test: all tests passed");
    return 0;
}
