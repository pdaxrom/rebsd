/*
 * Copyright (c) 1983, 1987 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#ifndef _TIME_H
#define _TIME_H

#ifndef NULL
#define NULL    0
#endif

#ifndef _TIME_T
#define _TIME_T
typedef long long time_t;
#endif

#include <stddef.h>

/*
 * Structure returned by gmtime and localtime calls (see ctime(3)).
 */
struct tm {
    int     tm_sec;
    int     tm_min;
    int     tm_hour;
    int     tm_mday;
    int     tm_mon;
    int     tm_year;
    int     tm_wday;
    int     tm_yday;
    int     tm_isdst;
    long    tm_gmtoff;
    char    *tm_zone;
};

struct tm *gmtime(const time_t *);
struct tm *localtime(const time_t *);
char *asctime(const struct tm *);
char *ctime(const time_t *);
struct tm *gmtime_r(const time_t *, struct tm *);
struct tm *localtime_r(const time_t *, struct tm *);
char *asctime_r(const struct tm *, char *);
char *ctime_r(const time_t *, char *);
time_t time(time_t *);
time_t mktime(struct tm *);
time_t timegm(struct tm *);
double difftime(time_t, time_t);
void tzset(void);

extern char *tzname[2];

size_t strftime (char *s, size_t maxsize, const char *format,
    const struct tm *timeptr);

#endif
