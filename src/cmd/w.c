/*
 * w - print system status (who and what)
 *
 * Rewritten using sysctl, no nlist or raw kernel memory used.
 *
 * This program is similar to the systat command on Tenex/Tops 10/20
 */
#include <ctype.h>
#include <errno.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <time.h>
#include <unistd.h>
#include <utmp.h>

#define NMAX sizeof(utmp.ut_name)
#define LMAX sizeof(utmp.ut_line)
#define ARGWIDTH 33  /* # chars left on 80 col crt for args */
struct smproc {
    short w_pid;               /* proc.p_pid */
    int w_igintr;              /* INTR+3*QUIT, 0=die, 1=ign, 2=catch */
    time_t w_time;             /* CPU time used by this process */
    time_t w_ctime;            /* CPU time used by children */
    dev_t w_tty;               /* tty device of process */
    char w_comm[15];           /* user.u_comm, null terminated */
    char w_args[ARGWIDTH + 1]; /* args if interesting process */
} * pr;

FILE *ut;
dev_t tty;
char doing[520]; /* process attached to terminal */
time_t proctime; /* cpu time of process in doing */
unsigned avenrun[3];

#define DIV60(t) ((t + 30) / 60) /* x/60 rounded */
#define TTYEQ (tty == pr[i].w_tty)
#define IGINT (1 + 3 * 1) /* ignoring both SIGINT & SIGQUIT */

char *program;
int header = 1;          /* true if -h flag: don't print heading */
int lflag = 1;           /* true if -l flag: long style output */
time_t idle;             /* number of minutes user is idle */
int nusers;              /* number of users logged in now */
char *sel_user;          /* login of particular user selected */
int wcmd = 1;            /* running as the w command */
time_t jobtime;          /* total cpu time visible */
time_t now;              /* the current time of day */
struct tm *nowt;         /* current time as time struct */
struct timeval boottime; /* time since last reboot */
time_t uptime;           /* elapsed time since */
int np;                  /* number of processes currently active */
struct utmp utmp;
static void readpr(void);
static void prtat(struct tm *p);
static void gettty(void);
static int findidle(void);
static void putline(void);
static void prttime(time_t tim, char *tail);

int main(int argc, char **argv)
{
    int days, hrs, mins;
    int i;
    char *cp;
    int curpid, empty;
    size_t size;
    int mib[2];

    program = argv[0];
    if ((cp = strrchr(program, '/')) || *(cp = program) == '-')
        cp++;
    if (*cp == 'u')
        wcmd = 0;

    while ((i = getopt(argc, argv, "hlswu")) != EOF) {
        switch (i) {
        case 'h':
            header = 0;
            break;
        case 'l':
            lflag++;
            break;
        case 's':
            lflag = 0;
            break;
        case 'u':
            wcmd = 0;
            break;
        case 'w':
            wcmd = 1;
            break;
        default:
            fprintf(stderr, "Usage: %s [-hlswu] [user]\n", program);
            exit(1);
        }
    }
    argc -= optind;
    argv += optind;
    if (*argv)
        sel_user = *argv;

    if (wcmd)
        readpr();

    ut = fopen(_PATH_UTMP, "r");
    if (header) {
        /* Print time of day */
        time(&now);
        nowt = localtime(&now);
        prtat(nowt);

        mib[0] = CTL_KERN;
        mib[1] = KERN_BOOTTIME;
        size = sizeof(boottime);
        if (sysctl(mib, 2, &boottime, &size, NULL, 0) != -1 && boottime.tv_sec != 0) {
            uptime = now - boottime.tv_sec;
            days = uptime / (60L * 60L * 24L);
            uptime %= (60L * 60L * 24L);
            hrs = uptime / (60L * 60L);
            uptime %= (60L * 60L);
            mins = DIV60(uptime);

            printf("  up");
            if (days > 0)
                printf(" %d day%s,", days, days > 1 ? "s" : "");
            if (hrs > 0 && mins > 0) {
                printf(" %2d:%02d,", hrs, mins);
            } else {
                if (hrs > 0)
                    printf(" %d hr%s,", hrs, hrs > 1 ? "s" : "");
                if (mins > 0)
                    printf(" %d min%s,", mins, mins > 1 ? "s" : "");
            }
        }

        /* Print number of users logged in to system */
        while (fread(&utmp, sizeof(utmp), 1, ut)) {
            if (utmp.ut_name[0] != '\0')
                nusers++;
        }
        rewind(ut);
        printf("  %d user%s", nusers, nusers > 1 ? "s" : "");

        if (getloadavg(avenrun, sizeof(avenrun) / sizeof(avenrun[0])) == -1)
            printf(", no load average information available\n");
        else {
            printf(",  load averages:");
            for (i = 0; i < (sizeof(avenrun) / sizeof(avenrun[0])); i++) {
                if (i > 0)
                    printf(",");
                printf(" %u.%02u", avenrun[i] / 100, avenrun[i] % 100);
            }
        }
        printf("\n");
        if (wcmd == 0)
            exit(0);

        /* Headers for rest of output */
        if (lflag)
            printf("%-*.*s %-*.*s  login@  idle   JCPU   PCPU  what\n",
                   (int)NMAX, (int)NMAX, "User", (int)LMAX,
                   (int)LMAX, "tty");
        else
            printf("%-*.*s tty idle  what\n", (int)NMAX, (int)NMAX, "User");
        fflush(stdout);
    }

    for (;;) { /* for each entry in utmp */
        if (fread(&utmp, sizeof(utmp), 1, ut) == NULL) {
            fclose(ut);
            exit(0);
        }
        if (utmp.ut_name[0] == '\0')
            continue; /* that tty is free */
        if (sel_user && strncmp(utmp.ut_name, sel_user, NMAX) != 0)
            continue; /* we wanted only somebody else */

        gettty();
        jobtime = 0;
        proctime = 0;
        strcpy(doing, "-"); /* default act: normally never prints */
        empty = 1;
        curpid = -1;
        idle = findidle();
        for (i = 0; i < np; i++) { /* for each process on this tty */
            if (!(TTYEQ))
                continue;
            jobtime += pr[i].w_time + pr[i].w_ctime;
            proctime += pr[i].w_time;
            if (empty && pr[i].w_igintr != IGINT) {
                empty = 0;
                curpid = -1;
            }
            if (pr[i].w_pid > curpid && (pr[i].w_igintr != IGINT || empty)) {
                curpid = pr[i].w_pid;
                strcpy(doing, lflag ? pr[i].w_args : pr[i].w_comm);
                if (doing[0] == 0 || doing[0] == '-' && doing[1] <= ' ' || doing[0] == '?') {
                    strcat(doing, " (");
                    strcat(doing, pr[i].w_comm);
                    strcat(doing, ")");
                }
            }
        }
        putline();
    }
}

/* figure out the major/minor device # pair for this tty */
void gettty()
{
    char ttybuf[20];
    struct stat statbuf;

    ttybuf[0] = 0;
    strcpy(ttybuf, "/dev/");
    strcat(ttybuf, utmp.ut_line);
    stat(ttybuf, &statbuf);
    tty = statbuf.st_rdev;
}

/*
 * putline: print out the accumulated line of info about one user.
 */
void putline()
{
    /* print login name of the user */
    printf("%-*.*s ", (int)NMAX, (int)NMAX, utmp.ut_name);

    /* print tty user is on */
    if (lflag)
        /* long form: all (up to) LMAX chars */
        printf("%-*.*s", (int)LMAX, (int)LMAX, utmp.ut_line);
    else {
        /* short form: 2 chars, skipping 'tty' if there */
        if (utmp.ut_line[0] == 't' && utmp.ut_line[1] == 't' && utmp.ut_line[2] == 'y')
            printf("%-2.2s", &utmp.ut_line[3]);
        else
            printf("%-2.2s", utmp.ut_line);
    }

    if (lflag)
        /* print when the user logged in */
        prtat(localtime(&utmp.ut_time));

    /* print idle time */
    prttime(idle, " ");

    if (lflag) {
        /* print CPU time for all processes & children */
        prttime(DIV60(jobtime), " ");
        /* print cpu time for interesting process */
        prttime(DIV60(proctime), " ");
    }

    /* what user is doing, either command tail or args */
    printf(" %-.32s\n", doing);
    fflush(stdout);
}

/* find & return number of minutes current tty has been idle */
int findidle()
{
    struct stat stbuf;
    long lastaction, diff;
    char ttyname[20];

    strcpy(ttyname, "/dev/");
    strncat(ttyname, utmp.ut_line, LMAX);
    stat(ttyname, &stbuf);
    time(&now);
    lastaction = stbuf.st_atime;
    diff = now - lastaction;
    diff = DIV60(diff);
    if (diff < 0)
        diff = 0;
    return (diff);
}

/*
 * prttime prints a time in hours and minutes.
 * The character string tail is printed at the end, obvious
 * strings to pass are "", " ", or "am".
 */
void prttime(time_t tim, char *tail)
{
    int didhrs = 0;

    if (tim >= 60) {
        printf("%3lld:", (long long)(tim / 60));
        didhrs++;
    } else {
        printf("    ");
    }
    tim %= 60;
    if (tim > 0 || didhrs) {
        printf(didhrs && tim < 10 ? "%02lld" : "%2lld", (long long)tim);
    } else {
        printf("  ");
    }
    printf("%s", tail);
}

/* prtat prints a 12 hour time given a pointer to a time of day */
void prtat(struct tm *p)
{
    int pm;
    time_t t;

    t = p->tm_hour;
    pm = (t > 11);
    if (t > 11)
        t -= 12;
    if (t == 0)
        t = 12;
    prttime(t * 60 + p->tm_min, pm ? "pm" : "am");
}

/*
 * readpr finds and reads in the array pr, containing the interesting
 * parts of the proc and user tables for each live process.
 */
void readpr()
{
    struct kinfo_proc *kp;
    struct proc *p;
    struct smproc *smp;
    struct kinfo_proc *kpt;
    int pn, nproc;
    int mib[4], st;
    size_t size;
    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC;
    mib[2] = KERN_PROC_ALL;
    size = 0;
    st = sysctl(mib, 4, NULL, &size, NULL, 0);
    if (st == -1) {
        fprintf(stderr, "sysctl: %s \n", strerror(errno));
        exit(1);
    }
    if (size % sizeof(struct kinfo_proc) != 0) {
        fprintf(stderr, "proc size mismatch (%zu total, %zu chunks)\n", size,
                sizeof(struct kinfo_proc));
        exit(1);
    }
    kpt = (struct kinfo_proc *)malloc(size);
    if (kpt == (struct kinfo_proc *)NULL) {
        fprintf(stderr, "Not %zu bytes of memory for proc table\n", size);
        exit(1);
    }
    if (sysctl(mib, 4, kpt, &size, NULL, 0) == -1) {
        fprintf(stderr, "sysctl fetch of proc table failed: %s\n", strerror(errno));
        exit(1);
    }

    nproc = size / sizeof(struct kinfo_proc);
    pr = (struct smproc *)malloc(nproc * sizeof(struct smproc));
    if (pr == (struct smproc *)NULL) {
        fprintf(stderr, "Not enough memory for proc table\n");
        exit(1);
    }
    /*
     * Now step thru the kinfo_proc structures and save interesting
     * process's info in the 'smproc' structure.
     */
    smp = pr;
    kp = kpt;
    for (pn = 0; pn < nproc; kp++, pn++) {
        p = &kp->kp_proc;
        /* decide if it's an interesting process */
        if (p->p_stat == 0 || p->p_stat == SZOMB || p->p_pgrp == 0)
            continue;
        if (kp->kp_eproc.e_tdev == NODEV)
            continue;

        /* save the interesting parts */
        smp->w_pid = p->p_pid;
        smp->w_igintr = kp->ki_sigs;
        smp->w_time = kp->ki_utime + kp->ki_stime;
        smp->w_ctime = kp->ki_cutime + kp->ki_cstime;
        smp->w_tty = kp->kp_eproc.e_tdev;
        strncpy(smp->w_comm, kp->ki_comm, sizeof(smp->w_comm) - 1);
        smp->w_comm[sizeof(smp->w_comm) - 1] = '\0';
        strncpy(smp->w_args, smp->w_comm, ARGWIDTH);
        smp->w_args[ARGWIDTH] = '\0';
        smp++;
    }
    np = smp - pr;
    free(kpt);
}
