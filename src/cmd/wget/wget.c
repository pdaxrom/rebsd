/*
 * Minimal HTTP/1.0 downloader for RetroBSD.
 *
 * This is deliberately small: plain HTTP only, close-delimited body,
 * and one request per process.  TLS belongs in a separate port.
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WGET_BUFSIZE 512
#define WGET_HDRSIZE 4096

struct urlinfo {
    char host[128];
    int port;
    char *path;
};

static int quiet;
static char *outfile;

static void
usage()
{
    fprintf(stderr, "usage: wget [-q] [-O file] http://host[:port]/path\n");
    exit(1);
}

static int
parse_url(url, u)
    char *url;
    struct urlinfo *u;
{
    char *p, *colon, *path;
    int len;

    if (strncmp(url, "http://", 7) != 0) {
        fprintf(stderr, "wget: only http:// URLs are supported\n");
        return -1;
    }

    p = url + 7;
    path = strchr(p, '/');
    if (path == 0)
        len = strlen(p);
    else
        len = path - p;
    if (len <= 0 || len >= sizeof(u->host)) {
        fprintf(stderr, "wget: bad host name\n");
        return -1;
    }

    memcpy(u->host, p, len);
    u->host[len] = 0;
    u->port = 80;
    u->path = path ? path : "/";

    colon = strchr(u->host, ':');
    if (colon) {
        *colon++ = 0;
        u->port = atoi(colon);
        if (u->port <= 0 || u->port > 65535) {
            fprintf(stderr, "wget: bad port\n");
            return -1;
        }
        if (u->host[0] == 0) {
            fprintf(stderr, "wget: bad host name\n");
            return -1;
        }
    }
    return 0;
}

static char *
default_output_name(path)
    char *path;
{
    char *base, *q;
    static char name[128];
    int len;

    base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (*base == 0)
        base = "index.html";
    q = strchr(base, '?');
    len = q ? q - base : strlen(base);
    if (len <= 0)
        base = "index.html", len = strlen(base);
    if (len >= sizeof(name))
        len = sizeof(name) - 1;
    memcpy(name, base, len);
    name[len] = 0;
    return name;
}

static int
open_http(u)
    struct urlinfo *u;
{
    struct sockaddr_in sin;
    struct hostent *hp;
    int s;

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(u->port);
    sin.sin_addr.s_addr = inet_addr(u->host);
    if (sin.sin_addr.s_addr == -1L) {
        hp = gethostbyname(u->host);
        if (hp == 0) {
            herror(u->host);
            return -1;
        }
        if (hp->h_addrtype != AF_INET || hp->h_length != 4 ||
            hp->h_addr == 0) {
            fprintf(stderr, "wget: bad host address %s\n", u->host);
            return -1;
        }
        memcpy(&sin.sin_addr, hp->h_addr, hp->h_length);
    }

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        perror("wget: socket");
        return -1;
    }
    if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        perror("wget: connect");
        close(s);
        return -1;
    }
    return s;
}

static int
write_all(fd, buf, len)
    int fd;
    char *buf;
    int len;
{
    int n;

    while (len > 0) {
        n = write(fd, buf, len);
        if (n <= 0)
            return -1;
        buf += n;
        len -= n;
    }
    return 0;
}

static int
send_request(fd, u)
    int fd;
    struct urlinfo *u;
{
    char req[WGET_BUFSIZE];

    if (strlen(u->path) + strlen(u->host) + 96 >= sizeof(req)) {
        fprintf(stderr, "wget: request too long\n");
        return -1;
    }
    sprintf(req,
        "GET %s HTTP/1.0\r\n"
        "Host: %s\r\n"
        "User-Agent: RetroBSD-wget/0.1\r\n"
        "Connection: close\r\n"
        "\r\n",
        u->path, u->host);
    if (write_all(fd, req, strlen(req)) < 0) {
        perror("wget: write");
        return -1;
    }
    return 0;
}

static int
find_header_end(buf, len)
    char *buf;
    int len;
{
    int i;

    for (i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' &&
            buf[i + 2] == '\r' && buf[i + 3] == '\n')
            return i + 4;
    }
    for (i = 0; i + 1 < len; i++) {
        if (buf[i] == '\n' && buf[i + 1] == '\n')
            return i + 2;
    }
    return -1;
}

static int
status_code(header)
    char *header;
{
    char *p;

    p = strchr(header, ' ');
    if (p == 0)
        return 0;
    return atoi(p + 1);
}

static int
copy_response(fd, out, totalp)
    int fd;
    FILE *out;
    long *totalp;
{
    char buf[WGET_BUFSIZE];
    char header[WGET_HDRSIZE];
    int hlen, hend, n, code;
    long total;

    hlen = 0;
    total = 0;
    for (;;) {
        n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            perror("wget: read");
            return -1;
        }
        if (n == 0) {
            fprintf(stderr, "wget: no HTTP response\n");
            return -1;
        }
        if (hlen + n >= sizeof(header)) {
            fprintf(stderr, "wget: response header too large\n");
            return -1;
        }
        memcpy(header + hlen, buf, n);
        hlen += n;
        header[hlen] = 0;
        hend = find_header_end(header, hlen);
        if (hend >= 0)
            break;
    }

    code = status_code(header);
    if (code != 200) {
        fprintf(stderr, "wget: HTTP status %d\n", code);
        return -1;
    }

    if (hend < hlen) {
        n = hlen - hend;
        if (fwrite(header + hend, 1, n, out) != n) {
            perror("wget: write output");
            return -1;
        }
        total += n;
    }

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            perror("wget: write output");
            return -1;
        }
        total += n;
    }
    if (n < 0) {
        perror("wget: read");
        return -1;
    }
    if (fflush(out) == EOF) {
        perror("wget: write output");
        return -1;
    }
    *totalp = total;
    return 0;
}

int
main(argc, argv)
    int argc;
    char **argv;
{
    struct urlinfo u;
    char *url, *name;
    FILE *out;
    int fd, i;
    long total;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-q") == 0) {
            quiet = 1;
            continue;
        }
        if (strcmp(argv[i], "-O") == 0) {
            if (++i >= argc)
                usage();
            outfile = argv[i];
            continue;
        }
        if (argv[i][0] == '-')
            usage();
        break;
    }
    if (i + 1 != argc)
        usage();
    url = argv[i];

    if (parse_url(url, &u) < 0)
        return 1;

    name = outfile ? outfile : default_output_name(u.path);
    if (strcmp(name, "-") == 0)
        out = stdout;
    else {
        out = fopen(name, "w");
        if (out == 0) {
            perror(name);
            return 1;
        }
    }

    fd = open_http(&u);
    if (fd < 0)
        return 1;
    if (send_request(fd, &u) < 0) {
        close(fd);
        return 1;
    }
    if (copy_response(fd, out, &total) < 0) {
        close(fd);
        if (out != stdout)
            fclose(out);
        return 1;
    }
    close(fd);
    if (out != stdout && fclose(out) == EOF) {
        perror(name);
        return 1;
    }
    if (!quiet && out != stdout)
        printf("%s: %ld bytes\n", name, total);
    return 0;
}
