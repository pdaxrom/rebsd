/*
 * USB Ethernet TCP smoke.
 *
 * The host side should run a TCP echo service on 10.64.0.1:2323.  On macOS
 * one simple option is:
 *
 *      nc -lk 10.64.0.1 2323
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define USBN_PEER_ADDR "10.64.0.1"
#define USBN_PEER_PORT 2323

static char msg[] = "usbn external tcp smoke\n";

static int
same(a, b, n)
    char *a, *b;
    int n;
{
    int i;

    for (i = 0; i < n; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

static int
timeout(sig)
    int sig;
{
    sig = sig;
    write(2, "usbn-tcp-smoke: timeout\n", 24);
    _exit(2);
    return 0;
}

int
main(argc, argv)
    int argc;
    char **argv;
{
    struct sockaddr_in sin;
    char buf[64];
    char *peer;
    int fd, got, i, left, n, sent;
    int port;

    peer = USBN_PEER_ADDR;
    port = USBN_PEER_PORT;
    if (argc > 1)
        peer = argv[1];
    if (argc > 2)
        port = atoi(argv[2]);
    if (argc > 3 || port <= 0 || port > 65535) {
        write(2, "usage: usbn-tcp-smoke [address [port]]\n", 40);
        return 1;
    }

    signal(SIGALRM, timeout);
    alarm(10);

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("usbn-tcp-smoke: socket");
        return 1;
    }

    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = inet_addr(peer);
    if (sin.sin_addr.s_addr == (unsigned long)-1) {
        write(2, "usbn-tcp-smoke: bad peer address\n", 34);
        close(fd);
        return 1;
    }
    for (i = 0; i < sizeof(sin.sin_zero); i++)
        sin.sin_zero[i] = 0;

    if (connect(fd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        perror("usbn-tcp-smoke: connect");
        close(fd);
        return 1;
    }

    sent = sizeof(msg) - 1;
    if (write(fd, msg, sent) != sent) {
        perror("usbn-tcp-smoke: write");
        close(fd);
        return 1;
    }

    got = 0;
    left = sent;
    while (left > 0) {
        n = read(fd, buf + got, left);
        if (n <= 0) {
            perror("usbn-tcp-smoke: read");
            close(fd);
            return 1;
        }
        got += n;
        left -= n;
    }

    alarm(0);
    close(fd);

    if (got != sent || !same(msg, buf, sent)) {
        write(2, "usbn-tcp-smoke: echo mismatch\n", 30);
        return 1;
    }

    printf("usbn external tcp smoke ok\n");
    return 0;
}
