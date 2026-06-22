#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <machine/io.h>
#include <machine/n64.h>
#include <machine/n64int.h>
#include <machine/si.h>

#define N64_SI_DRAM_ADDR_ADDR       0xa4800000u
#define N64_SI_PIF_ADDR_READ_ADDR   0xa4800004u
#define N64_SI_PIF_ADDR_WRITE_ADDR  0xa4800010u
#define N64_SI_PIF_RAM_ADDR         0x1fc007c0u

#define N64_SI_STATUS_DMA_BUSY      0x00000001u
#define N64_SI_STATUS_IO_BUSY       0x00000002u
#define N64_SI_STATUS_BUSY \
    (N64_SI_STATUS_DMA_BUSY | N64_SI_STATUS_IO_BUSY)

#define N64_SI_TIMEOUT_USEC         20000u

#define N64_REG32(addr)             (*(volatile unsigned *)(addr))
#define N64_SI_DRAM_ADDR            N64_REG32(N64_SI_DRAM_ADDR_ADDR)
#define N64_SI_PIF_ADDR_READ        N64_REG32(N64_SI_PIF_ADDR_READ_ADDR)
#define N64_SI_PIF_ADDR_WRITE       N64_REG32(N64_SI_PIF_ADDR_WRITE_ADDR)
#define N64_SI_STATUS               N64_REG32(N64_SI_STATUS_ADDR)

static unsigned char n64_si_input[N64_SI_BLOCK_SIZE] __attribute__((aligned(16)));
static unsigned char n64_si_output[N64_SI_BLOCK_SIZE] __attribute__((aligned(16)));
static int n64_si_locked;

static unsigned
n64_si_phys(const void *ptr)
{
    return (unsigned)ptr & N64_KSEG_PHYS_MASK;
}

static void *
n64_si_uncached(void *ptr)
{
    return N64_PHYS_TO_KSEG1(n64_si_phys(ptr));
}

static int
n64_si_wait_idle(void)
{
    unsigned start = mips_read_c0_register(C0_COUNT, 0);
    unsigned ticks = (N64_COUNT_KHZ * N64_SI_TIMEOUT_USEC + 999u) / 1000u;

    while (N64_SI_STATUS & N64_SI_STATUS_BUSY) {
        if ((unsigned)(mips_read_c0_register(C0_COUNT, 0) - start) >= ticks)
            return ETIMEDOUT;
    }
    return 0;
}

int
n64_si_exec(const void *input, void *output)
{
    unsigned char *in;
    unsigned char *out;
    int s;
    int error;

    if (input == 0 || output == 0)
        return EINVAL;

    s = splhigh();
    if (n64_si_locked) {
        splx(s);
        return EBUSY;
    }
    n64_si_locked = 1;
    splx(s);

    in = n64_si_uncached(n64_si_input);
    out = n64_si_uncached(n64_si_output);

    error = n64_si_wait_idle();
    if (error)
        goto done;

    bcopy(input, in, N64_SI_BLOCK_SIZE);
    bzero(out, N64_SI_BLOCK_SIZE);
    N64_SI_STATUS = 0;

    N64_SI_DRAM_ADDR = n64_si_phys(n64_si_input);
    asm volatile ("" ::: "memory");
    N64_SI_PIF_ADDR_WRITE = N64_SI_PIF_RAM_ADDR;
    asm volatile ("" ::: "memory");

    error = n64_si_wait_idle();
    if (error)
        goto done;

    N64_SI_STATUS = 0;
    N64_SI_DRAM_ADDR = n64_si_phys(n64_si_output);
    asm volatile ("" ::: "memory");
    N64_SI_PIF_ADDR_READ = N64_SI_PIF_RAM_ADDR;
    asm volatile ("" ::: "memory");

    error = n64_si_wait_idle();
    if (error)
        goto done;

    bcopy(out, output, N64_SI_BLOCK_SIZE);

done:
    N64_SI_STATUS = 0;
    s = splhigh();
    n64_si_locked = 0;
    splx(s);
    return error;
}
