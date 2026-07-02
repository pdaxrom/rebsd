/*
 * Small inetd-compatible super-server for RetroBSD/MIPS.
 *
 * Supports the normal /etc/inetd.conf stream/tcp/nowait subset needed by the
 * first network images.  The file format is intentionally compatible with the
 * historical inetd entry shape so services can be moved to a fuller inetd
 * later without changing configuration.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <netinet/in.h>

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_CONF "/etc/inetd.conf"
#define MAXARGS 16
#define MAXLINE 256

struct service {
    char *name;
    char *server;
    char *argv[MAXARGS + 1];
    int fd;
    struct service *next;
};

static char *conf_path = DEFAULT_CONF;
static int debug;
static int oneshot;
static struct service *services;

static void usage(void);
static char *xstrdup(char *s);
static char *skipws(char *s);
static char *nexttok(char **sp);
static void free_services(void);
static int load_config(void);
static int add_service(char **tok, int ntok);
static int service_socket(char *name);
static void daemonize(void);
static void reap(int sig);
static void serve(struct service *svc);

static void
usage()
{
    fprintf(stderr, "usage: inetd [-d] [-1] [-f config]\n");
    exit(1);
}

static char *
xstrdup(s)
    char *s;
{
    char *p;

    p = malloc(strlen(s) + 1);
    if (!p) {
        perror("inetd: malloc");
        exit(1);
    }
    strcpy(p, s);
    return p;
}

static char *
skipws(s)
    char *s;
{
    while (*s == ' ' || *s == '\t')
        s++;
    return s;
}

static char *
nexttok(sp)
    char **sp;
{
    char *s, *tok;

    s = skipws(*sp);
    if (*s == 0 || *s == '\n' || *s == '#') {
        *sp = s;
        return 0;
    }
    tok = s;
    while (*s && *s != '\n' && *s != ' ' && *s != '\t')
        s++;
    if (*s)
        *s++ = 0;
    *sp = s;
    return tok;
}

static void
free_services()
{
    struct service *svc, *next;
    int i;

    for (svc = services; svc; svc = next) {
        next = svc->next;
        if (svc->fd >= 0)
            close(svc->fd);
        free(svc->name);
        free(svc->server);
        for (i = 0; svc->argv[i]; i++)
            free(svc->argv[i]);
        free(svc);
    }
    services = 0;
}

static int
load_config()
{
    FILE *fp;
    char line[MAXLINE], *p, *tok[MAXARGS + 6];
    int ntok;

    fp = fopen(conf_path, "r");
    if (!fp) {
        perror(conf_path);
        return -1;
    }
    free_services();
    while (fgets(line, sizeof(line), fp)) {
        p = skipws(line);
        if (*p == 0 || *p == '\n' || *p == '#')
            continue;
        ntok = 0;
        while (ntok < MAXARGS + 6 && (tok[ntok] = nexttok(&p)) != 0)
            ntok++;
        if (ntok == 0)
            continue;
        if (ntok < 6) {
            fprintf(stderr, "inetd: short line for %s\n", tok[0]);
            continue;
        }
        if (add_service(tok, ntok) < 0) {
            fclose(fp);
            return -1;
        }
    }
    fclose(fp);
    return 0;
}

static int
add_service(tok, ntok)
    char **tok;
    int ntok;
{
    struct service *svc;
    int i, argc;

    if (strcmp(tok[1], "stream") != 0 || strcmp(tok[2], "tcp") != 0 ||
        strcmp(tok[3], "nowait") != 0) {
        if (debug)
            fprintf(stderr, "inetd: skip unsupported service %s\n", tok[0]);
        return 0;
    }
    if (strcmp(tok[4], "root") != 0) {
        fprintf(stderr, "inetd: only root services supported: %s\n", tok[0]);
        return -1;
    }
    if (strcmp(tok[5], "internal") == 0) {
        if (debug)
            fprintf(stderr, "inetd: skip unsupported internal %s\n", tok[0]);
        return 0;
    }

    svc = calloc(1, sizeof(*svc));
    if (!svc) {
        perror("inetd: calloc");
        return -1;
    }
    svc->fd = -1;
    svc->name = xstrdup(tok[0]);
    svc->server = xstrdup(tok[5]);
    argc = ntok - 6;
    if (argc <= 0) {
        svc->argv[0] = xstrdup(tok[5]);
        argc = 1;
    } else {
        if (argc > MAXARGS)
            argc = MAXARGS;
        for (i = 0; i < argc; i++)
            svc->argv[i] = xstrdup(tok[6 + i]);
    }
    svc->argv[argc] = 0;
    svc->fd = service_socket(svc->name);
    if (svc->fd < 0) {
        free(svc->name);
        free(svc->server);
        for (i = 0; svc->argv[i]; i++)
            free(svc->argv[i]);
        free(svc);
        return -1;
    }
    svc->next = services;
    services = svc;
    return 0;
}

static int
service_socket(name)
    char *name;
{
    struct servent *sp;
    struct sockaddr_in sin;
    int fd, on;

    sp = getservbyname(name, "tcp");
    if (!sp) {
        fprintf(stderr, "inetd: %s/tcp not in /etc/services\n", name);
        return -1;
    }
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("inetd: socket");
        return -1;
    }
    on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = sp->s_port;
    sin.sin_addr.s_addr = INADDR_ANY;
    if (bind(fd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        perror(name);
        close(fd);
        return -1;
    }
    if (listen(fd, 4) < 0) {
        perror("inetd: listen");
        close(fd);
        return -1;
    }
    return fd;
}

static void
daemonize()
{
    int pid, fd;

    pid = fork();
    if (pid < 0) {
        perror("inetd: fork");
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
serve(struct service *svc)
{
    struct sockaddr_in from;
    int len, fd, pid, i;

    len = sizeof(from);
    fd = accept(svc->fd, (struct sockaddr *)&from, &len);
    if (fd < 0) {
        if (errno != EINTR)
            perror("inetd: accept");
        return;
    }
    pid = fork();
    if (pid < 0) {
        perror("inetd: fork");
        close(fd);
        return;
    }
    if (pid == 0) {
        dup2(fd, 0);
        dup2(fd, 1);
        dup2(fd, 2);
        for (i = 3; i < 32; i++)
            if (i != fd)
                close(i);
        execv(svc->server, svc->argv);
        perror(svc->server);
        _exit(1);
    }
    close(fd);
}

int
main(argc, argv)
    int argc;
    char **argv;
{
    struct service *svc;
    fd_set rfds;
    int i, maxfd, n;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) {
            debug = 1;
        } else if (strcmp(argv[i], "-1") == 0) {
            oneshot = 1;
            debug = 1;
        } else if (strcmp(argv[i], "-f") == 0) {
            if (++i >= argc)
                usage();
            conf_path = argv[i];
        } else {
            usage();
        }
    }
    if (load_config() < 0)
        return 1;
    if (!services) {
        fprintf(stderr, "inetd: no enabled services\n");
        return 1;
    }
    if (!debug)
        daemonize();
    signal(SIGCHLD, reap);
    signal(SIGPIPE, SIG_IGN);

    for (;;) {
        FD_ZERO(&rfds);
        maxfd = -1;
        for (svc = services; svc; svc = svc->next) {
            FD_SET(svc->fd, &rfds);
            if (svc->fd > maxfd)
                maxfd = svc->fd;
        }
        n = select(maxfd + 1, &rfds, (fd_set *)0, (fd_set *)0,
            (struct timeval *)0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror("inetd: select");
            return 1;
        }
        for (svc = services; svc; svc = svc->next) {
            if (FD_ISSET(svc->fd, &rfds)) {
                serve(svc);
                if (oneshot)
                    return 0;
            }
        }
    }
}
