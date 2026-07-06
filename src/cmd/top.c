/*
 * Small sysctl based process monitor.
 */
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/vm.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const char *
state_name(int state)
{
    switch (state) {
    case SSLEEP:
        return "sleep";
    case SRUN:
        return "run";
    case SIDL:
        return "idle";
    case SZOMB:
        return "zomb";
    case SSTOP:
        return "stop";
    default:
        return "-";
    }
}

static int
proc_compare(const void *av, const void *bv)
{
    const struct kinfo_proc *a = av;
    const struct kinfo_proc *b = bv;
    int acpu = a->kp_proc.p_cpu & 0377;
    int bcpu = b->kp_proc.p_cpu & 0377;

    if (acpu != bcpu)
        return bcpu - acpu;
    return a->kp_proc.p_pid - b->kp_proc.p_pid;
}

static struct kinfo_proc *
read_procs(size_t *countp)
{
    int mib[4];
    struct kinfo_proc *procs;
    size_t size;

    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC;
    mib[2] = KERN_PROC_ALL;
    mib[3] = 0;
    size = 0;
    if (sysctl(mib, 4, NULL, &size, NULL, 0) < 0) {
        fprintf(stderr, "top: kern.proc size failed");
        if (errno)
            fprintf(stderr, ": %s", strerror(errno));
        fprintf(stderr, "\n");
        exit(1);
    }
    if (size % sizeof(*procs) != 0) {
        fprintf(stderr, "top: proc size mismatch (%u total, %u chunks)\n",
            (unsigned)size, (unsigned)sizeof(*procs));
        exit(1);
    }
    procs = malloc(size);
    if (procs == NULL) {
        fprintf(stderr, "top: out of memory\n");
        exit(1);
    }
    if (sysctl(mib, 4, procs, &size, NULL, 0) < 0) {
        fprintf(stderr, "top: kern.proc fetch failed");
        if (errno)
            fprintf(stderr, ": %s", strerror(errno));
        fprintf(stderr, " (size %u)\n", (unsigned)size);
        free(procs);
        exit(1);
    }
    if (size % sizeof(*procs) != 0) {
        fprintf(stderr, "top: proc size mismatch (%u total, %u chunks)\n",
            (unsigned)size, (unsigned)sizeof(*procs));
        free(procs);
        exit(1);
    }
    *countp = size / sizeof(*procs);
    return procs;
}

static long
sysctl_long2(int top, int leaf)
{
    int mib[2];
    long value;
    size_t size;

    mib[0] = top;
    mib[1] = leaf;
    size = sizeof(value);
    if (sysctl(mib, 2, &value, &size, NULL, 0) < 0)
        return 0;
    return value;
}

static void
read_vmtotal(struct vmtotal *total)
{
    int mib[2];
    size_t size;

    memset(total, 0, sizeof(*total));
    mib[0] = CTL_VM;
    mib[1] = VM_METER;
    size = sizeof(*total);
    (void)sysctl(mib, 2, total, &size, NULL, 0);
}

static void
print_load(void)
{
    unsigned load[3];

    if (getloadavg(load, 3) == 3)
        printf("load %u.%02u %u.%02u %u.%02u",
            load[0] / 100, load[0] % 100,
            load[1] / 100, load[1] % 100,
            load[2] / 100, load[2] % 100);
    else
        printf("load -");
}

static void
show_top(int lines)
{
    struct kinfo_proc *procs;
    struct vmtotal total;
    time_t now;
    struct tm *tm;
    size_t count, i, shown;
    long user_kb, used_kb, free_kb;

    procs = read_procs(&count);
    qsort(procs, count, sizeof(*procs), proc_compare);
    read_vmtotal(&total);
    user_kb = sysctl_long2(CTL_HW, HW_USERMEM) / 1024;
    used_kb = total.t_vm / DEV_BSIZE;
    if (used_kb > user_kb)
        used_kb = user_kb;
    free_kb = user_kb - used_kb;
    time(&now);
    tm = localtime(&now);

    if (tm)
        printf("%02d:%02d  ", tm->tm_hour, tm->tm_min);
    print_load();
    printf("  mem %ldK total %ldK used %ldK free  procs %u\n",
        user_kb, used_kb, free_kb, (unsigned)count);
    printf("  PID  PPID UID STAT PRI NI CPU SIZE  RSS COMMAND\n");

    shown = 0;
    for (i = 0; i < count && shown < (size_t)lines; i++) {
        struct proc *p = &procs[i].kp_proc;
        long size_kb, rss_kb;
        const char *comm = procs[i].ki_comm;

        if (p->p_stat == 0)
            continue;
        size_kb = (p->p_dsize + p->p_ssize + USIZE) / DEV_BSIZE;
        rss_kb = (p->p_flag & SLOAD) ? size_kb : 0;
        if (comm[0] == '\0')
            comm = "?";
        printf("%5d %5d %3d %-5s %3d %2d %3d %4ld %4ld %s\n",
            p->p_pid, p->p_ppid, procs[i].kp_eproc.e_ruid,
            state_name(p->p_stat), p->p_pri, p->p_nice,
            p->p_cpu & 0377, size_kb, rss_kb, comm);
        shown++;
    }
    free(procs);
}

int
main(int argc, char **argv)
{
    int ch, count, delay, lines, iter;

    count = 0;
    delay = 5;
    lines = 15;
    while ((ch = getopt(argc, argv, "d:l:n:")) != EOF) {
        switch (ch) {
        case 'd':
            delay = atoi(optarg);
            if (delay <= 0)
                delay = 1;
            break;
        case 'l':
            lines = atoi(optarg);
            if (lines <= 0)
                lines = 15;
            break;
        case 'n':
            count = atoi(optarg);
            if (count < 0)
                count = 0;
            break;
        default:
            fprintf(stderr, "usage: top [-d seconds] [-l lines] [-n count]\n");
            return 1;
        }
    }

    iter = 0;
    do {
        if (count != 1)
            printf("\033[H\033[J");
        show_top(lines);
        fflush(stdout);
        iter++;
        if (count != 0 && iter >= count)
            break;
        sleep(delay);
    } while (1);
    return 0;
}
