/*
 * Copyright (c) 2026 ReBSD contributors
 */

#ifndef _RTC_MC146818_H_
#define _RTC_MC146818_H_

#include <sys/todr.h>

struct mc146818_io {
    unsigned (*read)(void *, unsigned);
    void (*write)(void *, unsigned, unsigned);
};

struct mc146818_softc {
    struct todr_chip_handle sc_todr;
    const struct mc146818_io *sc_io;
    void *sc_cookie;
};

int mc146818_attach(struct mc146818_softc *, const char *,
    const struct mc146818_io *, void *, int);

#endif
