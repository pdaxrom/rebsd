/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _SYS_I2C_H_
#define _SYS_I2C_H_

#define I2C_M_RD            0x0001u

struct i2c_msg {
    unsigned short addr;
    unsigned short flags;
    unsigned len;
    unsigned char *buf;
};

struct i2c_adapter;

struct i2c_adapter_ops {
    int (*transfer)(struct i2c_adapter *, struct i2c_msg *, unsigned);
};

struct i2c_adapter {
    const char *name;
    const struct i2c_adapter_ops *ops;
    void *cookie;
};

int i2c_adapter_init(struct i2c_adapter *, const char *,
    const struct i2c_adapter_ops *, void *);
int i2c_transfer(struct i2c_adapter *, struct i2c_msg *, unsigned);
int i2c_write_read(struct i2c_adapter *, unsigned,
    const unsigned char *, unsigned, unsigned char *, unsigned);

#endif
