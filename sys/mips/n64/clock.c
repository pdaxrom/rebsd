#include <sys/param.h>
#include <machine/io.h>
#include <machine/n64.h>

/*
 * CP0 Count increments at half the VR4300 CPU clock.
 */
void
clkstart(void)
{
    unsigned count = mips_read_c0_register(C0_COUNT, 0);

    mips_write_c0_register(C0_COMPARE, 0,
        count + (N64_COUNT_KHZ * 1000u + HZ - 1) / HZ);
}
