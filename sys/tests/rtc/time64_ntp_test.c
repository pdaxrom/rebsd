#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ntp_proto.h"

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "time64_ntp_test:%d: %s\n", __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

static time_t
utc_time(int year, int month, int day, int hour)
{
    struct tm tm;

    memset(&tm, 0, sizeof(tm));
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    return timegm(&tm);
}

static void
make_timeval(long long usec, struct timeval *tv)
{
    ntp_unix_usec_to_timeval(usec, tv);
}

int
main(void)
{
    struct timeval value, decoded, t1, t4, t2_value, t3_value;
    struct ntp_timestamp stamp, t2, t3;
    long long source, result, offset, delay;

    CHECK(sizeof(time_t) == 8);
    value.tv_sec = utc_time(2040, 1, 2, 3);
    value.tv_usec = 456789;
    source = ntp_timeval_to_unix_usec(&value);
    ntp_timeval_to_timestamp(&value, &stamp);
    CHECK(stamp.seconds < 2208988800U);

    result = ntp_timestamp_to_unix_usec(&stamp, value.tv_sec);
    CHECK(result >= source - 1 && result <= source + 1);
    result = ntp_timestamp_to_unix_usec(&stamp, 0);
    CHECK(result >= source - 1 && result <= source + 1);
    ntp_unix_usec_to_timeval(result, &decoded);
    CHECK(decoded.tv_sec == value.tv_sec);
    CHECK(decoded.tv_usec >= value.tv_usec - 1 &&
        decoded.tv_usec <= value.tv_usec + 1);

    /*
     * The client sends at T, the server clock is 2 seconds ahead, the
     * outward path is 100 ms, server processing is 20 ms, and the return
     * path is 120 ms.  The standard result is +1.990 s offset and 220 ms
     * round-trip delay.
     */
    make_timeval(source, &t1);
    make_timeval(source + 240000, &t4);
    make_timeval(source + 2100000, &t2_value);
    make_timeval(source + 2120000, &t3_value);
    ntp_timeval_to_timestamp(&t2_value, &t2);
    ntp_timeval_to_timestamp(&t3_value, &t3);
    CHECK(ntp_calculate_offset(&t1, &t2, &t3, &t4, &offset,
        &delay) == 0);
    CHECK(offset >= 1989999 && offset <= 1990001);
    CHECK(delay >= 219998 && delay <= 220002);

    ntp_unix_usec_to_timeval(-1, &decoded);
    CHECK(decoded.tv_sec == (time_t)-1);
    CHECK(decoded.tv_usec == 999999);

    if (failures != 0)
        return 1;
    puts("time64_ntp_test: ok");
    return 0;
}
