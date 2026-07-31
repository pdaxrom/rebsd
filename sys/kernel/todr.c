/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Common BSD time-of-day clock selection and synchronization.
 */

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/todr.h>

static struct todr_chip_handle *todr_clocks;

int
todr_attach(struct todr_chip_handle *handle)
{
    struct todr_chip_handle **position;
    struct todr_chip_handle *clock;

    if (handle == 0 || handle->todr_name == 0 ||
        handle->todr_gettime_ymdhms == 0 || handle->todr_priority < 0)
        return EINVAL;
    for (clock = todr_clocks; clock != 0; clock = clock->todr_next) {
        if (clock == handle)
            return EEXIST;
    }
    position = &todr_clocks;
    while (*position != 0 &&
        (*position)->todr_priority >= handle->todr_priority)
        position = &(*position)->todr_next;
    handle->todr_next = *position;
    *position = handle;
    printf("todr: %s registered priority %d\n", handle->todr_name,
        handle->todr_priority);
    return 0;
}

void
inittodr(time_t base)
{
    struct todr_chip_handle *clock;
    struct clock_ymdhms dt;
    time_t seconds;
    int error;

    for (clock = todr_clocks; clock != 0; clock = clock->todr_next) {
        error = (*clock->todr_gettime_ymdhms)(clock, &dt);
        if (error != 0)
            continue;
        error = clock_ymdhms_to_secs(&dt, &seconds);
        if (error != 0)
            continue;
        time.tv_sec = seconds;
        time.tv_usec = 0;
        boottime = time;
        printf("todr: %s %u-%02u-%02u %02u:%02u:%02u UTC\n",
            clock->todr_name, dt.dt_year, dt.dt_mon, dt.dt_day,
            dt.dt_hour, dt.dt_min, dt.dt_sec);
        return;
    }
    if (base < 0)
        base = 0;
    time.tv_sec = base;
    time.tv_usec = 0;
    boottime = time;
    printf("todr: no valid hardware clock; using filesystem time\n");
}

void
resettodr(void)
{
    struct todr_chip_handle *clock;
    struct clock_ymdhms dt;
    int error;

    error = clock_secs_to_ymdhms(time.tv_sec, &dt);
    if (error != 0)
        return;
    for (clock = todr_clocks; clock != 0; clock = clock->todr_next) {
        if (clock->todr_settime_ymdhms == 0)
            continue;
        error = (*clock->todr_settime_ymdhms)(clock, &dt);
        if (error != 0)
            printf("todr: cannot set %s, error=%d\n",
                clock->todr_name, error);
    }
}
