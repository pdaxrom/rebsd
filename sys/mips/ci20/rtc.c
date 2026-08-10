/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Creator Ci20 attachments for the common PCF8563 and JZ4780 RTC drivers.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/todr.h>
#include <rtc/jz4780.h>
#include <rtc/pcf8563.h>

#include "i2c4.h"

#define CI20_RTC_BASE       0xb0003000u
#define CI20_PCF8563_ADDR   0x51u

static struct pcf8563_softc ci20_pcf8563;
static struct jz4780_rtc_softc ci20_internal_rtc;

static unsigned
ci20_rtc_read(void *cookie, unsigned reg)
{
    (void)cookie;
    return *(volatile unsigned *)(CI20_RTC_BASE + reg);
}

static void
ci20_rtc_write(void *cookie, unsigned reg, unsigned value)
{
    (void)cookie;
    *(volatile unsigned *)(CI20_RTC_BASE + reg) = value;
}

static void
ci20_rtc_delay(void *cookie, unsigned usec)
{
    (void)cookie;
    udelay(usec);
}

static const struct jz4780_rtc_io ci20_rtc_io = {
    ci20_rtc_read,
    ci20_rtc_write,
    ci20_rtc_delay,
};

int
ci20_rtc_poweroff(void)
{
    return jz4780_rtc_poweroff(&ci20_internal_rtc);
}

void
ci20_rtcattach(int unit)
{
    struct i2c_adapter *i2c4;
    int error;

    (void)unit;
    i2c4 = ci20_i2c4_attach();
    if (i2c4 != 0) {
        error = pcf8563_attach(&ci20_pcf8563, "pcf8563",
            i2c4, CI20_PCF8563_ADDR, TODR_PRIORITY_PRIMARY);
        if (error != 0)
            printf("pcf8563: attach failed, error=%d\n", error);
    }
    error = jz4780_rtc_attach(&ci20_internal_rtc, "jz4780-rtc",
        &ci20_rtc_io, 0, TODR_PRIORITY_SECONDARY);
    if (error != 0)
        printf("jz4780-rtc: attach failed, error=%d\n", error);
}
