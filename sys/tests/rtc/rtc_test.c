#include <stdio.h>
#include <string.h>

#include <sys/errno.h>
#include <sys/i2c.h>
#include <sys/todr.h>
#include <rtc/jz4780.h>
#include <rtc/mc146818.h>
#include <rtc/pcf8563.h>

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "rtc_test: check failed at line %d: %s\n", \
            __LINE__, #expr); \
        return 1; \
    } \
} while (0)

static struct todr_chip_handle *attached;

int
todr_attach(struct todr_chip_handle *handle)
{
    attached = handle;
    return 0;
}

static int
test_calendar(void)
{
    struct clock_ymdhms dt;
    struct clock_ymdhms back;
    time_t seconds;
    unsigned value;

    CHECK(sizeof(time_t) == 8);

    memset(&dt, 0, sizeof(dt));
    dt.dt_year = 1970;
    dt.dt_mon = 1;
    dt.dt_day = 1;
    dt.dt_wday = 4;
    CHECK(clock_ymdhms_to_secs(&dt, &seconds) == 0 && seconds == 0);
    CHECK(clock_secs_to_ymdhms(0, &back) == 0);
    CHECK(back.dt_year == 1970 && back.dt_mon == 1 && back.dt_day == 1 &&
        back.dt_wday == 4);

    dt.dt_year = 2000;
    dt.dt_mon = 2;
    dt.dt_day = 29;
    dt.dt_wday = 2;
    dt.dt_hour = 12;
    dt.dt_min = 34;
    dt.dt_sec = 56;
    CHECK(clock_ymdhms_to_secs(&dt, &seconds) == 0);
    CHECK(clock_secs_to_ymdhms(seconds, &back) == 0);
    CHECK(memcmp(&dt, &back, sizeof(dt)) == 0);

    dt.dt_year = 2038;
    dt.dt_mon = 1;
    dt.dt_day = 19;
    dt.dt_wday = 2;
    dt.dt_hour = 3;
    dt.dt_min = 14;
    dt.dt_sec = 7;
    CHECK(clock_ymdhms_to_secs(&dt, &seconds) == 0 &&
        (unsigned long long)seconds == 0x7fffffffull);
    ++dt.dt_sec;
    CHECK(clock_ymdhms_to_secs(&dt, &seconds) == 0 &&
        (unsigned long long)seconds == 0x80000000ull);
    CHECK(clock_secs_to_ymdhms(seconds, &back) == 0 &&
        back.dt_year == 2038 && back.dt_mon == 1 && back.dt_day == 19 &&
        back.dt_hour == 3 && back.dt_min == 14 && back.dt_sec == 8);

    dt.dt_year = 2100;
    dt.dt_mon = 3;
    dt.dt_day = 1;
    dt.dt_wday = 1;
    dt.dt_hour = 0;
    dt.dt_min = 0;
    dt.dt_sec = 0;
    CHECK(clock_ymdhms_to_secs(&dt, &seconds) == 0);
    CHECK(clock_secs_to_ymdhms(seconds, &back) == 0);
    CHECK(memcmp(&dt, &back, sizeof(dt)) == 0);
    CHECK(clock_bcd_to_bin(0x59, &value) == 0 && value == 59);
    CHECK(clock_bcd_to_bin(0x6a, &value) == EINVAL);
    CHECK(clock_bin_to_bcd(42) == 0x42);
    return 0;
}

struct mc_fake {
    unsigned char regs[128];
};

static unsigned
mc_fake_read(void *cookie, unsigned reg)
{
    struct mc_fake *fake = cookie;
    return fake->regs[reg & 0x7f];
}

static void
mc_fake_write(void *cookie, unsigned reg, unsigned value)
{
    struct mc_fake *fake = cookie;
    fake->regs[reg & 0x7f] = (unsigned char)value;
}

static int
test_mc146818(void)
{
    static const struct mc146818_io io = {
        mc_fake_read, mc_fake_write
    };
    struct mc146818_softc sc;
    struct clock_ymdhms dt;
    struct mc_fake fake;

    memset(&sc, 0, sizeof(sc));
    memset(&fake, 0, sizeof(fake));
    fake.regs[0x0b] = 0x02;
    fake.regs[0x00] = 0x56;
    fake.regs[0x02] = 0x34;
    fake.regs[0x04] = 0x12;
    fake.regs[0x06] = 0x07;
    fake.regs[0x07] = 0x01;
    fake.regs[0x08] = 0x08;
    fake.regs[0x09] = 0x26;
    CHECK(mc146818_attach(&sc, "mc-test", &io, &fake, 200) == 0);
    CHECK(attached == &sc.sc_todr);
    CHECK(attached->todr_gettime_ymdhms(attached, &dt) == 0);
    CHECK(dt.dt_year == 2026 && dt.dt_mon == 8 && dt.dt_day == 1 &&
        dt.dt_hour == 12 && dt.dt_min == 34 && dt.dt_sec == 56);
    dt.dt_year = 2027;
    dt.dt_mon = 9;
    dt.dt_day = 2;
    dt.dt_wday = 4;
    dt.dt_hour = 23;
    dt.dt_min = 45;
    dt.dt_sec = 1;
    CHECK(attached->todr_settime_ymdhms(attached, &dt) == 0);
    CHECK(fake.regs[0x09] == 0x27 && fake.regs[0x08] == 0x09 &&
        fake.regs[0x04] == 0x23 && fake.regs[0x00] == 0x01);
    dt.dt_year = 2100;
    CHECK(attached->todr_settime_ymdhms(attached, &dt) == EOVERFLOW);
    return 0;
}

struct i2c_fake {
    unsigned char regs[16];
    unsigned pointer;
};

static int
i2c_fake_transfer(struct i2c_adapter *adapter, struct i2c_msg *messages,
    unsigned count)
{
    struct i2c_fake *fake = adapter->cookie;
    unsigned i;
    unsigned m;

    for (m = 0; m < count; ++m) {
        if ((messages[m].flags & I2C_M_RD) != 0) {
            for (i = 0; i < messages[m].len; ++i)
                messages[m].buf[i] = fake->regs[fake->pointer++ & 15u];
        } else {
            fake->pointer = messages[m].buf[0];
            for (i = 1; i < messages[m].len; ++i)
                fake->regs[fake->pointer++ & 15u] = messages[m].buf[i];
        }
    }
    return 0;
}

static int
test_pcf8563(void)
{
    static const struct i2c_adapter_ops ops = { i2c_fake_transfer };
    struct pcf8563_softc sc;
    struct i2c_adapter adapter;
    struct clock_ymdhms dt;
    struct i2c_fake fake;

    memset(&sc, 0, sizeof(sc));
    memset(&fake, 0, sizeof(fake));
    CHECK(i2c_adapter_init(&adapter, "fake", &ops, &fake) == 0);
    CHECK(pcf8563_attach(&sc, "pcf-test", &adapter, 0x51, 200) == 0);
    fake.regs[2] = 0x56;
    fake.regs[3] = 0x34;
    fake.regs[4] = 0x12;
    fake.regs[5] = 0x01;
    fake.regs[6] = 0x06;
    fake.regs[7] = 0x08;
    fake.regs[8] = 0x26;
    CHECK(attached->todr_gettime_ymdhms(attached, &dt) == 0);
    CHECK(dt.dt_year == 2026 && dt.dt_mon == 8 && dt.dt_day == 1 &&
        dt.dt_wday == 6 && dt.dt_hour == 12);
    fake.regs[2] |= 0x80;
    CHECK(attached->todr_gettime_ymdhms(attached, &dt) == EIO);
    fake.regs[2] &= 0x7f;
    dt.dt_year = 1999;
    dt.dt_mon = 12;
    dt.dt_day = 31;
    dt.dt_wday = 5;
    dt.dt_hour = 23;
    dt.dt_min = 59;
    dt.dt_sec = 58;
    CHECK(attached->todr_settime_ymdhms(attached, &dt) == 0);
    CHECK(fake.regs[7] == 0x92 && fake.regs[8] == 0x99);
    dt.dt_year = 2100;
    CHECK(attached->todr_settime_ymdhms(attached, &dt) == EOVERFLOW);
    return 0;
}

struct jz_fake {
    unsigned regs[32];
    unsigned write_reg[16];
    unsigned write_value[16];
    unsigned write_count;
};

static unsigned
jz_fake_read(void *cookie, unsigned reg)
{
    struct jz_fake *fake = cookie;
    return fake->regs[reg / 4u];
}

static void
jz_fake_write(void *cookie, unsigned reg, unsigned value)
{
    struct jz_fake *fake = cookie;

    if (fake->write_count < 16u) {
        fake->write_reg[fake->write_count] = reg;
        fake->write_value[fake->write_count] = value;
    }
    fake->write_count++;
    if (reg == 0x3c && value == 0xa55a)
        fake->regs[reg / 4u] = 1u << 31;
    else
        fake->regs[reg / 4u] = value;
}

static void
jz_fake_delay(void *cookie, unsigned usec)
{
    (void)cookie;
    (void)usec;
}

static int
test_jz4780(void)
{
    static const struct jz4780_rtc_io io = {
        jz_fake_read, jz_fake_write, jz_fake_delay
    };
    struct jz4780_rtc_softc sc;
    struct clock_ymdhms dt;
    struct jz_fake fake;
    time_t seconds;

    memset(&sc, 0, sizeof(sc));
    memset(&fake, 0, sizeof(fake));
    fake.regs[0] = 1u << 7;
    fake.regs[0x34 / 4] = 0x12345678;
    dt.dt_year = 2026;
    dt.dt_mon = 8;
    dt.dt_day = 1;
    dt.dt_wday = 6;
    dt.dt_hour = 1;
    dt.dt_min = 2;
    dt.dt_sec = 3;
    CHECK(clock_ymdhms_to_secs(&dt, &seconds) == 0);
    fake.regs[1] = (unsigned)seconds;
    CHECK(jz4780_rtc_attach(&sc, "jz-test", &io, &fake, 100) == 0);
    memset(&dt, 0, sizeof(dt));
    CHECK(attached->todr_gettime_ymdhms(attached, &dt) == 0);
    CHECK(dt.dt_year == 2026 && dt.dt_mon == 8 && dt.dt_day == 1 &&
        dt.dt_hour == 1 && dt.dt_min == 2 && dt.dt_sec == 3);
    dt.dt_min = 3;
    CHECK(attached->todr_settime_ymdhms(attached, &dt) == 0);
    CHECK(fake.regs[0x34 / 4] == 0x12345678);
    dt.dt_year = 2107;
    CHECK(attached->todr_settime_ymdhms(attached, &dt) == EOVERFLOW);

    fake.write_count = 0;
    fake.regs[0] |= 1u << 1;
    CHECK(jz4780_rtc_poweroff(&sc) == EOPNOTSUPP);
    CHECK(fake.write_count == 0);
    fake.regs[0] &= ~(1u << 1);
    CHECK(jz4780_rtc_poweroff(&sc) == 0);
    CHECK(fake.write_count == 6);
    CHECK(fake.write_reg[0] == 0x3c && fake.write_value[0] == 0xa55a);
    CHECK(fake.write_reg[1] == 0x24 && fake.write_value[1] == 0x0cc0);
    CHECK(fake.write_reg[2] == 0x3c && fake.write_value[2] == 0xa55a);
    CHECK(fake.write_reg[3] == 0x28 && fake.write_value[3] == 0);
    CHECK(fake.write_reg[4] == 0x3c && fake.write_value[4] == 0xa55a);
    CHECK(fake.write_reg[5] == 0x20 && fake.write_value[5] == 1);
    CHECK(jz4780_rtc_poweroff(0) == EINVAL);
    return 0;
}

int
main(void)
{
    CHECK(test_calendar() == 0);
    CHECK(test_mc146818() == 0);
    CHECK(test_pcf8563() == 0);
    CHECK(test_jz4780() == 0);
    puts("rtc_test: ok");
    return 0;
}
