#include <machine/n64.h>

static unsigned detected_rdram_size;

static void
memory_barrier(void)
{
    asm volatile ("" ::: "memory");
}

static unsigned
read_uncached32(unsigned phys)
{
    unsigned value;

    memory_barrier();
    value = *(volatile unsigned *)N64_PHYS_TO_KSEG1(phys);
    memory_barrier();
    return value;
}

static void
write_uncached32(unsigned phys, unsigned value)
{
    memory_barrier();
    *(volatile unsigned *)N64_PHYS_TO_KSEG1(phys) = value;
    memory_barrier();
}

static unsigned
n64_boot_rdram_size(void)
{
    /*
     * IPL usually leaves the RDRAM size here, but early vectors may
     * clobber it before the kernel asks.
     */
    unsigned size = read_uncached32(N64_BOOT_MEM_SIZE_ADDR);

    if (size >= N64_RDRAM_THRESHOLD_8M && size <= N64_RDRAM_SIZE_8M)
        return N64_RDRAM_SIZE_8M;

    if (size >= N64_RDRAM_THRESHOLD_4M && size <= N64_RDRAM_SIZE_4M)
        return N64_RDRAM_SIZE_4M;

    return 0;
}

static int
n64_probe_expanded_rdram(void)
{
    unsigned saved_4m = read_uncached32(N64_RDRAM_PROBE_4M);
    unsigned saved_8m = read_uncached32(N64_RDRAM_PROBE_8M);
    int expanded;

    write_uncached32(N64_RDRAM_PROBE_4M, 0x13579bdfu);
    write_uncached32(N64_RDRAM_PROBE_8M, 0x2468ace0u);

    expanded =
        read_uncached32(N64_RDRAM_PROBE_8M) == 0x2468ace0u &&
        read_uncached32(N64_RDRAM_PROBE_4M) == 0x13579bdfu;

    write_uncached32(N64_RDRAM_PROBE_8M, saved_8m);
    write_uncached32(N64_RDRAM_PROBE_4M, saved_4m);
    return expanded;
}

unsigned
n64_rdram_size(void)
{
    if (detected_rdram_size == 0) {
        detected_rdram_size = n64_boot_rdram_size();
        if (detected_rdram_size != N64_RDRAM_SIZE_8M &&
            n64_probe_expanded_rdram())
            detected_rdram_size = N64_RDRAM_SIZE_8M;
        if (detected_rdram_size == 0)
            detected_rdram_size = N64_RDRAM_SIZE_4M;
    }

    return detected_rdram_size;
}
