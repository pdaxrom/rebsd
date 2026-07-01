/*
 * Small standalone TELNET daemon for RetroBSD.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/file.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/telnet.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "telcrypto.h"

#define TBUFSIZE 512

static int listenfd = -1;
static char *shell_path;
static char *crypto_key;
static struct rtel_session *active_crypto;

static void
usage()
{
    fprintf(stderr, "usage: telnetd [-d] [-1] [-K key] [-p port] [-s shell]\n");
    exit(1);
}

static int
write_all(fd, buf, len)
    int fd;
    unsigned char *buf;
    int len;
{
    int n;

    while (len > 0) {
        if (active_crypto && active_crypto->enabled)
            return rtel_write(fd, active_crypto, buf, len);
        n = write(fd, buf, len);
        if (n <= 0)
            return -1;
        buf += n;
        len -= n;
    }
    return 0;
}

static void
send_cmd(fd, cmd, opt)
    int fd, cmd, opt;
{
    unsigned char b[3];

    b[0] = IAC;
    b[1] = cmd;
    b[2] = opt;
    write_all(fd, b, 3);
}

static void
send_start_options(fd)
    int fd;
{
    send_cmd(fd, WILL, TELOPT_ECHO);
    send_cmd(fd, WILL, TELOPT_SGA);
    send_cmd(fd, DO, TELOPT_SGA);
}

static int
listen_socket(port)
    int port;
{
    struct sockaddr_in sin;
    int s, on;

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        perror("telnetd: socket");
        return -1;
    }
    on = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = INADDR_ANY;
    if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        perror("telnetd: bind");
        close(s);
        return -1;
    }
    if (listen(s, 4) < 0) {
        perror("telnetd: listen");
        close(s);
        return -1;
    }
    return s;
}

static int
open_pty(slave_path)
    char *slave_path;
{
    char master_path[16];
    int unit, fd;

    for (unit = 0; unit < 4; unit++) {
        sprintf(master_path, "/dev/ptyp%d", unit);
        sprintf(slave_path, "/dev/ttyp%d", unit);
        fd = open(master_path, O_RDWR);
        if (fd >= 0)
            return fd;
    }
    return -1;
}

static void
pty_defaults(fd)
    int fd;
{
    struct sgttyb sg;

    if (ioctl(fd, TIOCGETP, &sg) < 0)
        return;
    sg.sg_flags |= ECHO | CRMOD;
    sg.sg_flags &= ~(RAW | CBREAK);
    ioctl(fd, TIOCSETP, &sg);
}

static int
spawn_login(master, slave_path, host)
    int master;
    char *slave_path, *host;
{
    char *shell_name;
    int slave, pid, i;

    slave = open(slave_path, O_RDWR);
    if (slave < 0) {
        perror(slave_path);
        return -1;
    }
    pty_defaults(slave);
    pid = fork();
    if (pid < 0) {
        perror("telnetd: fork");
        close(slave);
        return -1;
    }
    if (pid == 0) {
        close(master);
        if (listenfd >= 0)
            close(listenfd);
        dup2(slave, 0);
        dup2(slave, 1);
        dup2(slave, 2);
        for (i = 3; i < 32; i++)
            if (i != slave)
                close(i);
        if (shell_path) {
            shell_name = strrchr(shell_path, '/');
            shell_name = shell_name ? shell_name + 1 : shell_path;
            execl(shell_path, shell_name, (char *)0);
        } else {
            execl("/bin/login", "login", "-h", host, (char *)0);
            execl("/usr/bin/login", "login", "-h", host, (char *)0);
        }
        perror("telnetd: login");
        _exit(1);
    }
    close(slave);
    return pid;
}

struct telstate {
    int state;
    int optcmd;
    int cr;
};

static int
send_net_data(net, c)
    int net, c;
{
    unsigned char b[2];

    b[0] = c;
    if (c == IAC) {
        b[1] = IAC;
        return write_all(net, b, 2);
    } else {
        return write_all(net, b, 1);
    }
}

static void
net_to_pty(net, pty, pid, st, buf, len)
    int net, pty, pid;
    struct telstate *st;
    unsigned char *buf;
    int len;
{
    unsigned char c;
    int i;

    for (i = 0; i < len; i++) {
        c = buf[i];
        if (st->cr) {
            st->cr = 0;
            if (c == '\n' || c == 0)
                continue;
        }
        switch (st->state) {
        case 0:
            if (c == IAC) {
                st->state = 1;
            } else if (c == '\r') {
                write(pty, &c, 1);
                st->cr = 1;
            } else {
                write(pty, &c, 1);
            }
            break;
        case 1:
            if (c == IAC) {
                write(pty, &c, 1);
                st->state = 0;
            } else if (c == DO || c == DONT || c == WILL || c == WONT) {
                st->optcmd = c;
                st->state = 2;
            } else if (c == SB) {
                st->state = 3;
            } else {
                if (c == IP || c == BREAK) {
                    c = 3;
                    write(pty, &c, 1);
                } else if (c == AYT) {
                    static unsigned char yes[] = "\r\n[yes]\r\n";
                    write_all(net, yes, sizeof(yes) - 1);
                }
                st->state = 0;
            }
            break;
        case 2:
            if (st->optcmd == WILL && c != TELOPT_SGA)
                send_cmd(net, DONT, c);
            else if (st->optcmd == DO && c != TELOPT_ECHO &&
                c != TELOPT_SGA)
                send_cmd(net, WONT, c);
            st->state = 0;
            break;
        case 3:
            if (c == IAC)
                st->state = 4;
            break;
        case 4:
            st->state = (c == SE) ? 0 : 3;
            break;
        }
    }
}

static int
pty_to_net(net, buf, len)
    int net;
    unsigned char *buf;
    int len;
{
    int i;

    for (i = 0; i < len; i++)
        if (send_net_data(net, buf[i]) < 0)
            return -1;
    return 0;
}

static void
session(net, from)
    int net;
    struct sockaddr_in *from;
{
    char slave_path[16];
    char *host;
    struct telstate ts;
    struct rtel_session crypto;
    fd_set rfds;
    struct timeval tv;
    unsigned char buf[TBUFSIZE];
    int master, pid, n, maxfd, status, net_open, child_done, done_idle;
    int did_pty;

    memset(&crypto, 0, sizeof(crypto));
    if (crypto_key) {
        if (rtel_server_handshake(net, crypto_key, &crypto) < 0) {
            close(net);
            return;
        }
        active_crypto = &crypto;
    }
    host = inet_ntoa(from->sin_addr);
    master = open_pty(slave_path);
    if (master < 0) {
        static unsigned char msg[] = "telnetd: no free pty\r\n";
        write_all(net, msg, sizeof(msg) - 1);
        close(net);
        return;
    }
    pid = spawn_login(master, slave_path, host);
    if (pid < 0) {
        close(master);
        close(net);
        return;
    }

    memset(&ts, 0, sizeof(ts));
    net_open = 1;
    child_done = 0;
    done_idle = 0;
    send_start_options(net);
    for (;;) {
        did_pty = 0;
        FD_ZERO(&rfds);
        if (net_open)
            FD_SET(net, &rfds);
        FD_SET(master, &rfds);
        maxfd = net > master ? net + 1 : master + 1;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        n = select(maxfd, &rfds, (fd_set *)0, (fd_set *)0, &tv);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (n == 0)
            goto check_child;
        if (net_open && FD_ISSET(net, &rfds)) {
            n = rtel_read(net, &crypto, buf, sizeof(buf));
            if (n <= 0)
                net_open = 0;
            else
                net_to_pty(net, master, pid, &ts, buf, n);
        }
        if (FD_ISSET(master, &rfds)) {
            n = read(master, buf, sizeof(buf));
            if (n <= 0)
                break;
            if (net_open && pty_to_net(net, buf, n) < 0)
                net_open = 0;
            else
                did_pty = 1;
        }
check_child:
        if (!child_done && waitpid(pid, &status, WNOHANG) == pid)
            child_done = 1;
        if (child_done) {
            if (did_pty)
                done_idle = 0;
            else if (++done_idle >= 2)
                break;
        }
    }
    kill(pid, SIGHUP);
    close(master);
    close(net);
    active_crypto = 0;
    waitpid(pid, &status, 0);
}

static void
reap(sig)
    int sig;
{
    int status;

    (void)sig;
    while (waitpid(-1, &status, WNOHANG) > 0)
        ;
}

static void
daemonize()
{
    int pid, fd;

    pid = fork();
    if (pid < 0) {
        perror("telnetd: fork");
        exit(1);
    }
    if (pid > 0)
        exit(0);
    fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, 0);
        dup2(fd, 1);
        dup2(fd, 2);
        if (fd > 2)
            close(fd);
    }
}

int
main(argc, argv)
    int argc;
    char **argv;
{
    struct sockaddr_in from;
    int port, debug, oneshot, i, net, fromlen, pid;

    port = 23;
    debug = 0;
    oneshot = 0;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) {
            debug = 1;
        } else if (strcmp(argv[i], "-1") == 0) {
            oneshot = 1;
            debug = 1;
        } else if (strcmp(argv[i], "-K") == 0) {
            if (++i >= argc)
                usage();
            crypto_key = argv[i];
        } else if (strcmp(argv[i], "-p") == 0) {
            if (++i >= argc)
                usage();
            port = atoi(argv[i]);
        } else if (strcmp(argv[i], "-s") == 0) {
            if (++i >= argc)
                usage();
            shell_path = argv[i];
        } else {
            usage();
        }
    }
    if (port <= 0 || port > 65535)
        usage();

    listenfd = listen_socket(port);
    if (listenfd < 0)
        return 1;
    if (!debug)
        daemonize();
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, reap);

    for (;;) {
        fromlen = sizeof(from);
        net = accept(listenfd, (struct sockaddr *)&from, &fromlen);
        if (net < 0) {
            if (errno == EINTR)
                continue;
            perror("telnetd: accept");
            break;
        }
        if (oneshot) {
            session(net, &from);
            break;
        }
        pid = fork();
        if (pid == 0) {
            close(listenfd);
            listenfd = -1;
            session(net, &from);
            exit(0);
        }
        if (pid < 0)
            perror("telnetd: fork");
        close(net);
    }
    close(listenfd);
    return 0;
}
