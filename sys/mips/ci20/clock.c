#include <sys/param.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <machine/io.h>

#define CI20_INTC       0xb0001000u
#define CI20_TCU        0xb0002000u

#define INTC_CLEAR_MASK 0x0c
#define INTC_CHIP_SIZE  0x20

#define TCU_TESR        0x14
#define TCU_TECR        0x18
#define TCU_TFR         0x20
#define TCU_TFCR        0x28
#define TCU_TMSR        0x34
#define TCU_TMCR        0x38
#define TCU_TSCR        0x3c
#define TCU_TDFR(n)     (0x40 + (n) * 0x10)
#define TCU_TDHR(n)     (0x44 + (n) * 0x10)
#define TCU_TCNT(n)     (0x48 + (n) * 0x10)
#define TCU_TCSR(n)     (0x4c + (n) * 0x10)
#define TCU_TCSR_PRESCALE64 (0x3 << 3)
#define TCU_TCSR_EXT_EN (1 << 2)
#define TCU_FFLAG(n)    (1u << (n))
#define TCU_HFLAG(n)    (1u << ((n) + 16))

#define CI20_TCU_IRQ        25
#define CI20_TCU_CHANNEL    0
#define CI20_EXTCLK_HZ      48000000u
#define CI20_TCU_DIVISOR    64u
#define CI20_TCU_RATE       (CI20_EXTCLK_HZ / CI20_TCU_DIVISOR)
#define CI20_TCU_PERIOD     ((CI20_TCU_RATE + HZ / 2) / HZ)
#define CI20_COUNT_PERIOD   ((MIPS_COUNT_KHZ * 1000u + HZ - 1) / HZ)

#if CI20_TCU_PERIOD > 0xffffu
#error CI20_TCU_PERIOD does not fit the JZ4780 TCU channel counter
#endif

extern unsigned long mips_timer_count_to_usec(unsigned count);
extern void mips_timer_record(unsigned long late_us, unsigned long clock_us);

static unsigned ci20_clock_last_count;
static int ci20_clock_last_count_valid;

static volatile unsigned *
tcu_reg(unsigned offset)
{
    return (volatile unsigned *)(CI20_TCU + offset);
}

static unsigned
tcu_read(unsigned offset)
{
    return *tcu_reg(offset);
}

static void
tcu_write(unsigned offset, unsigned value)
{
    *tcu_reg(offset) = value;
}

static volatile unsigned *
intc_reg(unsigned irq, unsigned offset)
{
    return (volatile unsigned *)(CI20_INTC +
        (irq / 32) * INTC_CHIP_SIZE + offset);
}

static unsigned
intc_bit(unsigned irq)
{
    return 1u << (irq % 32);
}

static void
intc_unmask(unsigned irq)
{
    *intc_reg(irq, INTC_CLEAR_MASK) = intc_bit(irq);
}

void
clkstart(void)
{
    unsigned bit = 1u << CI20_TCU_CHANNEL;
    unsigned flags = TCU_FFLAG(CI20_TCU_CHANNEL) |
        TCU_HFLAG(CI20_TCU_CHANNEL);

    tcu_write(TCU_TSCR, bit);
    tcu_write(TCU_TECR, bit);
    tcu_write(TCU_TMSR, 0xffffffffu);
    tcu_write(TCU_TCSR(CI20_TCU_CHANNEL),
        TCU_TCSR_EXT_EN | TCU_TCSR_PRESCALE64);
    tcu_write(TCU_TDFR(CI20_TCU_CHANNEL), CI20_TCU_PERIOD);
    tcu_write(TCU_TDHR(CI20_TCU_CHANNEL), 0xffff);
    tcu_write(TCU_TCNT(CI20_TCU_CHANNEL), 0);
    tcu_write(TCU_TFCR, flags);
    tcu_write(TCU_TMSR, TCU_HFLAG(CI20_TCU_CHANNEL));
    tcu_write(TCU_TMCR, TCU_FFLAG(CI20_TCU_CHANNEL));
    tcu_write(TCU_TESR, bit);

    intc_unmask(CI20_TCU_IRQ);

    printf("ci20 clock: tcu%u irq %u rate %u hz period %u\n",
        CI20_TCU_CHANNEL, CI20_TCU_IRQ, CI20_TCU_RATE, CI20_TCU_PERIOD);
}

int
ci20_clock_intr(int *frame, unsigned status)
{
    unsigned bit = 1u << CI20_TCU_CHANNEL;
    unsigned start_count;
    unsigned elapsed;
    unsigned late;
    unsigned long late_us = 0;
    unsigned long clock_us;

    if ((tcu_read(TCU_TFR) & TCU_FFLAG(CI20_TCU_CHANNEL)) == 0)
        return 0;

    start_count = mips_read_c0_register(C0_COUNT, 0);
    if (ci20_clock_last_count_valid) {
        elapsed = start_count - ci20_clock_last_count;
        late = elapsed - CI20_COUNT_PERIOD;
        if ((int)late > 0)
            late_us = mips_timer_count_to_usec(late);
    }

    tcu_write(TCU_TECR, bit);
    tcu_write(TCU_TFCR, TCU_FFLAG(CI20_TCU_CHANNEL));
    tcu_write(TCU_TCNT(CI20_TCU_CHANNEL), 0);
    tcu_write(TCU_TESR, bit);

    mips_clock_intr(frame, status);
    clock_us = mips_timer_count_to_usec(
        mips_read_c0_register(C0_COUNT, 0) - start_count);
    mips_timer_record(late_us, clock_us);
    ci20_clock_last_count = start_count;
    ci20_clock_last_count_valid = 1;
    return 1;
}

int
mips_board_microtime(struct timeval *tv, u_int tick_usec)
{
    unsigned count;
    unsigned usec;

    count = tcu_read(TCU_TCNT(CI20_TCU_CHANNEL));
    usec = (count * 1000u) / (CI20_TCU_RATE / 1000u);
    if (usec >= tick_usec)
        usec = tick_usec - 1;

    tv->tv_usec += usec;
    if (tv->tv_usec >= 1000000L) {
        tv->tv_sec += tv->tv_usec / 1000000L;
        tv->tv_usec %= 1000000L;
    }
    return 1;
}
