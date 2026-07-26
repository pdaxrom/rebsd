#include <sys/param.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/proc.h>

#include "interrupt.h"
#include "io.h"

#define PIT_INPUT_HZ       1193182u
#define PIT_CHANNEL_0      0x0040u
#define PIT_COMMAND        0x0043u
#define PIT_CHANNEL_0_RATE 0x34u

static volatile i386_u32 i386_timer_ticks;

static void
i386_pit_init(void)
{
    i386_u32 divisor;

    divisor = (PIT_INPUT_HZ + I386_PIT_HZ / 2u) / I386_PIT_HZ;
    i386_timer_ticks = 0;
    i386_outb(PIT_COMMAND, PIT_CHANNEL_0_RATE);
    i386_outb(PIT_CHANNEL_0, (i386_u8)(divisor & 0xffu));
    i386_outb(PIT_CHANNEL_0, (i386_u8)(divisor >> 8));
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
