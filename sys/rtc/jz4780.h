/*
 * Copyright (c) 2026 ReBSD contributors
 */

#ifndef _RTC_JZ4780_H_
#define _RTC_JZ4780_H_

#include <sys/todr.h>

struct jz4780_rtc_io {
    unsigned (*read)(void *, unsigned);
    void (*write)(void *, unsigned, unsigned);
    void (*delay_us)(void *, unsigned);
};

struct jz4780_rtc_softc {
    struct todr_chip_handle sc_todr;
    const struct jz4780_rtc_io *sc_io;
    void *sc_cookie;
};

int jz4780_rtc_attach(struct jz4780_rtc_softc *, const char *,
    const struct jz4780_rtc_io *, void *, int);
int jz4780_rtc_poweroff(struct jz4780_rtc_softc *);

#endif
