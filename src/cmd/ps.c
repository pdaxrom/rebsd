/*
 *  1999/8/11 - Remove reference to SDETACH.  It was removed from the kernel
 *          (finally) because it was not needed.
 *
 *  1997/12/16 - Fix coredump when processing -U.
 *
 *  1996/11/16 - Move 'psdatabase' in /var/run.
 *
 *  12/20/94 - Missing casts caused errors in reporting on swapped
 *         processes - sms
 *  1/7/93 - Heavily revised when the symbol table format changed - sms
 *
 *  ps - process status
 *  Usage:  ps [ acglnrtuwxU# ]
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <pwd.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>

#include <sys/param.h>
#include <sys/dir.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <utmp.h>
#include <psout.h>

#define NNAMESIZ    8

int hz;
int chkpid = 0;
char aflg;              /* -a: all processes, not just mine */
char cflg;              /* -c: not complete listing of args, just comm. */
char gflg;              /* -g: complete listing including group headers, etc */
char lflg;              /* -l: long listing form */
char rflg;              /* -r: raw output in style <psout.h> */
char uflg;              /* -u: user name */
char wflg;              /* -w[w]: wide terminal */
char xflg;              /* -x: ALL processes, even those without ttys */
char *tptr, *mytty;
int nproc;
int nttys;
int npr;                /* number of processes found so far */
int twidth;             /* terminal width */
int cmdstart;           /* start position for command field */

/*
 * 256 terminals was not only wasteful but unrealistic.  For one thing
 * 2.11BSD uses bit 7 of the minor device (for almost all terminal interfaces)
 * to indicate direct/modem status - this means that 128 terminals is
 * a better maximum number of terminals, for another thing the system can't
 * support 256 terminals - other resources (memory, files, processes) will
 * have been exhausted long ago.  If 'ps' complains about too many terminals
 * it is time to clean up /dev!
 */
#define MAXTTYS     160 /* 128 plus a few extra */

struct ttys {
    char    name[14];   /* MAXNAMLEN uses too much memory,  besides */
                        /* device names tend to be very short */
    dev_t   ttyd;

} allttys[MAXTTYS];

struct winsize ws;
struct psout *outargs;  /* info for first npr processes */

/*
 * Attempt to avoid stats by guessing minor device
 * numbers from tty names.  Console is known,
 * know that r(hp|up|mt) are unlikely as are different mem's,
 * floppy, null, tty, etc.
 */
void maybetty(char *cp)
{
    struct ttys *dp;
    struct stat stb;

    /* Allow only terminal devices. */
    switch (cp[0]) {
    case 'c':
        if (strcmp(cp, "console") == 0)
            break;
        return;
    case 't':
        if (strncmp(cp, "tty", 3) == 0)
            break;
        return;
    default:
        return;
    }
    if (nttys >= MAXTTYS) {
        fprintf(stderr, "ps: tty table overflow\n");
        exit(1);
    }
    dp = &allttys[nttys++];
    (void)strcpy(dp->name, cp);

    if (stat(dp->name, &stb) == 0 &&
       (stb.st_mode & S_IFMT) == S_IFCHR)
        dp->ttyd = stb.st_rdev;
    else
        dp->ttyd = -1;
}

void getdev()
{
    DIR *df;
    struct direct *dbuf;

    if (chdir("/dev") < 0) {
        perror("/dev");
        exit(1);
    }
    if ((df = opendir(".")) == NULL) {
        fprintf(stderr, "Can't open . in /dev\n");
        exit(1);
    }
    while ((dbuf = readdir(df)))
        maybetty(dbuf->d_name);
    closedir(df);
}

static char *
getttydev(dev_t ttyd)
{
    int tty_step;
    char *p;

    if (ttyd == NODEV)
        return "?";
    for (tty_step = 0; tty_step < nttys; ++tty_step) {
        if (allttys[tty_step].ttyd != ttyd)
            continue;
        p = allttys[tty_step].name;
        if (strncmp(p, "tty", 3) == 0)
            p += 3;
        return p;
    }
    return "?";
}

static int
savkproc(const struct kinfo_proc *kp, int puid)
{
    const struct proc *procp = &kp->kp_proc;
    struct psout *a;
    char *tp;

    tp = getttydev(kp->kp_eproc.e_tdev);
    if ((tptr && strncmp(tptr, tp, 2) != 0) ||
        (!aflg && strncmp(mytty, tp, 2) != 0))
        return 0;

    a = &outargs[npr];
    a->o_uid = puid;
    a->o_pid = procp->p_pid;
    a->o_flag = procp->p_flag;
    a->o_ppid = procp->p_ppid;
    a->o_cpu = procp->p_cpu;
    a->o_pri = procp->p_pri;
    a->o_nice = procp->p_nice;
    a->o_addr0 = procp->p_addr;
    a->o_size = (procp->p_dsize + procp->p_ssize + USIZE) / DEV_BSIZE;
    a->o_wchan = procp->p_wchan;
    a->o_pgrp = procp->p_pgrp;
    strncpy(a->o_tty, tp, sizeof(a->o_tty));
    a->o_ttyd = kp->kp_eproc.e_tdev;
    a->o_stat = procp->p_stat;
    a->o_utime = kp->ki_utime;
    a->o_stime = kp->ki_stime;
    a->o_cutime = kp->ki_cutime;
    a->o_cstime = kp->ki_cstime;
    a->o_sigs = kp->ki_sigs;
    a->o_uname[0] = 0;
    strncpy(a->o_comm, kp->ki_comm, MAXCOMLEN);
    a->o_comm[MAXCOMLEN] = 0;
    snprintf(a->o_args, sizeof(a->o_args), "(%s)",
        a->o_comm[0] != 0 ? a->o_comm : "?");
    return 1;
}

int pscomp(const void *a1, const void *a2)
{
    const struct psout *x1 = a1;
    const struct psout *x2 = a2;
    int c;

    c = (x1)->o_ttyd - (x2)->o_ttyd;
    if (c == 0)
        c = (x1)->o_pid - (x2)->o_pid;
    return(c);
}

/*
 * fixup figures out everybodys name and sorts into a nice order.
 */
void fixup(int np)
{
    int i;
    struct passwd  *pw;

    if (uflg) {
        setpwent();
        /*
         * If we want names, traverse the password file. For each
         * passwd entry, look for it in the processes.
         * In case of multiple entries in the password file we believe
         * the first one (same thing ls does).
         */
        while ((pw = getpwent()) != (struct passwd *) NULL) {
            for (i = 0; i < np; i++)
                if (outargs[i].o_uid == pw->pw_uid) {
                    if (outargs[i].o_uname[0] == 0)
                        strcpy(outargs[i].o_uname, pw->pw_name);
                }
        }
        endpwent();
    }
    qsort(outargs, np, sizeof (outargs[0]), pscomp);
}

static struct kinfo_proc *
getprocs(size_t *countp)
{
    struct kinfo_proc *procs;
    int mib[4];
    size_t size;
    int attempt;

    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC;
    mib[2] = KERN_PROC_ALL;
    mib[3] = 0;
    for (attempt = 0; attempt < 3; ++attempt) {
        size = 0;
        if (sysctl(mib, 4, NULL, &size, NULL, 0) < 0) {
            fprintf(stderr, "ps: kern.proc size: %s\n", strerror(errno));
            exit(1);
        }
        if (size == 0 || size % sizeof(*procs) != 0) {
            fprintf(stderr, "ps: invalid kern.proc size %u\n",
                (unsigned)size);
            exit(1);
        }
        procs = malloc(size);
        if (procs == NULL) {
            fputs("ps: not enough memory for process snapshot\n", stderr);
            exit(1);
        }
        if (sysctl(mib, 4, procs, &size, NULL, 0) == 0) {
            if (size % sizeof(*procs) != 0) {
                free(procs);
                fputs("ps: kern.proc record size mismatch\n", stderr);
                exit(1);
            }
            *countp = size / sizeof(*procs);
            return procs;
        }
        free(procs);
        if (errno != ENOMEM) {
            fprintf(stderr, "ps: kern.proc fetch: %s\n", strerror(errno));
            exit(1);
        }
    }
    fputs("ps: process table changed too quickly\n", stderr);
    exit(1);
}

void ptime(struct psout *a)
{
    time_t  tm;

    tm = (a->o_utime + a->o_stime + 30) / hz;
    printf("%3lld:", (long long)(tm / 60));
    tm %= 60;
    printf(tm < 10 ? "0%lld" : "%lld", (long long)tm);
}

char *uhdr = "USER       PID NICE SZ TTY  TIME";

void upr(struct psout *a)
{
    printf("%-8.8s%6u%5d%3d %-3.3s", a->o_uname, a->o_pid,
        a->o_nice, a->o_size, a->o_tty);
    ptime(a);
}

char *shdr = "   PID TTY  TIME";

void spr(struct psout *a)
{
    printf("%6u %-3.3s",a->o_pid,a->o_tty);
    ptime(a);
}

void lpr(struct psout *a)
{
    static char clist[] = "0SWRIZT";

    printf("%3o %c %5u %5u %5u %3d %3d %4d %#10x %5d ",
        0377 & a->o_flag, clist[(unsigned char)a->o_stat],
        a->o_uid, a->o_pid, a->o_ppid, a->o_cpu & 0377,
        a->o_pri, a->o_nice, a->o_addr0, a->o_size);
    if (a->o_wchan)
        printf("%10x", (unsigned)a->o_wchan);
    else
        fputs("          ", stdout);
    printf(" %-3.3s", a->o_tty);
    ptime(a);
}

void printhdr()
{
    char *hdr, *cmdstr = " COMMAND";
    char longhdr[96];

    if (rflg)
        return;
    if (lflg && uflg) {
        fputs("ps: specify only one of l and u.\n",stderr);
        exit(1);
    }
    if (lflg) {
        snprintf(longhdr, sizeof(longhdr),
            "%3s %1s %5s %5s %5s %3s %3s %4s %10s %5s "
            "%10s %-3s%6s",
            "F", "S", "UID", "PID", "PPID", "CPU", "PRI", "NICE",
            "ADDR", "SZ", "WCHAN", "TTY", "TIME");
        hdr = longhdr;
    } else
        hdr = uflg ? uhdr : shdr;
    fputs(hdr,stdout);
    cmdstart = strlen(hdr);
    if (cmdstart + strlen(cmdstr) >= twidth)
        cmdstr = " CMD";
    printf("%s\n", cmdstr);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    int     uid, euid, puid;
    int     i;
    char    *ap;
    struct proc    *procp;
    struct kinfo_proc *kprocs;
    size_t proc_count;

    if ((ioctl(fileno(stdout), TIOCGWINSZ, &ws) != -1 &&
         ioctl(fileno(stderr), TIOCGWINSZ, &ws) != -1 &&
         ioctl(fileno(stdin), TIOCGWINSZ, &ws) != -1) ||
         ws.ws_col == 0)
        twidth = 80;
    else
        twidth = ws.ws_col;

    mytty = ttyname(0);

    argc--, argv++;
    if (argc > 0) {
        ap = argv [0];
        while (*ap) switch (*ap++) {
        case '-':
            break;

        case 'a':
            aflg++;
            break;

        case 'c':
            cflg++;
            break;

        case 'g':
            gflg++;
            break;

        case 'l':
            lflg    = 1;
            break;

        case 'n':
            lflg    = 1;
            break;

        case 'r':
            rflg++;
            break;

        case 't':
            if (*ap) {
                tptr = ap;
            } else if (! mytty) {
                /* Stdin is not a tty - 't' flag ignored. */
                break;
            } else {
                tptr = mytty;
                if (strncmp(tptr, "/dev/", 5) == 0)
                    tptr += 5;
            }
            if (strncmp(tptr, "tty", 3) == 0)
                tptr += 3;
            aflg++;
            gflg++;
            if (tptr && *tptr == '?')
                xflg++;
            while (*ap)
                ap++;
            break;

        case 'u':
            uflg    = 1;
            break;

        case 'w':
            if (wflg)
                twidth  = BUFSIZ;
            else if (twidth < 132)
                twidth  = 132;
            wflg++;
            break;

        case 'x':
            xflg++;
            break;

        default:
            if (!isdigit(ap[-1]))
                break;
            chkpid  = atoi(--ap);
            *ap = '\0';
            aflg++;
            xflg++;
            break;
        }
    }

    getdev();
    hz = HZ;
    kprocs = getprocs(&proc_count);
    nproc = proc_count;
    outargs = (struct psout *)calloc(nproc, sizeof(struct psout));
    if (!outargs) {
        fputs("ps: not enough memory for saving info\n", stderr);
        exit(1);
    }
    uid = getuid();
    euid = geteuid();
    /* handle case where ps is in background and ttyname returns 0 */
    if (!mytty) mytty = "";
    if (!strncmp(mytty,"/dev/",5)) mytty += 5;
    if (!strncmp(mytty,"tty",3)) mytty += 3;
    printhdr();
    for (i = 0; i < nproc; ++i) {
        procp = &kprocs[i].kp_proc;
        if (procp->p_stat == 0)
            continue;
        if (procp->p_pgrp == 0 && xflg == 0)
            continue;
        if (!tptr && !gflg && !xflg && procp->p_ppid == 1)
            continue;
        puid = procp->p_uid;
        if ((uid != puid && euid != puid && aflg == 0) ||
            (chkpid != 0 && chkpid != procp->p_pid))
            continue;
        if (savkproc(&kprocs[i], puid))
            npr++;
    }
    free(kprocs);
    fixup(npr);
    for (i = 0; i < npr; i++) {
        int    cmdwidth = twidth - cmdstart - 2;
        struct psout *a = &outargs[i];

        if (rflg) {
            if (write(1, (char *) a, sizeof (*a)) != sizeof (*a))
                perror("write");
            continue;
        } else if (lflg)
            lpr(a);
        else if (uflg)
            upr(a);
        else
            spr(a);

        if (cmdwidth < 0)
            cmdwidth = 80 - cmdstart - 2;
        if (a->o_stat == SZOMB)
            printf("%.*s", cmdwidth, " <defunct>");
        else if (a->o_pid == 0)
            printf("%.*s", cmdwidth, " swapper");
        else
            printf(" %.*s", twidth - cmdstart - 2, cflg ? a->o_comm : a->o_args);
        putchar('\n');
    }
    exit(!npr);
}
