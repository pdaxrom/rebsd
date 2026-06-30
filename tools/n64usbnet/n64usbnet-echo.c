#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define DEFAULT_ADDR "10.64.0.1"
#define DEFAULT_PORT 2323

static int
write_all(int fd, const char *buf, ssize_t len)
{
    ssize_t n;

    while (len > 0) {
        n = write(fd, buf, (size_t)len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            return -1;
        buf += n;
        len -= n;
    }
    return 0;
}

static void
serve_client(int fd)
{
    char buf[2048];
    ssize_t n;

    for (;;) {
        n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror("read");
            break;
        }
        if (n == 0)
            break;
        if (write_all(fd, buf, n) < 0) {
            perror("write");
            break;
        }
    }
}

int
main(int argc, char **argv)
{
    struct sockaddr_in sin, peer;
    socklen_t peerlen;
    const char *addr;
    int fd, cfd, on, port;

    addr = DEFAULT_ADDR;
    port = DEFAULT_PORT;
    if (argc > 1)
        addr = argv[1];
    if (argc > 2)
        port = atoi(argv[2]);
    if (argc > 3 || port <= 0 || port > 65535) {
        fprintf(stderr, "usage: %s [address [port]]\n", argv[0]);
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }

    on = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
        perror("setsockopt");
        close(fd);
        return 1;
    }

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons((unsigned short)port);
    if (inet_aton(addr, &sin.sin_addr) == 0) {
        fprintf(stderr, "%s: bad address\n", addr);
        close(fd);
        return 1;
    }

    if (bind(fd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        perror("bind");
        close(fd);
        return 1;
    }
    if (listen(fd, 4) < 0) {
        perror("listen");
        close(fd);
        return 1;
    }

    fprintf(stderr, "n64usbnet-echo: listening on %s:%d\n", addr, port);
    for (;;) {
        peerlen = sizeof(peer);
        cfd = accept(fd, (struct sockaddr *)&peer, &peerlen);
        if (cfd < 0) {
            if (errno == EINTR)
                continue;
            perror("accept");
            close(fd);
            return 1;
        }
        fprintf(stderr, "n64usbnet-echo: connection from %s:%u\n",
            inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));
        serve_client(cfd);
        close(cfd);
    }
}
