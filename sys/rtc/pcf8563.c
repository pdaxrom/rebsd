/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Machine-independent NXP PCF8563 time-of-day clock.
 */

#include <sys/errno.h>
#include <sys/i2c.h>
#include <sys/todr.h>
#include <rtc/pcf8563.h>

#define PCF8563_REG_CONTROL1    0x00u
#define PCF8563_REG_SECONDS     0x02u
#define PCF8563_SECONDS_VL      0x80u
#define PCF8563_MONTH_CENTURY   0x80u
#define PCF8563_TIME_REGS       7u

static int
pcf8563_read(struct pcf8563_softc *sc, unsigned reg,
    unsigned char *data, unsigned length)
{
    unsigned char address;

    address = (unsigned char)reg;
    return i2c_write_read(sc->sc_i2c, sc->sc_address,
        &address, 1, data, length);
}

static int
pcf8563_write(struct pcf8563_softc *sc, unsigned reg,
    const unsigned char *data, unsigned length)
{
    unsigned char buffer[PCF8563_TIME_REGS + 1u];
    struct i2c_msg message;
    unsigned i;

    if (length > PCF8563_TIME_REGS)
        return EINVAL;
    buffer[0] = (unsigned char)reg;
    for (i = 0; i < length; ++i)
        buffer[i + 1u] = data[i];
    message.addr = (unsigned short)sc->sc_address;
    message.flags = 0;
    message.len = length + 1u;
    message.buf = buffer;
    return i2c_transfer(sc->sc_i2c, &message, 1);
}

static int
pcf8563_gettime(struct todr_chip_handle *handle, struct clock_ymdhms *dt)
{
    struct pcf8563_softc *sc;
    unsigned char data[PCF8563_TIME_REGS];
    unsigned value;
    int error;

    sc = (struct pcf8563_softc *)handle->todr_cookie;
    error = pcf8563_read(sc, PCF8563_REG_SECONDS, data, sizeof(data));
    if (error != 0)
        return error;
    if ((data[0] & PCF8563_SECONDS_VL) != 0)
        return EIO;
    if (clock_bcd_to_bin(data[0] & 0x7fu, &dt->dt_sec) != 0 ||
        clock_bcd_to_bin(data[1] & 0x7fu, &dt->dt_min) != 0 ||
        clock_bcd_to_bin(data[2] & 0x3fu, &dt->dt_hour) != 0 ||
        clock_bcd_to_bin(data[3] & 0x3fu, &dt->dt_day) != 0 ||
        clock_bcd_to_bin(data[5] & 0x1fu, &dt->dt_mon) != 0 ||
        clock_bcd_to_bin(data[6], &value) != 0)
        return EINVAL;
    dt->dt_wday = data[4] & 0x07u;
    dt->dt_year = 1900u + value;
    if ((data[5] & PCF8563_MONTH_CENTURY) == 0)
        dt->dt_year += 100u;
    return clock_ymdhms_validate(dt);
}

static int
pcf8563_settime(struct todr_chip_handle *handle,
    const struct clock_ymdhms *dt)
{
    struct pcf8563_softc *sc;
    unsigned char data[PCF8563_TIME_REGS];
    int error;

    error = clock_ymdhms_validate(dt);
    if (error != 0)
        return error;
    if (dt->dt_year > 2099u)
        return EOVERFLOW;
    sc = (struct pcf8563_softc *)handle->todr_cookie;
    data[0] = (unsigned char)clock_bin_to_bcd(dt->dt_sec);
    data[1] = (unsigned char)clock_bin_to_bcd(dt->dt_min);
    data[2] = (unsigned char)clock_bin_to_bcd(dt->dt_hour);
    data[3] = (unsigned char)clock_bin_to_bcd(dt->dt_day);
    data[4] = (unsigned char)dt->dt_wday;
    data[5] = (unsigned char)clock_bin_to_bcd(dt->dt_mon);
    if (dt->dt_year < 2000u)
        data[5] |= PCF8563_MONTH_CENTURY;
    data[6] = (unsigned char)clock_bin_to_bcd(dt->dt_year % 100u);
    return pcf8563_write(sc, PCF8563_REG_SECONDS, data, sizeof(data));
}

int
pcf8563_attach(struct pcf8563_softc *sc, const char *name,
    struct i2c_adapter *adapter, unsigned address, int priority)
{
    unsigned char controls[2];
    int error;

    if (sc == 0 || name == 0 || adapter == 0 || address > 0x7fu)
        return EINVAL;
    sc->sc_i2c = adapter;
    sc->sc_address = address;
    controls[0] = 0;
    controls[1] = 0;
    error = pcf8563_write(sc, PCF8563_REG_CONTROL1,
        controls, sizeof(controls));
    if (error != 0)
        return error;
    sc->sc_todr.todr_name = name;
    sc->sc_todr.todr_priority = priority;
    sc->sc_todr.todr_cookie = sc;
    sc->sc_todr.todr_gettime_ymdhms = pcf8563_gettime;
    sc->sc_todr.todr_settime_ymdhms = pcf8563_settime;
    sc->sc_todr.todr_next = 0;
    return todr_attach(&sc->sc_todr);
}
