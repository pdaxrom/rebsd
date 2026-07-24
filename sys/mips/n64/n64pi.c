#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <machine/n64pi.h>

#define N64_PI_STATUS          (*(volatile unsigned *)0xa4600010u)
#define N64_PI_STATUS_DMA_BUSY 0x00000001u
#define N64_PI_STATUS_IO_BUSY  0x00000002u
#define N64_PI_STATUS_ERROR    0x00000004u
#define N64_PI_STATUS_RESET    0x00000001u
#define N64_PI_STATUS_CLR_INTR 0x00000002u
#define N64_PI_STATUS_BUSY     (N64_PI_STATUS_DMA_BUSY | \
                                N64_PI_STATUS_IO_BUSY)

static const void *n64pi_owner;
static unsigned n64pi_depth;
static int n64pi_saved_spl;

static void
n64pi_wait_idle(void)
{
    unsigned status;

    for (;;) {
        status = N64_PI_STATUS;
        if ((status & N64_PI_STATUS_BUSY) == 0)
            break;
    }

    if (status & N64_PI_STATUS_ERROR) {
        N64_PI_STATUS = N64_PI_STATUS_RESET | N64_PI_STATUS_CLR_INTR;
        asm volatile ("sync" ::: "memory");
        while (N64_PI_STATUS & N64_PI_STATUS_BUSY)
            ;
    }
}

void
n64pi_init(void)
{
    int s;

    s = splhigh();
    n64pi_owner = 0;
    n64pi_depth = 0;
    n64pi_saved_spl = s;
    n64pi_wait_idle();
    splx(s);
}

int
n64pi_bus_enter(const void *owner)
{
    int s;

    if (owner == 0)
        return EINVAL;

    s = splhigh();
    if (n64pi_owner == owner) {
        ++n64pi_depth;
        return 0;
    }

    /*
     * A valid owner cannot be descheduled while holding the lock because
     * interrupts remain masked until n64pi_bus_leave().  The loop is kept as
     * the explicit contract for any future multi-CPU or early-boot caller:
     * wait, do not enqueue and do not fail.
     */
    while (n64pi_owner != 0) {
        splx(s);
        s = splhigh();
    }

    n64pi_owner = owner;
    n64pi_depth = 1;
    n64pi_saved_spl = s;
    n64pi_wait_idle();
    return 0;
}

void
n64pi_bus_leave(const void *owner)
{
    int s;

    if (owner == 0 || n64pi_owner != owner)
        panic("n64pi bus owner");
    if (--n64pi_depth != 0)
        return;

    n64pi_wait_idle();
    s = n64pi_saved_spl;
    n64pi_owner = 0;
    asm volatile ("sync" ::: "memory");
    splx(s);
}

int
n64pi_is_busy(void)
{
    return n64pi_owner != 0 || (N64_PI_STATUS & N64_PI_STATUS_BUSY) != 0;
}
