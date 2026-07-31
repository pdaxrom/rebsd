/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Polling JZ4780 I2C4 adapter for the common I2C bus API.
 */

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/i2c.h>
#include <sys/systm.h>

#include "cgu.h"
#include "i2c4.h"

#define CI20_I2C4_BASE         0xb0054000u
#define CI20_GPIO_BASE         0xb0010000u
#define CI20_GPIO_PE           4u
#define CI20_I2C4_PIN_MASK     ((1u << 12) | (1u << 13))

#define GPIO_PXINTC(n)         (0x18u + (n) * 0x100u)
#define GPIO_PXMASKC(n)        (0x28u + (n) * 0x100u)
#define GPIO_PXPAT1C(n)        (0x38u + (n) * 0x100u)
#define GPIO_PXPAT0S(n)        (0x44u + (n) * 0x100u)
#define GPIO_PXPENS(n)         (0x74u + (n) * 0x100u)

#define I2C_CTRL               0x00u
#define I2C_TAR                0x04u
#define I2C_DC                 0x10u
#define I2C_FHCNT              0x1cu
#define I2C_FLCNT              0x20u
#define I2C_INTM               0x30u
#define I2C_CINTR              0x40u
#define I2C_CTXABRT            0x54u
#define I2C_ENB                0x6cu
#define I2C_STA                0x70u
#define I2C_TXABRT             0x80u
#define I2C_SDASU              0x94u
#define I2C_ENSTA              0x9cu
#define I2C_SDAHD              0xd0u

#define I2C_CTRL_STPHLD        (1u << 7)
#define I2C_CTRL_SLVDIS        (1u << 6)
#define I2C_CTRL_RESTART       (1u << 5)
#define I2C_CTRL_FAST          (1u << 2)
#define I2C_CTRL_MASTER        (1u << 0)
#define I2C_DC_READ            (1u << 8)
#define I2C_STA_MSTACT         (1u << 5)
#define I2C_STA_RFNE           (1u << 3)
#define I2C_STA_TFE            (1u << 2)
#define I2C_STA_TFNF           (1u << 1)
#define I2C_ENB_ENABLE         (1u << 0)
#define I2C_SDAHD_ENABLE       (1u << 8)

#define CI20_I2C4_GATE         (1u << 12)
#define CI20_I2C_SPEED_KHZ     400u
#define CI20_I2C_FIFO          16u
#define CI20_I2C_TIMEOUT_US    100000u

struct ci20_i2c4_softc {
    struct i2c_adapter adapter;
    unsigned pclk_khz;
};

static struct ci20_i2c4_softc ci20_i2c4;

static volatile unsigned *
ci20_gpio_reg(unsigned reg)
{
    return (volatile unsigned *)(CI20_GPIO_BASE + reg);
}

static volatile unsigned short *
ci20_i2c_reg(unsigned reg)
{
    return (volatile unsigned short *)(CI20_I2C4_BASE + reg);
}

static unsigned
ci20_i2c_read(unsigned reg)
{
    return *ci20_i2c_reg(reg);
}

static void
ci20_i2c_write(unsigned reg, unsigned value)
{
    *ci20_i2c_reg(reg) = (unsigned short)value;
}

static void
ci20_i2c_pinmux(void)
{
    *ci20_gpio_reg(GPIO_PXINTC(CI20_GPIO_PE)) = CI20_I2C4_PIN_MASK;
    *ci20_gpio_reg(GPIO_PXMASKC(CI20_GPIO_PE)) = CI20_I2C4_PIN_MASK;
    *ci20_gpio_reg(GPIO_PXPAT1C(CI20_GPIO_PE)) = CI20_I2C4_PIN_MASK;
    *ci20_gpio_reg(GPIO_PXPAT0S(CI20_GPIO_PE)) = CI20_I2C4_PIN_MASK;
    *ci20_gpio_reg(GPIO_PXPENS(CI20_GPIO_PE)) = CI20_I2C4_PIN_MASK;
}

static int
ci20_i2c_wait(unsigned reg, unsigned mask, unsigned expected,
    int check_abort)
{
    unsigned elapsed;

    for (elapsed = 0; elapsed < CI20_I2C_TIMEOUT_US; ++elapsed) {
        if ((ci20_i2c_read(reg) & mask) == expected)
            return 0;
        if (check_abort && ci20_i2c_read(I2C_TXABRT) != 0)
            return EIO;
        udelay(1);
    }
    return ETIMEDOUT;
}

static int
ci20_i2c_disable(void)
{
    ci20_i2c_write(I2C_ENB, 0);
    return ci20_i2c_wait(I2C_ENSTA, I2C_ENB_ENABLE, 0, 0);
}

static int
ci20_i2c_enable(void)
{
    ci20_i2c_write(I2C_ENB, I2C_ENB_ENABLE);
    return ci20_i2c_wait(I2C_ENSTA, I2C_ENB_ENABLE,
        I2C_ENB_ENABLE, 0);
}

static void
ci20_i2c_stop_hold(int hold)
{
    unsigned value;

    value = ci20_i2c_read(I2C_CTRL);
    if (hold)
        value |= I2C_CTRL_STPHLD;
    else
        value &= ~I2C_CTRL_STPHLD;
    ci20_i2c_write(I2C_CTRL, value);
}

static int
ci20_i2c_check_abort(void)
{
    unsigned abort;

    abort = ci20_i2c_read(I2C_TXABRT);
    if (abort == 0)
        return 0;
    (void)ci20_i2c_read(I2C_CTXABRT);
    return EIO;
}

static int
ci20_i2c_write_message(const struct i2c_msg *message, int last)
{
    unsigned i;
    int error;

    for (i = 0; i < message->len; ++i) {
        error = ci20_i2c_wait(I2C_STA, I2C_STA_TFNF,
            I2C_STA_TFNF, 1);
        if (error != 0)
            return error;
        ci20_i2c_write(I2C_DC, message->buf[i]);
    }
    if (last)
        ci20_i2c_stop_hold(0);
    error = ci20_i2c_wait(I2C_STA, I2C_STA_TFE, I2C_STA_TFE, 1);
    if (error != 0)
        return error;
    if (last)
        error = ci20_i2c_wait(I2C_STA, I2C_STA_MSTACT, 0, 1);
    return error;
}

static int
ci20_i2c_read_message(struct i2c_msg *message, int last)
{
    unsigned elapsed;
    unsigned queued;
    unsigned received;
    unsigned status;
    int progress;
    int error;

    elapsed = 0;
    queued = 0;
    received = 0;
    while (received < message->len) {
        progress = 0;
        status = ci20_i2c_read(I2C_STA);
        while (queued < message->len &&
            queued - received < CI20_I2C_FIFO - 1u &&
            (status & I2C_STA_TFNF) != 0) {
            if (last && queued + 1u == message->len)
                ci20_i2c_stop_hold(0);
            ci20_i2c_write(I2C_DC, I2C_DC_READ);
            ++queued;
            progress = 1;
            status = ci20_i2c_read(I2C_STA);
        }
        while (received < message->len &&
            (ci20_i2c_read(I2C_STA) & I2C_STA_RFNE) != 0) {
            message->buf[received++] = (unsigned char)ci20_i2c_read(I2C_DC);
            progress = 1;
        }
        error = ci20_i2c_check_abort();
        if (error != 0)
            return error;
        if (progress) {
            elapsed = 0;
            continue;
        }
        if (++elapsed >= CI20_I2C_TIMEOUT_US)
            return ETIMEDOUT;
        udelay(1);
    }
    if (last)
        return ci20_i2c_wait(I2C_STA, I2C_STA_MSTACT, 0, 1);
    return 0;
}

static int
ci20_i2c_transfer(struct i2c_adapter *adapter,
    struct i2c_msg *messages, unsigned count)
{
    unsigned i;
    int error;

    (void)adapter;
    for (i = 1; i < count; ++i) {
        if (messages[i].addr != messages[0].addr)
            return EOPNOTSUPP;
    }
    error = ci20_i2c_disable();
    if (error != 0)
        return error;
    (void)ci20_i2c_read(I2C_CTXABRT);
    (void)ci20_i2c_read(I2C_CINTR);
    ci20_i2c_write(I2C_TAR, messages[0].addr);
    ci20_i2c_stop_hold(1);
    error = ci20_i2c_enable();
    if (error != 0)
        return error;
    for (i = 0; i < count; ++i) {
        if ((messages[i].flags & I2C_M_RD) != 0)
            error = ci20_i2c_read_message(&messages[i], i + 1u == count);
        else
            error = ci20_i2c_write_message(&messages[i], i + 1u == count);
        if (error != 0)
            break;
    }
    if (error != 0)
        ci20_i2c_stop_hold(0);
    if (ci20_i2c_disable() != 0 && error == 0)
        error = EIO;
    if (error != 0)
        printf("i2c4: transfer address=%x error=%d abort=%x\n",
            messages[0].addr, error, ci20_i2c_read(I2C_TXABRT));
    return error;
}

static const struct i2c_adapter_ops ci20_i2c_ops = {
    ci20_i2c_transfer,
};

static int
ci20_i2c_configure(struct ci20_i2c4_softc *sc)
{
    unsigned count_high;
    unsigned count_low;
    unsigned period;
    unsigned setup;
    unsigned hold;
    int error;

    error = ci20_i2c_disable();
    if (error != 0)
        return error;
    period = sc->pclk_khz / CI20_I2C_SPEED_KHZ;
    count_high = period * 600u / (1300u + 600u);
    count_low = period - count_high;
    count_high = count_high > 8u ? count_high - 8u : 6u;
    if (count_high < 6u)
        count_high = 6u;
    count_low = count_low > 1u ? count_low - 1u : 8u;
    if (count_low < 8u)
        count_low = 8u;
    setup = (450u * sc->pclk_khz) / 1000000u + 1u;
    hold = (450u * sc->pclk_khz) / 1000000u;
    if (hold != 0)
        --hold;
    if (setup > 255u)
        setup = 255u;
    if (hold > 255u)
        hold = 255u;
    ci20_i2c_write(I2C_CTRL, I2C_CTRL_FAST | I2C_CTRL_RESTART |
        I2C_CTRL_SLVDIS | I2C_CTRL_MASTER);
    ci20_i2c_write(I2C_FHCNT, count_high);
    ci20_i2c_write(I2C_FLCNT, count_low);
    ci20_i2c_write(I2C_SDASU, setup);
    ci20_i2c_write(I2C_SDAHD, I2C_SDAHD_ENABLE | hold);
    ci20_i2c_write(I2C_INTM, 0);
    (void)ci20_i2c_read(I2C_CINTR);
    return 0;
}

struct i2c_adapter *
ci20_i2c4_attach(void)
{
    int error;

    ci20_i2c_pinmux();
    ci20_cgu_gate_enable(CI20_CGU_CLKGR1, CI20_I2C4_GATE);
    ci20_i2c4.pclk_khz = ci20_cgu_pclk_rate_khz();
    if (ci20_i2c4.pclk_khz < CI20_I2C_SPEED_KHZ * 16u) {
        printf("i2c4: invalid pclk %u kHz\n", ci20_i2c4.pclk_khz);
        return 0;
    }
    error = ci20_i2c_configure(&ci20_i2c4);
    if (error != 0) {
        printf("i2c4: configure failed, error=%d\n", error);
        return 0;
    }
    error = i2c_adapter_init(&ci20_i2c4.adapter, "jz4780-i2c4",
        &ci20_i2c_ops, &ci20_i2c4);
    if (error != 0)
        return 0;
    printf("i2c4: pclk %u kHz bus %u kHz\n",
        ci20_i2c4.pclk_khz, CI20_I2C_SPEED_KHZ);
    return &ci20_i2c4.adapter;
}
