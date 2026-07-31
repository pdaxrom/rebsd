/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Machine-independent MC146818-compatible time-of-day clock.
 */

#include <sys/errno.h>
#include <sys/todr.h>
#include <rtc/mc146818.h>

#define MC_SEC              0x00u
#define MC_MIN              0x02u
#define MC_HOUR             0x04u
#define MC_WDAY             0x06u
#define MC_MDAY             0x07u
#define MC_MONTH            0x08u
#define MC_YEAR             0x09u
#define MC_STATUS_A         0x0au
#define MC_STATUS_B         0x0bu

#define MC_STATUS_A_UIP     0x80u
#define MC_STATUS_B_24H     0x02u
#define MC_STATUS_B_BINARY  0x04u
#define MC_STATUS_B_SET     0x80u
#define MC_HOUR_PM          0x80u

#define MC_READ_RETRIES     16u
#define MC_UIP_RETRIES      100000u

static unsigned
mc_read(struct mc146818_softc *sc, unsigned reg)
{
    return (*sc->sc_io->read)(sc->sc_cookie, reg) & 0xffu;
}

static void
mc_write(struct mc146818_softc *sc, unsigned reg, unsigned value)
{
    (*sc->sc_io->write)(sc->sc_cookie, reg, value & 0xffu);
}

static int
mc_decode(unsigned raw, int binary, unsigned *value)
{
    if (binary) {
        *value = raw;
        return 0;
    }
    return clock_bcd_to_bin(raw, value);
}

static unsigned
mc_encode(unsigned value, int binary)
{
    return binary ? value : clock_bin_to_bcd(value);
}

static int
mc_wait_update(struct mc146818_softc *sc)
{
    unsigned count;

    for (count = 0; count < MC_UIP_RETRIES; ++count) {
        if ((mc_read(sc, MC_STATUS_A) & MC_STATUS_A_UIP) == 0)
            return 0;
    }
    return ETIMEDOUT;
}

static int
mc_gettime(struct todr_chip_handle *handle, struct clock_ymdhms *dt)
{
    struct mc146818_softc *sc;
    unsigned raw_sec;
    unsigned raw_hour;
    unsigned status;
    unsigned value;
    unsigned attempt;
    int binary;
    int error;

    sc = (struct mc146818_softc *)handle->todr_cookie;
    for (attempt = 0; attempt < MC_READ_RETRIES; ++attempt) {
        error = mc_wait_update(sc);
        if (error != 0)
            return error;
        status = mc_read(sc, MC_STATUS_B);
        binary = (status & MC_STATUS_B_BINARY) != 0;
        raw_sec = mc_read(sc, MC_SEC);
        error = mc_decode(raw_sec, binary, &dt->dt_sec);
        if (error != 0)
            continue;
        if (mc_decode(mc_read(sc, MC_MIN), binary, &dt->dt_min) != 0)
            continue;
        raw_hour = mc_read(sc, MC_HOUR);
        if (mc_decode(raw_hour & ~MC_HOUR_PM, binary, &dt->dt_hour) != 0)
            continue;
        if ((status & MC_STATUS_B_24H) == 0) {
            if (dt->dt_hour == 12u)
                dt->dt_hour = 0;
            if ((raw_hour & MC_HOUR_PM) != 0)
                dt->dt_hour += 12u;
        }
        if (mc_decode(mc_read(sc, MC_MDAY), binary, &dt->dt_day) != 0 ||
            mc_decode(mc_read(sc, MC_MONTH), binary, &dt->dt_mon) != 0 ||
            mc_decode(mc_read(sc, MC_YEAR), binary, &value) != 0)
            continue;
        dt->dt_year = 1900u + value;
        if (dt->dt_year < 1970u)
            dt->dt_year += 100u;
        value = mc_read(sc, MC_WDAY);
        if (!binary && clock_bcd_to_bin(value, &value) != 0)
            continue;
        dt->dt_wday = value >= 1u && value <= 7u ? value - 1u : 0u;
        if ((mc_read(sc, MC_STATUS_A) & MC_STATUS_A_UIP) == 0 &&
            mc_read(sc, MC_SEC) == raw_sec)
            return clock_ymdhms_validate(dt);
    }
    return EIO;
}

static int
mc_settime(struct todr_chip_handle *handle, const struct clock_ymdhms *dt)
{
    struct mc146818_softc *sc;
    unsigned hour;
    unsigned status;
    unsigned year;
    int binary;
    int error;

    error = clock_ymdhms_validate(dt);
    if (error != 0)
        return error;
    sc = (struct mc146818_softc *)handle->todr_cookie;
    error = mc_wait_update(sc);
    if (error != 0)
        return error;
    status = mc_read(sc, MC_STATUS_B);
    binary = (status & MC_STATUS_B_BINARY) != 0;
    mc_write(sc, MC_STATUS_B, status | MC_STATUS_B_SET);
    mc_write(sc, MC_SEC, mc_encode(dt->dt_sec, binary));
    mc_write(sc, MC_MIN, mc_encode(dt->dt_min, binary));
    hour = dt->dt_hour;
    if ((status & MC_STATUS_B_24H) == 0) {
        if (hour >= 12u) {
            hour -= 12u;
            hour |= MC_HOUR_PM;
        }
        if ((hour & ~MC_HOUR_PM) == 0)
            hour |= 12u;
        mc_write(sc, MC_HOUR,
            mc_encode(hour & ~MC_HOUR_PM, binary) |
            (hour & MC_HOUR_PM));
    } else
        mc_write(sc, MC_HOUR, mc_encode(hour, binary));
    mc_write(sc, MC_WDAY, mc_encode(dt->dt_wday + 1u, binary));
    mc_write(sc, MC_MDAY, mc_encode(dt->dt_day, binary));
    mc_write(sc, MC_MONTH, mc_encode(dt->dt_mon, binary));
    year = dt->dt_year % 100u;
    mc_write(sc, MC_YEAR, mc_encode(year, binary));
    mc_write(sc, MC_STATUS_B, status);
    return 0;
}

int
mc146818_attach(struct mc146818_softc *sc, const char *name,
    const struct mc146818_io *io, void *cookie, int priority)
{
    if (sc == 0 || name == 0 || io == 0 || io->read == 0 ||
        io->write == 0)
        return EINVAL;
    sc->sc_io = io;
    sc->sc_cookie = cookie;
    sc->sc_todr.todr_name = name;
    sc->sc_todr.todr_priority = priority;
    sc->sc_todr.todr_cookie = sc;
    sc->sc_todr.todr_gettime_ymdhms = mc_gettime;
    sc->sc_todr.todr_settime_ymdhms = mc_settime;
    sc->sc_todr.todr_next = 0;
    return todr_attach(&sc->sc_todr);
}
