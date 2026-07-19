/*
 * Display open files using the bounded KERN_PROCFILES snapshot.
 *
 * The old implementation walked proc, user, file, inode and socket objects
 * through /dev/kmem, /dev/mem and /dev/swap.  Apart from requiring an exact
 * kernel namelist, that let a diagnostic utility dereference unchecked kernel
 * addresses.  The kernel now flattens the useful fields into kinfo_procfile.
 */
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/stat.h>
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DTYPE_INODE    1
#define DTYPE_SOCKET   2
#define DTYPE_PIPE     3
#define DTYPE_SHM      4

struct namefilter {
    struct namefilter *next;
    dev_t dev;
    ino_t ino;
    char *name;
};

static struct namefilter *filters;
static int verbose;

static void
usage(void)
{
    fputs("usage: fstat [-v] [-u user] [-p pid] [filename ...]\n", stderr);
    exit(1);
}

static struct kinfo_procfile *
procfiles(size_t *countp)
{
    int mib[2] = { CTL_KERN, KERN_PROCFILES };
    struct kinfo_procfile *data;
    size_t size;
    int tries;

    for (tries = 0; tries < 3; tries++) {
        size = 0;
        if (sysctl(mib, 2, NULL, &size, NULL, 0) < 0)
            return NULL;
        data = malloc(size != 0 ? size : 1);
        if (data == NULL)
            return NULL;
        if (sysctl(mib, 2, data, &size, NULL, 0) == 0) {
            *countp = size / sizeof(*data);
            return data;
        }
        free(data);
        if (errno != ENOMEM)
            return NULL;
    }
    errno = ENOMEM;
    return NULL;
}

static void
addfilter(char *name)
{
    struct namefilter *filter;
    struct stat sb;

    if (stat(name, &sb) < 0) {
        fprintf(stderr, "fstat: %s: %s\n", name, strerror(errno));
        return;
    }
    filter = malloc(sizeof(*filter));
    if (filter == NULL) {
        fprintf(stderr, "fstat: out of memory\n");
        exit(1);
    }
    filter->next = filters;
    filter->name = name;
    if (S_ISBLK(sb.st_mode)) {
        filter->dev = sb.st_rdev;
        filter->ino = 0;
    } else {
        filter->dev = sb.st_dev;
        filter->ino = sb.st_ino;
    }
    filters = filter;
}

static char *
matchname(const struct kinfo_procfile *kpf)
{
    struct namefilter *filter;

    if (filters == NULL)
        return "";
    for (filter = filters; filter != NULL; filter = filter->next)
        if (filter->dev == kpf->kpf_dev &&
            (filter->ino == 0 || filter->ino == kpf->kpf_inode))
            return filter->name;
    return NULL;
}

static const char *
typename(const struct kinfo_procfile *kpf)
{
    if (kpf->kpf_type == DTYPE_SOCKET)
        return "soc";
    if (kpf->kpf_type == DTYPE_PIPE)
        return "pip";
    if (kpf->kpf_type == DTYPE_SHM)
        return "shm";
    switch (kpf->kpf_mode & S_IFMT) {
    case S_IFCHR: return "chr";
    case S_IFDIR: return "dir";
    case S_IFBLK: return "blk";
    case S_IFREG: return "reg";
    case S_IFLNK: return "lnk";
    case S_IFSOCK: return "soc";
    default: return "unk";
    }
}

static void
printone(const struct kinfo_procfile *kpf, const char *user, const char *name)
{
    printf("%-8.8s %-10.10s %5d  ", user, kpf->kpf_comm, kpf->kpf_pid);
    if (kpf->kpf_fd == KINFO_FD_CWD)
        printf("  wd");
    else
        printf("%4d", kpf->kpf_fd);

    if (kpf->kpf_type == DTYPE_SOCKET || kpf->kpf_type == DTYPE_SHM) {
        printf("\t*       %08lx                 %3s", kpf->kpf_datap,
            typename(kpf));
    } else {
        printf("\t%2d, %2d\t%5lu\t%10lld\t%3s",
            major(kpf->kpf_dev), minor(kpf->kpf_dev),
            (unsigned long)kpf->kpf_inode, (long long)kpf->kpf_size,
            typename(kpf));
    }
    if (*name != '\0')
        printf(" %s", name);
    if (verbose && kpf->kpf_fd != KINFO_FD_CWD)
        printf(" [file=%08lx flags=%x offset=%lld]", kpf->kpf_filep,
            kpf->kpf_flags, (long long)kpf->kpf_offset);
    putchar('\n');
}

int
main(int argc, char **argv)
{
    struct kinfo_procfile *data;
    struct passwd *pw;
    char *end, *name;
    long pidarg;
    uid_t uidarg;
    size_t count, i;
    int ch, havepid, haveuid, badfilter;

    havepid = haveuid = badfilter = 0;
    pidarg = 0;
    uidarg = 0;
    while ((ch = getopt(argc, argv, "p:u:v")) != EOF) {
        switch (ch) {
        case 'p':
            if (havepid)
                usage();
            pidarg = strtol(optarg, &end, 10);
            if (*optarg == '\0' || *end != '\0' || pidarg < 0)
                usage();
            havepid = 1;
            break;
        case 'u':
            if (haveuid)
                usage();
            pw = getpwnam(optarg);
            if (pw == NULL) {
                fprintf(stderr, "fstat: %s: unknown user\n", optarg);
                return 1;
            }
            uidarg = pw->pw_uid;
            haveuid = 1;
            break;
        case 'v':
            verbose = 1;
            break;
        default:
            usage();
        }
    }
    while (optind < argc) {
        struct namefilter *before = filters;
        addfilter(argv[optind++]);
        if (before == filters)
            badfilter = 1;
    }
    if (badfilter && filters == NULL)
        return 1;

    data = procfiles(&count);
    if (data == NULL) {
        fprintf(stderr, "fstat: KERN_PROCFILES: %s\n", strerror(errno));
        return 1;
    }
    printf("USER\t CMD\t      PID    FD\tDEVICE\tINODE\t      SIZE TYPE%s\n",
        filters != NULL ? " NAME" : "");
    for (i = 0; i < count; i++) {
        if (havepid && data[i].kpf_pid != pidarg)
            continue;
        if (haveuid && data[i].kpf_uid != uidarg)
            continue;
        name = matchname(&data[i]);
        if (name == NULL)
            continue;
        pw = getpwuid(data[i].kpf_uid);
        printone(&data[i], pw != NULL ? pw->pw_name : "unknown", name);
    }
    free(data);
    return 0;
}
