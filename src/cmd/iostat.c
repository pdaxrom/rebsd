/*
 * iostat
 */
#include <ctype.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/dk.h>
#include <sys/sysctl.h>

char **dr_name;
int *dr_select;
int dk_ndrive;
int ndrives = 0;

struct {
    int dk_busy;
    long cp_time[CPUSTATES];
    long *dk_bytes;
    long *dk_xfer;
    long tk_nin;
    long tk_nout;
} s, s1;

int hz;
double etime;
int tohdr = 1;
static struct kinfo_ucb_stats ucb;

static int read_stats(void);
static void stats(int dn);
static void stat1(int o);

void printhdr(int sig)
{
    int i;

    printf("---tty---");
    for (i = 0; i < dk_ndrive; i++)
        if (dr_select[i])
            printf(" ---%3.3s--", dr_name[i]);
    printf(" ------cpu------\n");

    printf(" tin tout");
    for (i = 0; i < dk_ndrive; i++)
        if (dr_select[i])
            printf(" kbps tps");
    printf("  us  ni  sy  id\n");
    tohdr = 19;
}

int main(int argc, char *argv[])
{
    int i;
    int iter;
    long t;
    char *arg, buf[BUFSIZ];

    if (read_stats() < 0) {
        fprintf(stderr, "iostat: VM_UCBSTATS: %s\n", strerror(errno));
        exit(1);
    }
    iter = 0;
    for (argc--, argv++; argc > 0 && argv[0][0] == '-'; argc--, argv++)
        ;
    dk_ndrive = ucb.kus_dk_ndrive;
    if (dk_ndrive <= 0) {
        printf("dk_ndrive %d\n", dk_ndrive);
        exit(1);
    }
    dr_select = (int *)calloc(dk_ndrive, sizeof(int));
    dr_name = (char **)calloc(dk_ndrive, sizeof(char *));
    s.dk_bytes = (long *)calloc(dk_ndrive, sizeof(long));
    s1.dk_bytes = (long *)calloc(dk_ndrive, sizeof(long));
    s.dk_xfer = (long *)calloc(dk_ndrive, sizeof(long));
    s1.dk_xfer = (long *)calloc(dk_ndrive, sizeof(long));
    for (arg = buf, i = 0; i < dk_ndrive; i++) {
        dr_name[i] = arg;
        sprintf(dr_name[i], "dk%d", i);
        arg += KINFO_DISKNAMELEN;
    }
    for (i = 0; i < dk_ndrive; i++) {
        strncpy(dr_name[i], ucb.kus_dk_name[i], KINFO_DISKNAMELEN - 1);
        dr_name[i][KINFO_DISKNAMELEN - 1] = '\0';
    }
    hz = ucb.kus_hz;

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
        dr_select[i] = 1;
        ndrives++;
    }
    if (argc > 1)
        iter = atoi(argv[1]);
    signal(SIGCONT, printhdr);
loop:
    if (--tohdr == 0)
        printhdr(0);
    if (read_stats() < 0) {
        fprintf(stderr, "iostat: VM_UCBSTATS: %s\n", strerror(errno));
        exit(1);
    }
    s.dk_busy = ucb.kus_dk_busy;
    memcpy(s.dk_xfer, ucb.kus_dk_xfer,
        dk_ndrive * sizeof(s.dk_xfer[0]));
    memcpy(s.dk_bytes, ucb.kus_dk_bytes,
        dk_ndrive * sizeof(s.dk_bytes[0]));
    s.tk_nin = ucb.kus_tk_nin;
    s.tk_nout = ucb.kus_tk_nout;
    memcpy(s.cp_time, ucb.kus_cp_time, sizeof(s.cp_time));

    for (i = 0; i < dk_ndrive; i++) {
        if (!dr_select[i])
            continue;
#define X(fld)             \
    t = s.fld[i];          \
    s.fld[i] -= s1.fld[i]; \
    s1.fld[i] = t
        X(dk_xfer);
        X(dk_bytes);
    }
    t = s.tk_nin;
    s.tk_nin -= s1.tk_nin;
    s1.tk_nin = t;
    t = s.tk_nout;
    s.tk_nout -= s1.tk_nout;
    s1.tk_nout = t;
    etime = 0;
    for (i = 0; i < CPUSTATES; i++) {
        X(cp_time);
        etime += s.cp_time[i];
    }
    if (etime == 0.0)
        etime = 1.0;
    etime /= (float)hz;
    printf("%4.0f%5.0f", s.tk_nin / etime, s.tk_nout / etime);
    for (i = 0; i < dk_ndrive; i++)
        if (dr_select[i])
            stats(i);
    for (i = 0; i < CPUSTATES; i++)
        stat1(i);
    printf("\n");
    fflush(stdout);
    if (--iter && argc > 0) {
        sleep(atoi(argv[0]));
        goto loop;
    }
}

void stats(int dn)
{
    /* number of bytes transferred */
    printf("%5.0f", (double)s.dk_bytes[dn] / 1024 / etime);

    /* number of transfers */
    printf("%4.0f", (double)s.dk_xfer[dn] / etime);
}

void stat1(int o)
{
    int i;
    double time;

    time = 0;
    for (i = 0; i < CPUSTATES; i++)
        time += s.cp_time[i];
    if (time == 0.0)
        time = 1.0;
    printf(" %3.0f", 100.0 * s.cp_time[o] / time);
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
