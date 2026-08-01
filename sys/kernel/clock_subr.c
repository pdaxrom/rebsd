/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Machine-independent Gregorian calendar conversion.
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/todr.h>

#define CLOCK_SECONDS_PER_DAY   86400ll
#define CLOCK_EPOCH_ADJUSTMENT  719468ll

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

static long long
clock_days_from_civil(unsigned year, unsigned month, unsigned day)
{
    long long adjusted_year;
    long long era;
    unsigned year_of_era;
    unsigned day_of_year;
    unsigned day_of_era;

    adjusted_year = year;
    if (month <= 2u)
        --adjusted_year;
    era = adjusted_year / 400ll;
    year_of_era = (unsigned)(adjusted_year - era * 400ll);
    month = month > 2u ? month - 3u : month + 9u;
    day_of_year = (153u * month + 2u) /
        5u + day - 1u;
    day_of_era = year_of_era * 365u + year_of_era / 4u -
        year_of_era / 100u + day_of_year;
    return era * 146097ll + day_of_era - CLOCK_EPOCH_ADJUSTMENT;
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
    long long days;
    long long seconds;
    int error;

    if (result == 0)
        return EINVAL;
    error = clock_ymdhms_validate(dt);
    if (error != 0)
        return error;

    days = clock_days_from_civil(dt->dt_year, dt->dt_mon, dt->dt_day);
    seconds = days * CLOCK_SECONDS_PER_DAY +
        (long long)dt->dt_hour * 3600ll +
        (long long)dt->dt_min * 60ll + dt->dt_sec;
    *result = (time_t)seconds;
    return 0;
}

int
clock_secs_to_ymdhms(time_t value, struct clock_ymdhms *dt)
{
    long long days;
    long long era;
    long long year;
    unsigned day_of_era;
    unsigned year_of_era;
    unsigned day_of_year;
    unsigned month_prime;
    unsigned seconds;
    unsigned month;

    if (dt == 0 || value < 0)
        return EINVAL;
    days = value / CLOCK_SECONDS_PER_DAY;
    seconds = (unsigned)(value % CLOCK_SECONDS_PER_DAY);
    dt->dt_wday = (unsigned)((days + 4ll) % 7ll);

    days += CLOCK_EPOCH_ADJUSTMENT;
    era = days / 146097ll;
    day_of_era = (unsigned)(days - era * 146097ll);
    year_of_era = (day_of_era - day_of_era / 1460u +
        day_of_era / 36524u - day_of_era / 146096u) / 365u;
    year = (long long)year_of_era + era * 400ll;
    day_of_year = day_of_era - (365u * year_of_era +
        year_of_era / 4u - year_of_era / 100u);
    month_prime = (5u * day_of_year + 2u) / 153u;
    month = month_prime < 10u ? month_prime + 3u : month_prime - 9u;
    year += month <= 2u;
    if (year < 1970ll || (unsigned long long)year > 0xffffffffull)
        return EOVERFLOW;
    dt->dt_year = (unsigned)year;
    dt->dt_mon = month;
    dt->dt_day = day_of_year - (153u * month_prime + 2u) / 5u + 1u;
    dt->dt_hour = seconds / 3600u;
    seconds %= 3600u;
    dt->dt_min = seconds / 60u;
    dt->dt_sec = seconds % 60u;
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
