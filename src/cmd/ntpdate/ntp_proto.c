/*
 * Machine-independent NTP timestamp conversion for ntpdate(8).
 *
 * NTP timestamps carry a 32-bit seconds field.  The era is reconstructed
 * around the local clock, as required by the NTP era rules.  If the local
 * clock has not yet been initialized, select the modern (post-1970) era;
 * this also makes a first network synchronization work after the 2036 NTP
 * era rollover.
 */

#include <sys/types.h>
#include <sys/time.h>

#include "ntp_proto.h"

#define NTP_UNIX_EPOCH 2208988800LL
#define NTP_ERA_SECONDS 4294967296LL
#define NTP_MODERN_PIVOT 946684800LL       /* 2000-01-01 UTC */
#define USEC_PER_SEC 1000000LL

static unsigned int
ntp_get_u32(const unsigned char *cp)
{
    return ((unsigned int)cp[0] << 24) |
        ((unsigned int)cp[1] << 16) |
        ((unsigned int)cp[2] << 8) |
        (unsigned int)cp[3];
}

static void
ntp_put_u32(unsigned char *cp, unsigned int value)
{
    cp[0] = (unsigned char)(value >> 24);
    cp[1] = (unsigned char)(value >> 16);
    cp[2] = (unsigned char)(value >> 8);
    cp[3] = (unsigned char)value;
}

void
ntp_get_timestamp(const unsigned char *cp, struct ntp_timestamp *stamp)
{
    stamp->seconds = ntp_get_u32(cp);
    stamp->fraction = ntp_get_u32(cp + 4);
}

void
ntp_put_timestamp(unsigned char *cp, const struct ntp_timestamp *stamp)
{
    ntp_put_u32(cp, stamp->seconds);
    ntp_put_u32(cp + 4, stamp->fraction);
}

long long
ntp_timeval_to_unix_usec(const struct timeval *tv)
{
    return (long long)tv->tv_sec * USEC_PER_SEC + tv->tv_usec;
}

void
ntp_unix_usec_to_timeval(long long value, struct timeval *tv)
{
    long long seconds;
    long remainder;

    seconds = value / USEC_PER_SEC;
    remainder = (long)(value % USEC_PER_SEC);
    if (remainder < 0) {
        --seconds;
        remainder += USEC_PER_SEC;
    }
    tv->tv_sec = (time_t)seconds;
    tv->tv_usec = remainder;
    tv->tv_pad = 0;
}

void
ntp_timeval_to_timestamp(const struct timeval *tv,
    struct ntp_timestamp *stamp)
{
    long long seconds;
    long usec;
    long long ntp_seconds;
    long long raw_seconds;

    seconds = tv->tv_sec;
    usec = tv->tv_usec;
    while (usec < 0) {
        --seconds;
        usec += USEC_PER_SEC;
    }
    while (usec >= USEC_PER_SEC) {
        ++seconds;
        usec -= USEC_PER_SEC;
    }
    ntp_seconds = seconds + NTP_UNIX_EPOCH;
    raw_seconds = ntp_seconds % NTP_ERA_SECONDS;
    if (raw_seconds < 0)
        raw_seconds += NTP_ERA_SECONDS;
    stamp->seconds = (unsigned int)raw_seconds;
    stamp->fraction = (unsigned int)
        (((unsigned long long)usec << 32) / USEC_PER_SEC);
}

static long long
ntp_distance(long long left, long long right)
{
    return left >= right ? left - right : right - left;
}

long long
ntp_timestamp_to_unix_usec(const struct ntp_timestamp *stamp, time_t pivot)
{
    long long era;
    long long seconds;
    long long candidate;
    long long best;
    long long pivot_ntp;
    long long distance;
    long long best_distance;
    unsigned long long fraction_usec;
    int delta;

    if ((long long)pivot < NTP_MODERN_PIVOT) {
        /* A stopped RTC commonly leaves ReBSD at the Unix epoch. */
        era = stamp->seconds < (unsigned int)NTP_UNIX_EPOCH ? 1 : 0;
        seconds = era * NTP_ERA_SECONDS + stamp->seconds;
    } else {
        pivot_ntp = (long long)pivot + NTP_UNIX_EPOCH;
        era = pivot_ntp / NTP_ERA_SECONDS;
        best = era * NTP_ERA_SECONDS + stamp->seconds;
        best_distance = ntp_distance(best, pivot_ntp);
        for (delta = -1; delta <= 1; ++delta) {
            candidate = (era + delta) * NTP_ERA_SECONDS +
                stamp->seconds;
            distance = ntp_distance(candidate, pivot_ntp);
            if (distance < best_distance) {
                best = candidate;
                best_distance = distance;
            }
        }
        seconds = best;
    }

    fraction_usec = ((unsigned long long)stamp->fraction *
        USEC_PER_SEC + 0x80000000ULL) >> 32;
    return (seconds - NTP_UNIX_EPOCH) * USEC_PER_SEC +
        (long long)fraction_usec;
}

int
ntp_timestamp_is_zero(const struct ntp_timestamp *stamp)
{
    return stamp->seconds == 0 && stamp->fraction == 0;
}

int
ntp_calculate_offset(const struct timeval *t1,
    const struct ntp_timestamp *receive,
    const struct ntp_timestamp *transmit, const struct timeval *t4,
    long long *offset, long long *delay)
{
    long long u1, u2, u3, u4;

    if (ntp_timestamp_is_zero(receive) ||
        ntp_timestamp_is_zero(transmit))
        return -1;
    u1 = ntp_timeval_to_unix_usec(t1);
    u4 = ntp_timeval_to_unix_usec(t4);
    u2 = ntp_timestamp_to_unix_usec(receive, t1->tv_sec);
    u3 = ntp_timestamp_to_unix_usec(transmit, t1->tv_sec);
    if (u3 < u2)
        return -1;
    *offset = ((u2 - u1) + (u3 - u4)) / 2;
    *delay = (u4 - u1) - (u3 - u2);
    return 0;
}
