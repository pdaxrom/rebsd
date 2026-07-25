/*
 * BSD formatted error reporting interfaces.
 */
#ifndef _ERR_H_
#define _ERR_H_

#include <stdarg.h>

void err(int, const char *, ...)
    __attribute__((__format__(__printf__, 2, 3), __noreturn__));
void errx(int, const char *, ...)
    __attribute__((__format__(__printf__, 2, 3), __noreturn__));
void warn(const char *, ...)
    __attribute__((__format__(__printf__, 1, 2)));
void warnx(const char *, ...)
    __attribute__((__format__(__printf__, 1, 2)));

void verr(int, const char *, va_list)
    __attribute__((__format__(__printf__, 2, 0), __noreturn__));
void verrx(int, const char *, va_list)
    __attribute__((__format__(__printf__, 2, 0), __noreturn__));
void vwarn(const char *, va_list)
    __attribute__((__format__(__printf__, 1, 0)));
void vwarnx(const char *, va_list)
    __attribute__((__format__(__printf__, 1, 0)));

#endif
