#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <machine/io.h>
#include <machine/n64.h>

dev_t rootdev;
dev_t swapdev;
dev_t pipedev;
int boothowto;

extern char _end[];

/*
 * Minimal machine startup. Full root device selection and cache/TLB setup
 * will be filled in when the real kernel link is enabled.
 */
void
startup(void)
{
    physmem = n64_rdram_size();
    rootdev = makedev(0, 0);
    swapdev = makedev(0, 1);
    pipedev = swapdev;
}

void
idle(void)
{
    for (;;)
        asm volatile ("wait");
}

void
udelay(unsigned usec)
{
    unsigned start = mips_read_c0_register(C0_COUNT, 0);
    unsigned ticks = (N64_COUNT_KHZ * usec + 999u) / 1000u;

    while ((unsigned)(mips_read_c0_register(C0_COUNT, 0) - start) < ticks)
        ;
}

void
led_control(int mask, int on)
{
}

int
baduaddr(caddr_t addr)
{
    unsigned a = (unsigned)addr;

    return a < USER_DATA_START || a >= USER_DATA_END;
}

int
badkaddr(caddr_t addr)
{
    unsigned a = (unsigned)addr;

    return a < KERNEL_DATA_START || a >= (unsigned)&_end;
}

int
copyout(caddr_t from, caddr_t to, u_int nbytes)
{
    if (baduaddr(to) || baduaddr(to + nbytes - 1))
        return EFAULT;
    bcopy(from, to, nbytes);
    return 0;
}

int
copyin(caddr_t from, caddr_t to, u_int nbytes)
{
    if (baduaddr(from) || baduaddr(from + nbytes - 1))
        return EFAULT;
    bcopy(from, to, nbytes);
    return 0;
}

void
boot(dev_t dev, int howto)
{
    printf("reboot requested: dev=%d,%d howto=%#x\n",
        major(dev), minor(dev), howto);
    for (;;)
        ;
}
