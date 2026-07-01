/*
 * Small TELNET client for RetroBSD.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/telnet.h>
#include <netdb.h>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "telcrypto.h"

#define TBUFSIZE 512

static struct sgttyb saved_tty;
static int have_tty;
static int netfd = -1;
static char *command;
static char *crypto_key;
static struct rtel_session crypto;

static void
usage()
{
    fprintf(stderr, "usage: telnet [-K key] [-c command] host [port]\n");
    exit(1);
}

static void
restore_tty()
{
    if (have_tty)
        ioctl(0, TIOCSETP, &saved_tty);
}

static void
on_signal(sig)
    int sig;
{
    restore_tty();
    if (netfd >= 0)
        close(netfd);
    signal(sig, SIG_DFL);
    kill(getpid(), sig);
}

static void
set_raw_tty()
{
    struct sgttyb sg;

    if (!isatty(0))
        return;
    if (ioctl(0, TIOCGETP, &saved_tty) < 0)
        return;
    sg = saved_tty;
    sg.sg_flags |= RAW;
    sg.sg_flags &= ~(ECHO | CRMOD | CBREAK);
    if (ioctl(0, TIOCSETP, &sg) == 0) {
        have_tty = 1;
        atexit(restore_tty);
        signal(SIGINT, on_signal);
        signal(SIGTERM, on_signal);
        signal(SIGHUP, on_signal);
    }
}

static int
write_all(fd, buf, len)
    int fd;
    unsigned char *buf;
    int len;
{
    int n;

    while (len > 0) {
        if (fd == netfd && crypto.enabled)
            return rtel_write(fd, &crypto, buf, len);
        n = write(fd, buf, len);
        if (n <= 0)
            return -1;
        buf += n;
        len -= n;
    }
    return 0;
}

static int
send_cmd(cmd, opt)
    int cmd, opt;
{
    unsigned char b[3];

    b[0] = IAC;
    b[1] = cmd;
    b[2] = opt;
    return write_all(netfd, b, 3);
}

static int
connect_host(host, port)
    char *host;
    int port;
{
    struct sockaddr_in sin;
    struct hostent *hp;
    int s;

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = inet_addr(host);
    if (sin.sin_addr.s_addr == -1L) {
        hp = gethostbyname(host);
        if (hp == 0) {
            herror(host);
            return -1;
        }
        if (hp->h_addrtype != AF_INET || hp->h_length != 4 ||
            hp->h_addr == 0) {
            fprintf(stderr, "telnet: bad host address %s\n", host);
            return -1;
        }
        memcpy(&sin.sin_addr, hp->h_addr, hp->h_length);
    }

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        perror("telnet: socket");
        return -1;
    }
    if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        perror("telnet: connect");
        close(s);
        return -1;
    }
    return s;
}

static void
from_keyboard(buf, len)
    unsigned char *buf;
    int len;
{
    unsigned char out[4];
    int i;

    for (i = 0; i < len; i++) {
        if (buf[i] == IAC) {
            out[0] = IAC;
            out[1] = IAC;
            write_all(netfd, out, 2);
        } else if (buf[i] == '\n' || buf[i] == '\r') {
            out[0] = '\r';
            out[1] = '\n';
            write_all(netfd, out, 2);
        } else {
            write_all(netfd, buf + i, 1);
        }
    }
}

static int
accepted_remote_option(opt)
    int opt;
{
    return opt == TELOPT_ECHO || opt == TELOPT_SGA;
}

static int
accepted_local_option(opt)
    int opt;
{
    return opt == TELOPT_SGA;
}

static void
from_network(buf, len)
    unsigned char *buf;
    int len;
{
    static int state;
    static int optcmd;
    int i, c;

    for (i = 0; i < len; i++) {
        c = buf[i] & 0xff;
        switch (state) {
        case 0:
            if (c == IAC)
                state = 1;
            else
                write(1, buf + i, 1);
            break;
        case 1:
            if (c == IAC) {
                write(1, buf + i, 1);
                state = 0;
            } else if (c == DO || c == DONT || c == WILL || c == WONT) {
                optcmd = c;
                state = 2;
            } else if (c == SB) {
                state = 3;
            } else {
                state = 0;
            }
            break;
        case 2:
            if (optcmd == WILL) {
                send_cmd(accepted_remote_option(c) ? DO : DONT, c);
            } else if (optcmd == DO) {
                send_cmd(accepted_local_option(c) ? WILL : WONT, c);
            }
            state = 0;
            break;
        case 3:
            if (c == IAC)
                state = 4;
            break;
        case 4:
            state = (c == SE) ? 0 : 3;
            break;
        }
    }
}

int
main(argc, argv)
    int argc;
    char **argv;
{
    fd_set rfds;
    unsigned char buf[TBUFSIZE];
    char *host;
    int port, n, maxfd, stdin_open, i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0) {
            if (++i >= argc)
                usage();
            command = argv[i];
        } else if (strcmp(argv[i], "-K") == 0) {
            if (++i >= argc)
                usage();
            crypto_key = argv[i];
        } else {
            break;
        }
    }
    if (i >= argc || argc - i > 2)
        usage();
    host = argv[i++];
    port = i < argc ? atoi(argv[i]) : 23;
    if (port <= 0 || port > 65535)
        usage();

    netfd = connect_host(host, port);
    if (netfd < 0)
        return 1;
    signal(SIGPIPE, SIG_IGN);
    if (crypto_key && rtel_client_handshake(netfd, crypto_key, &crypto) < 0) {
        perror("telnet: encrypted handshake");
        close(netfd);
        return 1;
    }
    if (command) {
        from_keyboard((unsigned char *)command, strlen(command));
        from_keyboard((unsigned char *)"\n", 1);
        stdin_open = 0;
    } else {
        set_raw_tty();
        stdin_open = 1;
    }

    for (;;) {
        FD_ZERO(&rfds);
        if (stdin_open)
            FD_SET(0, &rfds);
        FD_SET(netfd, &rfds);
        maxfd = netfd > 0 ? netfd + 1 : 1;
        n = select(maxfd, &rfds, (fd_set *)0, (fd_set *)0,
            (struct timeval *)0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror("telnet: select");
            break;
        }
        if (stdin_open && FD_ISSET(0, &rfds)) {
            n = read(0, buf, sizeof(buf));
            if (n <= 0) {
                if (!have_tty)
                    shutdown(netfd, 1);
                stdin_open = 0;
            } else {
                from_keyboard(buf, n);
            }
        }
        if (FD_ISSET(netfd, &rfds)) {
            n = rtel_read(netfd, &crypto, buf, sizeof(buf));
            if (n <= 0)
                break;
            from_network(buf, n);
        }
    }

    restore_tty();
    close(netfd);
    return 0;
}
