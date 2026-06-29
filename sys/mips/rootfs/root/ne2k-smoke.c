/*
 * QEMU NE2K external-path smoke for Malta.
 *
 * The Malta run-net target exposes a QEMU user-mode guestfwd echo endpoint:
 * 10.0.2.100:2323 -> host /bin/cat.  This program proves that the NE2K path
 * can send and receive a TCP stream outside loopback.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#define QEMU_GUESTFWD_ADDR \
    (((unsigned long)10 << 24) | ((unsigned long)0 << 16) | \
    ((unsigned long)2 << 8) | (unsigned long)100)
#define QEMU_GUESTFWD_PORT 2323

static char msg[] = "ne2k external tcp smoke\n";

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
    write(2, "ne2k-smoke: timeout\n", 20);
    _exit(2);
    return 0;
}

int
main()
{
    struct sockaddr_in sin;
    char buf[64];
    int fd, got, i, left, n, sent;

    signal(SIGALRM, timeout);
    alarm(10);

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("ne2k-smoke: socket");
        return 1;
    }

    sin.sin_family = AF_INET;
    sin.sin_port = htons(QEMU_GUESTFWD_PORT);
    sin.sin_addr.s_addr = QEMU_GUESTFWD_ADDR;
    for (i = 0; i < sizeof(sin.sin_zero); i++)
        sin.sin_zero[i] = 0;

    if (connect(fd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        perror("ne2k-smoke: connect 10.0.2.100:2323");
        close(fd);
        return 1;
    }

    sent = sizeof(msg) - 1;
    if (write(fd, msg, sent) != sent) {
        perror("ne2k-smoke: write");
        close(fd);
        return 1;
    }

    got = 0;
    left = sent;
    while (left > 0) {
        n = read(fd, buf + got, left);
        if (n <= 0) {
            perror("ne2k-smoke: read");
            close(fd);
            return 1;
        }
        got += n;
        left -= n;
    }

    alarm(0);
    close(fd);

    if (got != sent || !same(msg, buf, sent)) {
        write(2, "ne2k-smoke: echo mismatch\n", 26);
        return 1;
    }

    printf("ne2k external tcp smoke ok\n");
    return 0;
}
