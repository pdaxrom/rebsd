/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Machine-independent Ingenic JZ4780 time-of-day clock.
 */

#include <sys/errno.h>
#include <sys/todr.h>
#include <rtc/jz4780.h>

#define JZ_RTC_CTRL             0x00u
#define JZ_RTC_SECONDS          0x04u
#define JZ_RTC_SCRATCHPAD       0x34u
#define JZ_RTC_WENR             0x3cu
#define JZ_RTC_CTRL_WRDY        (1u << 7)
#define JZ_RTC_WENR_WEN         (1u << 31)
#define JZ_RTC_WENR_MAGIC       0xa55au
#define JZ_RTC_SCRATCH_MAGIC    0x12345678u
#define JZ_RTC_WAIT_US          1000u

static unsigned
jz_read(struct jz4780_rtc_softc *sc, unsigned reg)
{
    return (*sc->sc_io->read)(sc->sc_cookie, reg);
}

static void
jz_write_raw(struct jz4780_rtc_softc *sc, unsigned reg, unsigned value)
{
    (*sc->sc_io->write)(sc->sc_cookie, reg, value);
}

static int
jz_wait_mask(struct jz4780_rtc_softc *sc, unsigned reg, unsigned mask)
{
    unsigned elapsed;

    for (elapsed = 0; elapsed < JZ_RTC_WAIT_US; ++elapsed) {
        if ((jz_read(sc, reg) & mask) != 0)
            return 0;
        (*sc->sc_io->delay_us)(sc->sc_cookie, 1);
    }
    return ETIMEDOUT;
}

static int
jz_write(struct jz4780_rtc_softc *sc, unsigned reg, unsigned value)
{
    int error;

    error = jz_wait_mask(sc, JZ_RTC_CTRL, JZ_RTC_CTRL_WRDY);
    if (error != 0)
        return error;
    jz_write_raw(sc, JZ_RTC_WENR, JZ_RTC_WENR_MAGIC);
    error = jz_wait_mask(sc, JZ_RTC_WENR, JZ_RTC_WENR_WEN);
    if (error != 0)
        return error;
    error = jz_wait_mask(sc, JZ_RTC_CTRL, JZ_RTC_CTRL_WRDY);
    if (error != 0)
        return error;
    jz_write_raw(sc, reg, value);
    return 0;
}

static int
jz_gettime(struct todr_chip_handle *handle, struct clock_ymdhms *dt)
{
    struct jz4780_rtc_softc *sc;
    unsigned first;
    unsigned second;
    unsigned attempt;

    sc = (struct jz4780_rtc_softc *)handle->todr_cookie;
    if (jz_read(sc, JZ_RTC_SCRATCHPAD) != JZ_RTC_SCRATCH_MAGIC)
        return EINVAL;
    first = jz_read(sc, JZ_RTC_SECONDS);
    for (attempt = 0; attempt < 5u; ++attempt) {
        second = jz_read(sc, JZ_RTC_SECONDS);
        if (first == second)
            return clock_secs_to_ymdhms((time_t)first, dt);
        first = second;
    }
    return EIO;
}

static int
jz_settime(struct todr_chip_handle *handle,
    const struct clock_ymdhms *dt)
{
    struct jz4780_rtc_softc *sc;
    time_t seconds;
    int error;

    error = clock_ymdhms_to_secs(dt, &seconds);
    if (error != 0)
        return error;
    sc = (struct jz4780_rtc_softc *)handle->todr_cookie;
    error = jz_write(sc, JZ_RTC_SECONDS, (unsigned)seconds);
    if (error != 0)
        return error;
    return jz_write(sc, JZ_RTC_SCRATCHPAD, JZ_RTC_SCRATCH_MAGIC);
}

int
jz4780_rtc_attach(struct jz4780_rtc_softc *sc, const char *name,
    const struct jz4780_rtc_io *io, void *cookie, int priority)
{
    if (sc == 0 || name == 0 || io == 0 || io->read == 0 ||
        io->write == 0 || io->delay_us == 0)
        return EINVAL;
    sc->sc_io = io;
    sc->sc_cookie = cookie;
    sc->sc_todr.todr_name = name;
    sc->sc_todr.todr_priority = priority;
    sc->sc_todr.todr_cookie = sc;
    sc->sc_todr.todr_gettime_ymdhms = jz_gettime;
    sc->sc_todr.todr_settime_ymdhms = jz_settime;
    sc->sc_todr.todr_next = 0;
    return todr_attach(&sc->sc_todr);
}
