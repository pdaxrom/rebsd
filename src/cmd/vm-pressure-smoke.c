/* Exercise anonymous allocation, swap pageout, and reverse-order pagein. */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define VM_PRESSURE_PAGE_SIZE       4096
#define VM_PRESSURE_PROGRESS_PAGES  16
#define VM_PRESSURE_EXTRA_MAX       128
#define VM_PRESSURE_PAGES_MAX       1536

struct vm_pressure_stats {
    long free_pages;
    long resident_pages;
    long swapped_pages;
    long pageins;
    long pageouts;
    long swap_failures;
    long reclaim_attempts;
    long reclaim_failures;
    struct timeval time;
};

static int
vm_pressure_sysctl(int leaf, long *value)
{
    int mib[2];
    size_t length;

    mib[0] = CTL_VM;
    mib[1] = leaf;
    length = sizeof(*value);
    if (sysctl(mib, 2, value, &length, 0, 0) < 0 ||
        length != sizeof(*value)) {
        fprintf(stderr, "vm-pressure-smoke: sysctl leaf=%d: %s\n",
            leaf, strerror(errno));
        return -1;
    }
    return 0;
}

static int
vm_pressure_snapshot(struct vm_pressure_stats *stats)
{
    if (vm_pressure_sysctl(VM_FREEPAGES, &stats->free_pages) != 0 ||
        vm_pressure_sysctl(VM_OBJECTRESIDENT,
            &stats->resident_pages) != 0 ||
        vm_pressure_sysctl(VM_OBJECTSWAPPED,
            &stats->swapped_pages) != 0 ||
        vm_pressure_sysctl(VM_PAGEINS, &stats->pageins) != 0 ||
        vm_pressure_sysctl(VM_PAGEOUTS, &stats->pageouts) != 0 ||
        vm_pressure_sysctl(VM_SWAPFAILURES, &stats->swap_failures) != 0 ||
        vm_pressure_sysctl(VM_RECLAIMATTEMPTS,
            &stats->reclaim_attempts) != 0 ||
        vm_pressure_sysctl(VM_RECLAIMFAILURES,
            &stats->reclaim_failures) != 0)
        return -1;
    if (gettimeofday(&stats->time, 0) != 0) {
        perror("vm-pressure-smoke: gettimeofday");
        return -1;
    }
    return 0;
}

static long
vm_pressure_elapsed_ms(const struct vm_pressure_stats *start,
    const struct vm_pressure_stats *now)
{
    long seconds;
    long useconds;

    seconds = now->time.tv_sec - start->time.tv_sec;
    useconds = now->time.tv_usec - start->time.tv_usec;
    return seconds * 1000 + useconds / 1000;
}

static void
vm_pressure_report(const char *phase, unsigned page,
    const struct vm_pressure_stats *start,
    const struct vm_pressure_stats *now)
{
    printf("VM_PRESSURE phase=%s page=%u elapsed_ms=%ld free=%ld "
        "resident=%ld swapped=%ld pageins=%ld pageouts=%ld "
        "swap_failures=%ld reclaim=%ld reclaim_failures=%ld\n",
        phase, page, vm_pressure_elapsed_ms(start, now), now->free_pages,
        now->resident_pages, now->swapped_pages,
        now->pageins - start->pageins, now->pageouts - start->pageouts,
        now->swap_failures - start->swap_failures,
        now->reclaim_attempts - start->reclaim_attempts,
        now->reclaim_failures - start->reclaim_failures);
    fflush(stdout);
}

static unsigned
vm_pressure_random(unsigned value)
{
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return value;
}

static void
vm_pressure_fill(unsigned char *page, unsigned index, int raw)
{
    unsigned value;
    unsigned offset;

    if (!raw) {
        memset(page, (unsigned char)(index * 37 + 11),
            VM_PRESSURE_PAGE_SIZE);
        return;
    }
    value = 0x9e3779b9u ^ (index + 1) * 0x85ebca6bu;
    for (offset = 0; offset < VM_PRESSURE_PAGE_SIZE; ++offset) {
        value = vm_pressure_random(value);
        page[offset] = (unsigned char)value;
    }
}

static int
vm_pressure_verify(const unsigned char *page, unsigned index, int raw,
    unsigned *bad_offset, unsigned *expected, unsigned *actual)
{
    unsigned value;
    unsigned byte;
    unsigned offset;

    value = 0x9e3779b9u ^ (index + 1) * 0x85ebca6bu;
    for (offset = 0; offset < VM_PRESSURE_PAGE_SIZE; ++offset) {
        if (raw) {
            value = vm_pressure_random(value);
            byte = (unsigned char)value;
        } else {
            byte = (unsigned char)(index * 37 + 11);
        }
        if (page[offset] != byte) {
            *bad_offset = offset;
            *expected = byte;
            *actual = page[offset];
            return -1;
        }
    }
    return 0;
}

static void
vm_pressure_usage(void)
{
    fputs("usage: vm-pressure-smoke [-rs] [-p pages] [-n passes]\n",
        stderr);
    exit(2);
}

int
main(int argc, char **argv)
{
    struct vm_pressure_stats before;
    struct vm_pressure_stats after_fill;
    struct vm_pressure_stats after_verify;
    struct vm_pressure_stats after_free;
    struct vm_pressure_stats progress;
    unsigned char *arena;
    unsigned long requested_pages;
    unsigned long requested_passes;
    unsigned pages;
    unsigned passes;
    unsigned extra;
    unsigned page;
    unsigned pass;
    unsigned bad_offset;
    unsigned expected;
    unsigned actual;
    long swap_bytes;
    long swap_pages;
    int raw;
    int require_swap;
    int ch;

    raw = 0;
    require_swap = 0;
    requested_pages = 0;
    requested_passes = 1;
    while ((ch = getopt(argc, argv, "p:n:rs")) != EOF) {
        switch (ch) {
        case 'p':
            requested_pages = strtoul(optarg, 0, 0);
            if (requested_pages == 0 || *optarg == '\0')
                vm_pressure_usage();
            break;
        case 'n':
            requested_passes = strtoul(optarg, 0, 0);
            if (requested_passes == 0 || *optarg == '\0')
                vm_pressure_usage();
            break;
        case 'r':
            raw = 1;
            break;
        case 's':
            require_swap = 1;
            break;
        default:
            vm_pressure_usage();
        }
    }
    if (optind != argc || requested_pages > VM_PRESSURE_PAGES_MAX ||
        requested_passes > 32)
        vm_pressure_usage();
    if (vm_pressure_snapshot(&before) != 0 ||
        vm_pressure_sysctl(VM_SWAPTOTAL, &swap_bytes) != 0)
        return 1;
    swap_pages = swap_bytes / VM_PRESSURE_PAGE_SIZE;
    if (requested_pages != 0) {
        pages = (unsigned)requested_pages;
    } else {
        extra = swap_pages > 0 ? (unsigned)swap_pages / 4 : 0;
        if (extra > VM_PRESSURE_EXTRA_MAX)
            extra = VM_PRESSURE_EXTRA_MAX;
        if (extra < VM_PRESSURE_PROGRESS_PAGES)
            extra = VM_PRESSURE_PROGRESS_PAGES;
        pages = (unsigned)before.free_pages + extra;
        if (pages > VM_PRESSURE_PAGES_MAX)
            pages = VM_PRESSURE_PAGES_MAX;
    }
    passes = (unsigned)requested_passes;
    printf("VM_PRESSURE_BEGIN version=1 pages=%u bytes=%u passes=%u "
        "pattern=%s swap_pages=%ld\n", pages,
        pages * VM_PRESSURE_PAGE_SIZE, passes,
        raw ? "raw" : "compressible", swap_pages);
    vm_pressure_report("before", 0, &before, &before);

    arena = mmap(0, pages * VM_PRESSURE_PAGE_SIZE,
        PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (arena == MAP_FAILED) {
        perror("vm-pressure-smoke: mmap");
        return 1;
    }
    for (page = 0; page < pages; ++page) {
        vm_pressure_fill(arena + page * VM_PRESSURE_PAGE_SIZE, page, raw);
        if ((page + 1) % VM_PRESSURE_PROGRESS_PAGES == 0 ||
            page + 1 == pages) {
            if (vm_pressure_snapshot(&progress) != 0)
                return 1;
            vm_pressure_report("fill", page + 1, &before, &progress);
        }
    }
    if (vm_pressure_snapshot(&after_fill) != 0)
        return 1;

    for (pass = 0; pass < passes; ++pass) {
        page = pages;
        while (page-- != 0) {
            if (vm_pressure_verify(arena + page * VM_PRESSURE_PAGE_SIZE,
                page, raw, &bad_offset, &expected, &actual) != 0) {
                fprintf(stderr, "VM_PRESSURE_CORRUPTION pass=%u page=%u "
                    "offset=%u expected=%02x actual=%02x\n", pass + 1,
                    page, bad_offset, expected, actual);
                return 1;
            }
            if ((pages - page) % VM_PRESSURE_PROGRESS_PAGES == 0 ||
                page == 0) {
                if (vm_pressure_snapshot(&progress) != 0)
                    return 1;
                vm_pressure_report("verify", page, &before, &progress);
            }
        }
    }
    if (vm_pressure_snapshot(&after_verify) != 0)
        return 1;
    if (munmap(arena, pages * VM_PRESSURE_PAGE_SIZE) != 0) {
        perror("vm-pressure-smoke: munmap");
        return 1;
    }
    if (vm_pressure_snapshot(&after_free) != 0)
        return 1;
    vm_pressure_report("after-free", pages, &before, &after_free);

    if (after_fill.pageouts == before.pageouts) {
        printf("VM_PRESSURE_NO_PAGEOUT pages=%u free_before=%ld "
            "free_after_fill=%ld\n", pages, before.free_pages,
            after_fill.free_pages);
        if (require_swap)
            return 1;
    } else if (after_verify.pageins == before.pageins) {
        printf("VM_PRESSURE_NO_PAGEIN pageouts=%ld\n",
            after_fill.pageouts - before.pageouts);
        if (require_swap)
            return 1;
    }
    printf("VM_PRESSURE_OK pages=%u pageouts=%ld pageins=%ld "
        "swap_failures=%ld reclaim=%ld reclaim_failures=%ld "
        "swapped_after_free=%ld\n", pages,
        after_free.pageouts - before.pageouts,
        after_free.pageins - before.pageins,
        after_free.swap_failures - before.swap_failures,
        after_free.reclaim_attempts - before.reclaim_attempts,
        after_free.reclaim_failures - before.reclaim_failures,
        after_free.swapped_pages);
    return 0;
}
