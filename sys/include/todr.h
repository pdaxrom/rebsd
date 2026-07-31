/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _SYS_TODR_H_
#define _SYS_TODR_H_

#include <sys/types.h>

struct clock_ymdhms {
    unsigned dt_year;
    unsigned dt_mon;
    unsigned dt_day;
    unsigned dt_wday;
    unsigned dt_hour;
    unsigned dt_min;
    unsigned dt_sec;
};

int clock_ymdhms_validate(const struct clock_ymdhms *);
int clock_ymdhms_to_secs(const struct clock_ymdhms *, time_t *);
int clock_secs_to_ymdhms(time_t, struct clock_ymdhms *);
int clock_bcd_to_bin(unsigned, unsigned *);
unsigned clock_bin_to_bcd(unsigned);

#ifdef KERNEL

struct todr_chip_handle {
    const char *todr_name;
    int todr_priority;
    void *todr_cookie;
    int (*todr_gettime_ymdhms)(struct todr_chip_handle *,
        struct clock_ymdhms *);
    int (*todr_settime_ymdhms)(struct todr_chip_handle *,
        const struct clock_ymdhms *);
    struct todr_chip_handle *todr_next;
};

#define TODR_PRIORITY_PRIMARY      200
#define TODR_PRIORITY_SECONDARY    100

int todr_attach(struct todr_chip_handle *);
void inittodr(time_t);
void resettodr(void);

#endif

#endif
