/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * PC/AT CMOS attachment for the common MC146818 RTC driver.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/todr.h>
#include <rtc/mc146818.h>

#include "io.h"

#define I386_CMOS_INDEX     0x70u
#define I386_CMOS_DATA      0x71u
#define I386_CMOS_NMI_MASK  0x80u

static struct mc146818_softc i386_cmos;

static unsigned
i386_cmos_read(void *cookie, unsigned reg)
{
    unsigned value;

    (void)cookie;
    i386_outb(I386_CMOS_INDEX,
        (unsigned char)(I386_CMOS_NMI_MASK | (reg & 0x7fu)));
    i386_io_wait();
    value = i386_inb(I386_CMOS_DATA);
    i386_outb(I386_CMOS_INDEX, 0);
    return value;
}

static void
i386_cmos_write(void *cookie, unsigned reg, unsigned value)
{
    (void)cookie;
    i386_outb(I386_CMOS_INDEX,
        (unsigned char)(I386_CMOS_NMI_MASK | (reg & 0x7fu)));
    i386_io_wait();
    i386_outb(I386_CMOS_DATA, (unsigned char)value);
    i386_outb(I386_CMOS_INDEX, 0);
}

static const struct mc146818_io i386_cmos_io = {
    i386_cmos_read,
    i386_cmos_write,
};

void
i386_rtcattach(int unit)
{
    int error;

    (void)unit;
    error = mc146818_attach(&i386_cmos, "mc146818",
        &i386_cmos_io, 0, TODR_PRIORITY_PRIMARY);
    if (error != 0)
        printf("mc146818: attach failed, error=%d\n", error);
}
