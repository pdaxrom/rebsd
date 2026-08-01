/*
 * Small RFC 4330/RFC 5905 SNTP client for ReBSD.
 *
 * The historical WIZnet-only demonstration has been replaced by this
 * socket-based utility so every architecture uses the common network stack.
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "ntp_proto.h"

#define NTP_DEFAULT_TIMEOUT 5

static void
usage(void)
{
    fprintf(stderr, "usage: ntpdate [-qv] [-t seconds] server\n");
    exit(1);
}

static int
parse_timeout(char *arg)
{
    char *end;
    long value;

    value = strtol(arg, &end, 10);
    if (arg == end || *end != 0 || value < 1 || value > 60)
        usage();
    return (int)value;
}

static int
resolve_server(char *name, struct sockaddr_in *sin)
{
    struct hostent *host;
    unsigned long address;

    memset(sin, 0, sizeof(*sin));
    sin->sin_family = AF_INET;
    sin->sin_port = htons(NTP_PORT);
    address = inet_addr(name);
    if (address != INADDR_NONE) {
        sin->sin_addr.s_addr = address;
        return 0;
    }
    host = gethostbyname(name);
    if (host == 0 || host->h_addrtype != AF_INET || host->h_length != 4 ||
        host->h_addr == 0)
        return -1;
    memcpy(&sin->sin_addr, host->h_addr, 4);
    return 0;
}

static void
print_interval(const char *label, long long value)
{
    unsigned long long magnitude;

    if (value < 0) {
        putchar('-');
        magnitude = (unsigned long long)(-(value + 1)) + 1;
    } else {
        putchar('+');
        magnitude = (unsigned long long)value;
    }
    printf("%llu.%06llu %s", magnitude / 1000000ULL,
        magnitude % 1000000ULL, label);
}

static int
valid_reply(unsigned char *packet, int length, unsigned char *request,
    int verbose)
{
    int leap, version, mode, stratum;

    if (length < NTP_PACKET_SIZE) {
        fprintf(stderr, "ntpdate: short reply (%d bytes)\n", length);
        return 0;
    }
    leap = packet[0] >> 6;
    version = (packet[0] >> 3) & 7;
    mode = packet[0] & 7;
    stratum = packet[1];
    if (leap == 3) {
        fprintf(stderr, "ntpdate: server clock is unsynchronized\n");
        return 0;
    }
    if (version < 3 || version > 4 || mode != 4) {
        fprintf(stderr, "ntpdate: invalid reply version=%d mode=%d\n",
            version, mode);
        return 0;
    }
    if (stratum < 1 || stratum > 15) {
        fprintf(stderr, "ntpdate: server refused synchronization (stratum %d)\n",
            stratum);
        return 0;
    }
    if (memcmp(packet + 24, request + 40, 8) != 0) {
        fprintf(stderr, "ntpdate: reply does not match request\n");
        return 0;
    }
    if (verbose)
        printf("ntpdate: NTPv%d stratum %d\n", version, stratum);
    return 1;
}

int
main(int argc, char **argv)
{
    unsigned char request[NTP_PACKET_SIZE];
    unsigned char reply[512];
    struct ntp_timestamp request_stamp, receive_stamp, transmit_stamp;
    struct sockaddr_in server, from;
    struct timeval t1, t4, timeout, target;
    struct tm *calendar;
    fd_set readfds;
    long long offset, delay, target_usec;
    int fd, fromlen, length, option, query, verbose, wait_seconds;
    char *name;

    query = 0;
    verbose = 0;
    wait_seconds = NTP_DEFAULT_TIMEOUT;
    while ((option = getopt(argc, argv, "qvt:")) != -1) {
        switch (option) {
        case 'q':
            query = 1;
            break;
        case 'v':
            verbose = 1;
            break;
        case 't':
            wait_seconds = parse_timeout(optarg);
            break;
        default:
            usage();
        }
    }
    if (optind + 1 != argc)
        usage();
    name = argv[optind];
    if (resolve_server(name, &server) < 0) {
        fprintf(stderr, "ntpdate: cannot resolve %s\n", name);
        return 1;
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("ntpdate: socket");
        return 1;
    }
    memset(request, 0, sizeof(request));
    request[0] = (4 << 3) | 3;       /* NTPv4, client mode */
    request[2] = 4;
    request[3] = (unsigned char)-20;
    if (gettimeofday(&t1, (struct timezone *)0) < 0) {
        perror("ntpdate: gettimeofday");
        close(fd);
        return 1;
    }
    ntp_timeval_to_timestamp(&t1, &request_stamp);
    ntp_put_timestamp(request + 40, &request_stamp);
    if (sendto(fd, (char *)request, sizeof(request), 0,
        (struct sockaddr *)&server, sizeof(server)) != sizeof(request)) {
        perror("ntpdate: sendto");
        close(fd);
        return 1;
    }

    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    timeout.tv_sec = wait_seconds;
    timeout.tv_usec = 0;
    length = select(fd + 1, &readfds, (fd_set *)0, (fd_set *)0, &timeout);
    if (length < 0) {
        perror("ntpdate: select");
        close(fd);
        return 1;
    }
    if (length == 0) {
        fprintf(stderr, "ntpdate: no reply from %s\n", name);
        close(fd);
        return 1;
    }
    fromlen = sizeof(from);
    length = recvfrom(fd, (char *)reply, sizeof(reply), 0,
        (struct sockaddr *)&from, &fromlen);
    if (gettimeofday(&t4, (struct timezone *)0) < 0) {
        perror("ntpdate: gettimeofday");
        close(fd);
        return 1;
    }
    close(fd);
    if (length < 0) {
        perror("ntpdate: recvfrom");
        return 1;
    }
    if (from.sin_addr.s_addr != server.sin_addr.s_addr ||
        from.sin_port != server.sin_port) {
        fprintf(stderr, "ntpdate: reply from unexpected server\n");
        return 1;
    }
    if (!valid_reply(reply, length, request, verbose))
        return 1;

    ntp_get_timestamp(reply + 32, &receive_stamp);
    ntp_get_timestamp(reply + 40, &transmit_stamp);
    if (ntp_calculate_offset(&t1, &receive_stamp, &transmit_stamp, &t4,
        &offset, &delay) < 0) {
        fprintf(stderr, "ntpdate: invalid server timestamps\n");
        return 1;
    }
    target_usec = ntp_timeval_to_unix_usec(&t4) + offset;
    ntp_unix_usec_to_timeval(target_usec, &target);
    if (!query && settimeofday(&target, (struct timezone *)0) < 0) {
        perror("ntpdate: settimeofday");
        return 1;
    }

    calendar = gmtime(&target.tv_sec);
    if (calendar != 0)
        printf("%s: %04d-%02d-%02d %02d:%02d:%02d.%06ld UTC, ", name,
            calendar->tm_year + 1900, calendar->tm_mon + 1,
            calendar->tm_mday, calendar->tm_hour, calendar->tm_min,
            calendar->tm_sec, target.tv_usec);
    else
        printf("%s: time %lld.%06ld, ", name,
            (long long)target.tv_sec, target.tv_usec);
    print_interval("sec offset, ", offset);
    print_interval("sec delay\n", delay);
    return 0;
}
