#include <sys/param.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/proc.h>
#include <sys/systm.h>

#include "interrupt.h"
#include "io.h"

#define PIT_INPUT_HZ       1193182u
#define PIT_CHANNEL_0      0x0040u
#define PIT_COMMAND        0x0043u
#define PIT_CHANNEL_0_LATCH 0x00u
#define PIT_CHANNEL_0_RATE 0x34u

static volatile i386_u32 i386_timer_ticks;
static i386_u32 i386_pit_divisor;

static i386_u32
i386_pit_count(void)
{
    i386_u32 count;

    i386_outb(PIT_COMMAND, PIT_CHANNEL_0_LATCH);
    count = i386_inb(PIT_CHANNEL_0);
    count |= (i386_u32)i386_inb(PIT_CHANNEL_0) << 8;
    return count == 0 ? i386_pit_divisor : count;
}

static void
i386_pit_init(void)
{
    i386_u32 divisor;

    divisor = (PIT_INPUT_HZ + I386_PIT_HZ / 2u) / I386_PIT_HZ;
    i386_pit_divisor = divisor;
    i386_timer_ticks = 0;
    i386_outb(PIT_COMMAND, PIT_CHANNEL_0_RATE);
    i386_outb(PIT_CHANNEL_0, (i386_u8)(divisor & 0xffu));
    i386_outb(PIT_CHANNEL_0, (i386_u8)(divisor >> 8));
}

void
i386_microtime(struct timeval *tv, u_int tick_usec)
{
    i386_u32 count;
    i386_u32 elapsed;
    i386_u32 usec;

    if (tv == 0 || tick_usec == 0 || i386_pit_divisor == 0)
        return;
    count = i386_pit_count();
    if (i386_pic_irq_pending(I386_IRQ_TIMER))
        usec = tick_usec - 1u;
    else {
        if (count == 0 || count > i386_pit_divisor)
            count = i386_pit_divisor;
        elapsed = i386_pit_divisor - count;
        usec = (elapsed * tick_usec) / i386_pit_divisor;
        if (usec >= tick_usec)
            usec = tick_usec - 1u;
    }
    tv->tv_usec += usec;
    if (tv->tv_usec >= 1000000L) {
        tv->tv_sec += tv->tv_usec / 1000000L;
        tv->tv_usec %= 1000000L;
    }
}

void
udelay(unsigned usec)
{
    i386_u32 current;
    i386_u32 elapsed;
    i386_u32 previous;
    i386_u32 target;

    if (usec == 0)
        return;
    if (i386_pit_divisor == 0)
        panic("udelay before clkstart");

    target = (i386_u32)(((unsigned long long)PIT_INPUT_HZ * usec +
        999999u) / 1000000u);
    previous = i386_pit_count();
    elapsed = 0;
    while (elapsed < target) {
        current = i386_pit_count();
        if (current <= previous)
            elapsed += previous - current;
        else
            elapsed += previous + i386_pit_divisor - current;
        previous = current;
    }
}

void
i386_pit_interrupt(i386_u32 pc, i386_u32 ps)
{
    ++i386_timer_ticks;
    hardclock((caddr_t)(unsigned long)pc, (int)ps);
}

void
clkstart(void)
{
    i386_pit_init();
    i386_pic_unmask(I386_IRQ_TIMER);
}

i386_u32
i386_pit_ticks(void)
{
    return i386_timer_ticks;
}

void
i386_pit_wait(i386_u32 ticks)
{
    i386_u32 start;

    start = i386_timer_ticks;
    while ((i386_u32)(i386_timer_ticks - start) < ticks)
        __asm__ volatile ("sti; hlt" : : : "memory");
    __asm__ volatile ("cli");
}
