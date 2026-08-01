#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int failures;

char *
tztab(int zone, int dst)
{
    (void)zone;
    (void)dst;
    return "GMT";
}

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "time64_tzif_test:%d: %s\n", __LINE__, #condition); \
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

int
main(int argc, char **argv)
{
    struct tm *tm;
    time_t value;

    if (argc != 2) {
        fprintf(stderr, "usage: time64_tzif_test zone-file\n");
        return 2;
    }
    CHECK(sizeof(time_t) == 8);
    CHECK(setenv("TZ", argv[1], 1) == 0);
    tzset();

    value = utc_time(2040, 1, 15, 12);
    tm = localtime(&value);
    CHECK(tm != NULL);
    if (tm != NULL) {
        CHECK(tm->tm_year == 140);
        CHECK(tm->tm_mon == 0);
        CHECK(tm->tm_mday == 15);
        CHECK(tm->tm_hour == 4);
        CHECK(tm->tm_isdst == 0);
        CHECK(tm->tm_gmtoff == -8 * 60 * 60);
        CHECK(strcmp(tm->tm_zone, "PST") == 0);
    }

    value = utc_time(2040, 7, 15, 12);
    tm = localtime(&value);
    CHECK(tm != NULL);
    if (tm != NULL) {
        CHECK(tm->tm_year == 140);
        CHECK(tm->tm_mon == 6);
        CHECK(tm->tm_mday == 15);
        CHECK(tm->tm_hour == 5);
        CHECK(tm->tm_isdst == 1);
        CHECK(tm->tm_gmtoff == -7 * 60 * 60);
        CHECK(strcmp(tm->tm_zone, "PDT") == 0);
    }

    if (failures != 0)
        return 1;
    puts("time64_tzif_test: ok");
    return 0;
}
