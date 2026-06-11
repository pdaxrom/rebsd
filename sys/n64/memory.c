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

unsigned
n64_rdram_size(void)
{
    if (detected_rdram_size == 0) {
        detected_rdram_size = n64_boot_rdram_size();
        if (detected_rdram_size == 0)
            detected_rdram_size = N64_RDRAM_SIZE_4M;
    }

    return detected_rdram_size;
}
