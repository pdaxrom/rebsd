/*
 * Copyright (c) 2026 ReBSD contributors
 */

#ifndef _RTC_PCF8563_H_
#define _RTC_PCF8563_H_

#include <sys/i2c.h>
#include <sys/todr.h>

struct pcf8563_softc {
    struct todr_chip_handle sc_todr;
    struct i2c_adapter *sc_i2c;
    unsigned sc_address;
};

int pcf8563_attach(struct pcf8563_softc *, const char *,
    struct i2c_adapter *, unsigned, int);

#endif
