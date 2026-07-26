#include <string.h>
#include <time.h>

char *
asctime_r(const struct tm *timeptr, char *buffer)
{
    char *result;

    result = asctime(timeptr);
    if (result == 0)
        return 0;
    memcpy(buffer, result, 26);
    return buffer;
}

char *
ctime_r(const time_t *clock, char *buffer)
{
    struct tm tm;

    if (localtime_r(clock, &tm) == 0)
        return 0;
    return asctime_r(&tm, buffer);
}

struct tm *
gmtime_r(const time_t *clock, struct tm *result)
{
    struct tm *value;

    value = gmtime(clock);
    if (value == 0)
        return 0;
    memcpy(result, value, sizeof(*result));
    return result;
}

struct tm *
localtime_r(const time_t *clock, struct tm *result)
{
    struct tm *value;

    value = localtime(clock);
    if (value == 0)
        return 0;
    memcpy(result, value, sizeof(*result));
    return result;
}
