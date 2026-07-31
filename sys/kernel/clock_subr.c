/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Machine-independent Gregorian calendar conversion.
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/todr.h>

#define CLOCK_SECONDS_PER_DAY   86400ul
#define CLOCK_TIME_MAX          0x7ffffffful

static int
clock_leap_year(unsigned year)
{
    return (year % 4u) == 0u &&
        ((year % 100u) != 0u || (year % 400u) == 0u);
}

static unsigned
clock_month_days(unsigned year, unsigned month)
{
    static const unsigned char days[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };

    if (month == 0u || month > 12u)
        return 0;
    if (month == 2u && clock_leap_year(year))
        return 29;
    return days[month - 1u];
}

int
clock_ymdhms_validate(const struct clock_ymdhms *dt)
{
    unsigned limit;

    if (dt == 0 || dt->dt_year < 1970u || dt->dt_mon == 0u ||
        dt->dt_mon > 12u || dt->dt_day == 0u || dt->dt_hour > 23u ||
        dt->dt_min > 59u || dt->dt_sec > 59u || dt->dt_wday > 6u)
        return EINVAL;
    limit = clock_month_days(dt->dt_year, dt->dt_mon);
    if (dt->dt_day > limit)
        return EINVAL;
    return 0;
}

int
clock_ymdhms_to_secs(const struct clock_ymdhms *dt, time_t *result)
{
    unsigned long days;
    unsigned long seconds;
    unsigned year;
    unsigned month;
    int error;

    if (result == 0)
        return EINVAL;
    error = clock_ymdhms_validate(dt);
    if (error != 0)
        return error;

    days = 0;
    for (year = 1970u; year < dt->dt_year; ++year)
        days += clock_leap_year(year) ? 366u : 365u;
    for (month = 1u; month < dt->dt_mon; ++month)
        days += clock_month_days(dt->dt_year, month);
    days += dt->dt_day - 1u;
    seconds = (unsigned long)dt->dt_hour * 3600ul +
        (unsigned long)dt->dt_min * 60ul + dt->dt_sec;
    if (days > CLOCK_TIME_MAX / CLOCK_SECONDS_PER_DAY ||
        (days == CLOCK_TIME_MAX / CLOCK_SECONDS_PER_DAY &&
        seconds > CLOCK_TIME_MAX % CLOCK_SECONDS_PER_DAY))
        return EOVERFLOW;
    *result = (time_t)(days * CLOCK_SECONDS_PER_DAY + seconds);
    return 0;
}

int
clock_secs_to_ymdhms(time_t value, struct clock_ymdhms *dt)
{
    unsigned long days;
    unsigned long seconds;
    unsigned limit;
    unsigned year;
    unsigned month;

    if (dt == 0 || value < 0 || (unsigned long)value > CLOCK_TIME_MAX)
        return EINVAL;
    days = (unsigned long)value / CLOCK_SECONDS_PER_DAY;
    seconds = (unsigned long)value % CLOCK_SECONDS_PER_DAY;
    dt->dt_wday = (unsigned)((days + 4u) % 7u);

    year = 1970u;
    for (;;) {
        limit = clock_leap_year(year) ? 366u : 365u;
        if (days < limit)
            break;
        days -= limit;
        ++year;
    }
    month = 1u;
    for (;;) {
        limit = clock_month_days(year, month);
        if (days < limit)
            break;
        days -= limit;
        ++month;
    }
    dt->dt_year = year;
    dt->dt_mon = month;
    dt->dt_day = (unsigned)days + 1u;
    dt->dt_hour = (unsigned)(seconds / 3600ul);
    seconds %= 3600ul;
    dt->dt_min = (unsigned)(seconds / 60ul);
    dt->dt_sec = (unsigned)(seconds % 60ul);
    return 0;
}

int
clock_bcd_to_bin(unsigned value, unsigned *result)
{
    unsigned low;
    unsigned high;

    if (result == 0 || value > 0xffu)
        return EINVAL;
    low = value & 0x0fu;
    high = (value >> 4) & 0x0fu;
    if (low > 9u || high > 9u)
        return EINVAL;
    *result = high * 10u + low;
    return 0;
}

unsigned
clock_bin_to_bcd(unsigned value)
{
    return ((value / 10u) << 4) | (value % 10u);
}
