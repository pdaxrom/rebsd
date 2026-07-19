/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <sys/dir.h>
#include <sys/dk.h>
#include <sys/file.h>
#include <sys/namei.h>
#include <sys/sysctl.h>
#include <sys/vm.h>

size_t pfree;
int pflag;
char **dr_name;
int *dr_select;
int dk_ndrive;
int ndrives = 0;
char *defdrives[] = { "sd0", 0 };
static long stat1(int row);
int hz;

struct {
    int busy;
    long time[CPUSTATES];
    long *xfer;

    struct vmrate Rate;
    struct vmtotal Total;
    struct vmsum Sum;

    struct forkstat Forkstat;
    unsigned rectime;
    unsigned pgintime;
} s, s1;

#define rate s.Rate
#define total s.Total
#define sum s.Sum
#define forkstat s.Forkstat

long etime;
time_t now, boottime;
int lines = 1;
static struct kinfo_ucb_stats ucb;

static void dotimes(void);
static void doforkst(void);
static void dosum(void);
static int read_stats(void);
static int reset_stats(void);
static void dointr(long nintv);
static void dovmstats(void);
static void stats(int dn);

struct vmstat_sysctl {
    int leaf;
    char *description;
};

static struct vmstat_sysctl vmstat_sysctls[] = {
    { VM_PHYSPAGES, "physical pages" },
    { VM_FREEPAGES, "free pages" },
    { VM_RESERVEDPAGES, "reserved pages" },
    { VM_BADPAGES, "bad pages" },
    { VM_PAGEALLOCS, "page allocations" },
    { VM_PAGEFREES, "page frees" },
    { VM_PAGEFAILURES, "page allocation failures" },
    { VM_PAGEPOISONFAILURES, "page poison failures" },
    { VM_PMAPMAPPINGS, "pmap mappings" },
    { VM_PMAPRESIDENT, "pmap resident pages" },
    { VM_PMAPREFILLS, "TLB refills" },
    { VM_PMAPMODIFIED, "first-write TLB updates" },
    { VM_PMAPFAULTS, "pmap protection faults" },
    { VM_PMAPTARGETED, "targeted TLB invalidations" },
    { VM_PMAPFLUSHES, "full TLB flushes" },
    { VM_PMAPROLLOVERS, "ASID rollovers" },
    { VM_OBJECTS, "VM objects" },
    { VM_ANONPAGES, "anonymous page descriptors" },
    { VM_OBJECTRESIDENT, "resident object pages" },
    { VM_OBJECTSWAPPED, "swapped object pages" },
    { VM_OBJECTFAULTS, "object fault resolutions" },
    { VM_OBJECTWAITS, "busy object page waits" },
    { VM_FAULTWOULDBLOCK, "non-sleeping faults rejected" },
    { VM_ZEROFAULTS, "demand-zero faults" },
    { VM_COWFAULTS, "copy-on-write faults" },
    { VM_PAGEINS, "pager page-ins" },
    { VM_PAGEOUTS, "pager page-outs" },
    { VM_SWAPFAILURES, "pager I/O failures" },
    { VM_RECLAIMATTEMPTS, "reclaim attempts" },
    { VM_RECLAIMFAILURES, "reclaim failures" },
    { VM_SHMOBJECTS, "shared-memory objects" },
    { VM_SHMPAGES, "shared-memory logical pages" },
    { VM_SHMMAPPINGS, "current-process shared mappings" },
    { VM_SYSVSEGMENTS, "System V shared segments" },
    { VM_SYSVATTACHMENTS, "System V attachments" },
};

void printhdr(int sig)
{
    int i;

    if (pflag)
        printf("-procs- -----memory----- -swap- ");
    else
        printf("-procs- ---memory-- ");

    printf("-----disks----- ");

    if (pflag) {
        printf("-----faults----- ------cpu------\n");
        printf(" r b w    avm  tx   fre   i  o  ");
    } else {
        printf("---faults--- ----cpu----\n");
        printf(" r b w    avm   fre ");
    }

    for (i = 0; i < dk_ndrive; i++)
        if (dr_select[i])
            printf("%c%c%c ", dr_name[i][0], dr_name[i][1], dr_name[i][2]);
    printf("  in  sy");
    if (pflag)
        printf("  tr");
    printf("  cs  us");
    if (pflag)
        printf("  ni");
    printf("  sy  id\n");
    lines = 19;
}

int main(int argc, char **argv)
{
    int i;
    int iter, iflag = 0;
    long nintv, t;
    char *arg, **cp, buf[BUFSIZ];

    iter = 0;
    argc--, argv++;
    while (argc > 0 && argv[0][0] == '-') {
        char *cp = *argv++;
        argc--;
        while (*++cp)
            switch (*cp) {
            case 't':
                dotimes();
                exit(0);

            case 'z':
                if (reset_stats() < 0) {
                    fprintf(stderr, "vmstat: reset: %s\n",
                        strerror(errno));
                    exit(1);
                }
                exit(0);

            case 'f':
                doforkst();
                exit(0);

            case 's':
                dosum();
                exit(0);

            case 'i':
                iflag++;
                break;

            case 'p':
                pflag++;
                break;

            default:
                fprintf(stderr, "usage: vmstat [ -fsiptz ] [ interval ] [ count]\n");
                exit(1);
            }
    }
    if (read_stats() < 0) {
        fprintf(stderr, "vmstat: VM_UCBSTATS: %s\n", strerror(errno));
        exit(1);
    }
    boottime = ucb.kus_boottime;
    hz = ucb.kus_hz;
    dk_ndrive = ucb.kus_dk_ndrive;
    if (dk_ndrive <= 0) {
        fprintf(stderr, "dk_ndrive %d\n", dk_ndrive);
        exit(1);
    }
    dr_select = (int *)calloc(dk_ndrive, sizeof(int));
    dr_name = (char **)calloc(dk_ndrive, sizeof(char *));
#define allocate(e, t)                            \
    s./**/ e = (t *)calloc(dk_ndrive, sizeof(t)); \
    s1./**/ e = (t *)calloc(dk_ndrive, sizeof(t));
    allocate(xfer, long);
    for (arg = buf, i = 0; i < dk_ndrive; i++) {
        dr_name[i] = arg;
        strncpy(dr_name[i], ucb.kus_dk_name[i], KINFO_DISKNAMELEN - 1);
        dr_name[i][KINFO_DISKNAMELEN - 1] = '\0';
        arg += KINFO_DISKNAMELEN;
    }
    time(&now);
    nintv = now - boottime;
    if (nintv <= 0 || nintv > 60L * 60L * 24L * 365L * 10L) {
        fprintf(stderr, "vmstat: kernel boot time makes no sense\n");
        exit(1);
    }
    if (iflag) {
        dointr(nintv);
        exit(0);
    }
    /*
     * Choose drives to be displayed.  Priority
     * goes to (in order) drives supplied as arguments,
     * default drives.  If everything isn't filled
     * in and there are drives not taken care of,
     * display the first few that fit.
     */
    ndrives = 0;
    while (argc > 0 && !isdigit(argv[0][0])) {
        for (i = 0; i < dk_ndrive; i++) {
            if (strcmp(dr_name[i], argv[0]))
                continue;
            dr_select[i] = 1;
            ndrives++;
        }
        argc--, argv++;
    }
    for (i = 0; i < dk_ndrive && ndrives < 4; i++) {
        if (dr_select[i])
            continue;
        for (cp = defdrives; *cp; cp++)
            if (strcmp(dr_name[i], *cp) == 0) {
                dr_select[i] = 1;
                ndrives++;
                break;
            }
    }
    for (i = 0; i < dk_ndrive && ndrives < 4; i++) {
        if (dr_select[i])
            continue;
        dr_select[i] = 1;
        ndrives++;
    }
    if (argc > 1)
        iter = atoi(argv[1]);
    signal(SIGCONT, printhdr);
loop:
    if (--lines == 0)
        printhdr(0);
    if (read_stats() < 0) {
        fprintf(stderr, "vmstat: VM_UCBSTATS: %s\n", strerror(errno));
        exit(1);
    }
    memcpy(s.time, ucb.kus_cp_time, sizeof(s.time));
    memcpy(s.xfer, ucb.kus_dk_xfer,
        dk_ndrive * sizeof(s.xfer[0]));

    if (nintv != 1) {
        sum = ucb.kus_sum;
        rate.v_swtch = sum.v_swtch;
        rate.v_trap = sum.v_trap;
        rate.v_syscall = sum.v_syscall;
        rate.v_intr = sum.v_intr;
        rate.v_swpin = sum.v_swpin;
        rate.v_swpout = sum.v_swpout;
    } else {
        rate = ucb.kus_rate;
        sum = ucb.kus_sum;
    }
    pfree = ucb.kus_freemem;
    total = ucb.kus_total;
    etime = 0;
    for (i = 0; i < dk_ndrive; i++) {
        t = s.xfer[i];
        s.xfer[i] -= s1.xfer[i];
        s1.xfer[i] = t;
    }
    for (i = 0; i < CPUSTATES; i++) {
        t = s.time[i];
        s.time[i] -= s1.time[i];
        s1.time[i] = t;
        etime += s.time[i];
    }
    if (etime == 0.)
        etime = 1.;

    printf("%2d%2d%2d", total.t_rq, total.t_dw, total.t_sw);
    /*
     * We don't use total.t_free because it slops around too much
     * within this kernel
     */
    printf("%7ld", total.t_avm);
    if (pflag)
        printf("%4ld", total.t_avm ? (total.t_avmtxt * 100) / total.t_avm : 0);
    printf("%6d", pfree);

    if (pflag) {
        printf("%4ld%3ld ", rate.v_swpin / nintv, rate.v_swpout / nintv);
    }

    for (i = 0; i < dk_ndrive; i++) {
        if (dr_select[i])
            stats(i);
    }
    printf("%5ld%4ld", rate.v_intr / nintv, rate.v_syscall / nintv);
    if (pflag)
        printf("%4ld", rate.v_trap / nintv);
    printf("%4ld", rate.v_swtch / nintv);

    for (i = 0; i < CPUSTATES; i++) {
        long f = stat1(i);
        if (!pflag && i == 0) { /* US+NI */
            i++;
            f += stat1(i);
        }
        printf(" %3ld", f);
    }
    printf("\n");
    fflush(stdout);
    nintv = 1;
    if (--iter && argc > 0) {
        sleep(atoi(argv[0]));
        goto loop;
    }
}

void dotimes()
{
    printf("page in/out/reclamation is not applicable to 2.11BSD\n");
}

void dosum()
{
    if (read_stats() < 0) {
        fprintf(stderr, "vmstat: VM_UCBSTATS: %s\n", strerror(errno));
        exit(1);
    }
    sum = ucb.kus_sum;
    printf("%9ld swap ins\n", sum.v_swpin);
    printf("%9ld swap outs\n", sum.v_swpout);
    printf("%9ld kbytes swapped in\n", sum.v_kbin);
    printf("%9ld kbytes swapped out\n", sum.v_kbout);
    printf("%9ld cpu context switches\n", sum.v_swtch);
    printf("%9ld device interrupts\n", sum.v_intr);
    printf("%9ld software interrupts\n", sum.v_soft);
    printf("%9ld traps\n", sum.v_trap);
    printf("%9ld system calls\n", sum.v_syscall);
    dovmstats();
}

static void
dovmstats(void)
{
    int mib[2];
    long value;
    size_t size;
    unsigned i;

    mib[0] = CTL_VM;
    for (i = 0; i < sizeof(vmstat_sysctls) / sizeof(vmstat_sysctls[0]);
        ++i) {
        mib[1] = vmstat_sysctls[i].leaf;
        size = sizeof(value);
        if (sysctl(mib, 2, &value, &size, NULL, 0) < 0 ||
            size != sizeof(value)) {
            fprintf(stderr, "vmstat: vm sysctl %d is unavailable\n",
                mib[1]);
            continue;
        }
        printf("%9ld %s\n", value, vmstat_sysctls[i].description);
    }
}

void doforkst()
{
    long avg;

    if (read_stats() < 0) {
        fprintf(stderr, "vmstat: VM_UCBSTATS: %s\n", strerror(errno));
        exit(1);
    }
    forkstat = ucb.kus_forkstat;
    if (forkstat.cntfork != 0) {
        avg = forkstat.sizfork * 100 / forkstat.cntfork;
        printf("%ld forks, %ld kbytes, average=%ld.%02ld\n",
               forkstat.cntfork, forkstat.sizfork, avg / 100, avg % 100);
    }
    if (forkstat.cntvfork != 0) {
        avg = forkstat.sizvfork * 100 / forkstat.cntvfork;
        printf("%ld vforks, %ld kbytes, average=%ld.%02ld\n",
               forkstat.cntvfork, forkstat.sizvfork, avg / 100, avg % 100);
    }
}

void stats(int dn)
{
    long xfer;

    if (dn >= dk_ndrive) {
        printf("   0");
        return;
    }
    xfer = (etime > 0) ? (s.xfer[dn] * hz + etime / 2) / etime : 0;
    printf("%4ld", xfer);
}

static long stat1(int row)
{
    long t;
    int i;

    t = 0;
    for (i = 0; i < CPUSTATES; i++)
        t += s.time[i];
    if (t == 0)
        t = 1;
    return (s.time[row] * 100 + t / 2) / t;
}

void dointr(long nintv)
{
    printf("Device interrupt statistics are not applicable to 2.11BSD\n");
}

static int
read_stats(void)
{
    int mib[2];
    size_t size;

    mib[0] = CTL_VM;
    mib[1] = VM_UCBSTATS;
    size = sizeof(ucb);
    if (sysctl(mib, 2, &ucb, &size, NULL, 0) < 0)
        return (-1);
    if (size != sizeof(ucb)) {
        errno = EINVAL;
        return (-1);
    }
    return (0);
}

static int
reset_stats(void)
{
    int mib[2];
    int reset;

    mib[0] = CTL_VM;
    mib[1] = VM_UCBRESET;
    reset = 1;
    return sysctl(mib, 2, NULL, NULL, &reset, sizeof(reset));
}
