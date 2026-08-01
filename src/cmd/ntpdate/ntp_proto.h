#ifndef _NTP_PROTO_H_
#define _NTP_PROTO_H_

#include <sys/time.h>

#define NTP_PACKET_SIZE 48
#define NTP_PORT 123

struct ntp_timestamp {
    unsigned int seconds;
    unsigned int fraction;
};

void ntp_get_timestamp(const unsigned char *, struct ntp_timestamp *);
void ntp_put_timestamp(unsigned char *, const struct ntp_timestamp *);
void ntp_timeval_to_timestamp(const struct timeval *, struct ntp_timestamp *);
long long ntp_timestamp_to_unix_usec(const struct ntp_timestamp *, time_t);
long long ntp_timeval_to_unix_usec(const struct timeval *);
void ntp_unix_usec_to_timeval(long long, struct timeval *);
int ntp_timestamp_is_zero(const struct ntp_timestamp *);
int ntp_calculate_offset(const struct timeval *, const struct ntp_timestamp *,
    const struct ntp_timestamp *, const struct timeval *, long long *,
    long long *);

#endif
