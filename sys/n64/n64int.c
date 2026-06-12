#include <sys/param.h>
#include <machine/n64int.h>

#define N64_REG32(addr)         (*(volatile unsigned *)(addr))

#define N64_SP_STATUS           N64_REG32(N64_SP_STATUS_ADDR)
#define N64_MI_MODE             N64_REG32(N64_MI_MODE_ADDR)
#define N64_MI_INTERRUPT        N64_REG32(N64_MI_INTERRUPT_ADDR)
#define N64_MI_MASK             N64_REG32(N64_MI_MASK_ADDR)
#define N64_VI_CURRENT          N64_REG32(N64_VI_CURRENT_ADDR)
#define N64_AI_STATUS           N64_REG32(N64_AI_STATUS_ADDR)
#define N64_PI_STATUS           N64_REG32(N64_PI_STATUS_ADDR)
#define N64_SI_STATUS           N64_REG32(N64_SI_STATUS_ADDR)

static unsigned
n64_mi_mask_write(unsigned mask, int enable)
{
    unsigned value = 0;

    if (mask & N64_MI_INTERRUPT_SP)
        value |= enable ? N64_MI_WMASK_SET_SP : N64_MI_WMASK_CLR_SP;
    if (mask & N64_MI_INTERRUPT_SI)
        value |= enable ? N64_MI_WMASK_SET_SI : N64_MI_WMASK_CLR_SI;
    if (mask & N64_MI_INTERRUPT_AI)
        value |= enable ? N64_MI_WMASK_SET_AI : N64_MI_WMASK_CLR_AI;
    if (mask & N64_MI_INTERRUPT_VI)
        value |= enable ? N64_MI_WMASK_SET_VI : N64_MI_WMASK_CLR_VI;
    if (mask & N64_MI_INTERRUPT_PI)
        value |= enable ? N64_MI_WMASK_SET_PI : N64_MI_WMASK_CLR_PI;
    if (mask & N64_MI_INTERRUPT_DP)
        value |= enable ? N64_MI_WMASK_SET_DP : N64_MI_WMASK_CLR_DP;
    return value;
}

void
n64_interrupt_init(void)
{
    n64_mi_disable(N64_MI_INTERRUPT_ALL);
}

void
n64_interrupt_shutdown(void)
{
    n64_mi_disable(N64_MI_INTERRUPT_ALL);
    n64_mi_ack(N64_MI_INTERRUPT_ALL);
}

unsigned
n64_mi_pending(void)
{
    return N64_MI_INTERRUPT & N64_MI_MASK;
}

void
n64_mi_enable(unsigned mask)
{
    N64_MI_MASK = n64_mi_mask_write(mask, 1);
}

void
n64_mi_disable(unsigned mask)
{
    N64_MI_MASK = n64_mi_mask_write(mask, 0);
}

void
n64_mi_ack(unsigned mask)
{
    if (mask & N64_MI_INTERRUPT_SP)
        N64_SP_STATUS = N64_SP_CLEAR_INTERRUPT;
    if (mask & N64_MI_INTERRUPT_SI)
        N64_SI_STATUS = 0;
    if (mask & N64_MI_INTERRUPT_AI)
        N64_AI_STATUS = 0;
    if (mask & N64_MI_INTERRUPT_VI)
        N64_VI_CURRENT = N64_VI_CURRENT;
    if (mask & N64_MI_INTERRUPT_PI)
        N64_PI_STATUS = N64_PI_CLEAR_INTERRUPT;
    if (mask & N64_MI_INTERRUPT_DP)
        N64_MI_MODE = N64_MI_WMODE_CLR_DPINT;
}

void
n64_interrupt_handle_mi(void)
{
    unsigned pending = n64_mi_pending();

    if (pending == 0)
        return;

    n64_mi_ack(pending);
}
