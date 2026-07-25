/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Architecture-independent I2C bus API.
 */

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/i2c.h>

int
i2c_adapter_init(struct i2c_adapter *adapter, const char *name,
    const struct i2c_adapter_ops *ops, void *cookie)
{
    if (adapter == 0 || name == 0 || ops == 0 || ops->transfer == 0)
        return EINVAL;
    adapter->name = name;
    adapter->ops = ops;
    adapter->cookie = cookie;
    return 0;
}

int
i2c_transfer(struct i2c_adapter *adapter, struct i2c_msg *messages,
    unsigned count)
{
    unsigned i;

    if (adapter == 0 || adapter->ops == 0 ||
        adapter->ops->transfer == 0 || messages == 0 || count == 0)
        return EINVAL;
    for (i = 0; i < count; ++i) {
        if (messages[i].addr > 0x7fu ||
            (messages[i].flags & ~I2C_M_RD) != 0 ||
            messages[i].len == 0 || messages[i].buf == 0)
            return EINVAL;
    }
    return (*adapter->ops->transfer)(adapter, messages, count);
}

int
i2c_write_read(struct i2c_adapter *adapter, unsigned address,
    const unsigned char *write_data, unsigned write_length,
    unsigned char *read_data, unsigned read_length)
{
    struct i2c_msg messages[2];

    if (address > 0x7fu || write_data == 0 || write_length == 0 ||
        read_data == 0 || read_length == 0)
        return EINVAL;
    messages[0].addr = address;
    messages[0].flags = 0;
    messages[0].len = write_length;
    messages[0].buf = (unsigned char *)write_data;
    messages[1].addr = address;
    messages[1].flags = I2C_M_RD;
    messages[1].len = read_length;
    messages[1].buf = read_data;
    return i2c_transfer(adapter, messages, 2);
}
