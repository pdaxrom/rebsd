/*
 * Print live system tables through sysctl.
 *
 * The historical pstat read /dev/kmem and /dev/mem directly.  That made the
 * output depend on a matching namelist and exposed arbitrary kernel memory.
 * Live-system operation now uses bounded kernel snapshots instead.
 */
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/tty.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DTYPE_PIPE 3

static int inof, prcf, ttyf, filf, swpf, totflg, allflg;

static void usage(void);
static void dofile(void);
static void doinode(void);
static void doproc(void);
static void dotty(void);
static void doswap(void);
static void dovm(void);
static void putf(long, char);
static void ttyprt(struct tty *, int);

static void *
snapshot(int *mib, u_int namelen, size_t *sizep)
{
    void *data;
    size_t size;

    size = 0;
    if (sysctl(mib, namelen, NULL, &size, NULL, 0) < 0)
        return NULL;
    data = malloc(size != 0 ? size : 1);
    if (data == NULL)
        return NULL;
    if (sysctl(mib, namelen, data, &size, NULL, 0) < 0) {
        free(data);
        return NULL;
    }
    *sizep = size;
    return data;
}

int
main(int argc, char **argv)
{
    char *argp;

    argc--;
    argv++;
    while (argc > 0 && argv[0][0] == '-') {
        argp = *argv++ + 1;
        argc--;
        while (*argp != '\0') {
            switch (*argp++) {
            case 'T': totflg = 1; break;
            case 'a': allflg = 1; break;
            case 'i': inof = 1; break;
            case 'p': prcf = 1; break;
            case 't': ttyf = 1; break;
            case 'f': filf = 1; break;
            case 's': swpf = 1; break;
            case 'k':
            case 'u':
                fprintf(stderr,
                    "pstat: crash dumps and raw u-area addresses are no "
                    "longer supported\n");
                return 1;
            default:
                usage();
                return 1;
            }
        }
    }
    if (argc != 0) {
        fprintf(stderr, "pstat: live sysctl mode does not accept a core file\n");
        return 1;
    }
    if (!(filf || totflg || inof || prcf || ttyf || swpf))
        filf = 1;
    if (filf || totflg)
        dofile();
    if (inof || totflg)
        doinode();
    if (prcf || totflg)
        doproc();
    if (ttyf)
        dotty();
    if (swpf || totflg)
        doswap();
    if (totflg)
        dovm();
    return 0;
}

static void
usage(void)
{
    fprintf(stderr, "usage: pstat [-aiptfsT]\n");
}

static void
putf(long value, char name)
{
    putchar(value ? name : ' ');
}

static void
doinode(void)
{
    int mib[2] = { CTL_KERN, KERN_INODE };
    struct kinfo_inode *ki;
    struct inode *ip;
    size_t size, count, i;

    ki = snapshot(mib, 2, &size);
    if (ki == NULL) {
        fprintf(stderr, "pstat: KERN_INODE: %s\n", strerror(errno));
        return;
    }
    count = size / sizeof(*ki);
    if (totflg) {
        printf("%3u/%3d inodes\n", (unsigned)count, NINODE);
        free(ki);
        return;
    }
    printf("%u/%d active inodes\n", (unsigned)count, NINODE);
    printf("   LOC       FLAGS      CNT  DEVICE  RDC WRC  INO   MODE  NLK  UID  SIZE/DEV FS\n");
    for (i = 0; i < count; i++) {
        ip = &ki[i].kp_inode;
        printf("%08x ", (unsigned)ki[i].kp_inodep);
        putf(ip->i_flag & ILOCKED, 'L'); putf(ip->i_flag & IUPD, 'U');
        putf(ip->i_flag & IACC, 'A'); putf(ip->i_flag & IMOUNT, 'M');
        putf(ip->i_flag & IWANT, 'W'); putf(ip->i_flag & ITEXT, 'T');
        putf(ip->i_flag & ICHG, 'C'); putf(ip->i_flag & ISHLOCK, 'S');
        putf(ip->i_flag & IEXLOCK, 'E'); putf(ip->i_flag & ILWAIT, 'Z');
        putchar('-'); putf(ip->i_flag & IMOD, 'm');
        putf(ip->i_flag & IRENAME, 'r'); putf(ip->i_flag & IXMOD, 'x');
        printf("%4u%4d,%3d%4u%4u%6lu %7.1o%4u%5u",
            ip->i_count, major(ip->i_dev), minor(ip->i_dev),
            ip->i_shlockc, ip->i_exlockc,
            (unsigned long)ip->i_number, ip->i_mode, ip->i_nlink,
            ip->i_uid);
        if ((ip->i_mode & IFMT) == IFBLK || (ip->i_mode & IFMT) == IFCHR)
            printf("%6d,%3d", major(ip->i_rdev), minor(ip->i_rdev));
        else
            printf("%10ld", (long)ip->i_size);
        printf(" %p\n", ip->i_fs);
    }
    free(ki);
}

static void
dofile(void)
{
    int mib[2] = { CTL_KERN, KERN_FILE };
    struct kinfo_file *kf;
    struct file *fp;
    static const char *types[] = { "???", "inode", "socket", "pipe", "shm" };
    size_t size, count, i;

    kf = snapshot(mib, 2, &size);
    if (kf == NULL) {
        fprintf(stderr, "pstat: KERN_FILE: %s\n", strerror(errno));
        return;
    }
    count = size / sizeof(*kf);
    if (totflg) {
        printf("%3u/%3d files\n", (unsigned)count, NFILE);
        free(kf);
        return;
    }
    printf("%u/%d open files\n", (unsigned)count, NFILE);
    printf("   LOC   TYPE    FLG        CNT  MSG  DATA      OFFSET\n");
    for (i = 0; i < count; i++) {
        fp = &kf[i].kp_file;
        printf("%08x %-8.8s", (unsigned)kf[i].kp_filep,
            fp->f_type >= 0 && fp->f_type < 5 ? types[fp->f_type] : "unknown");
        putf(fp->f_flag & FREAD, 'R'); putf(fp->f_flag & FWRITE, 'W');
        putf(fp->f_flag & FAPPEND, 'A'); putf(fp->f_flag & FSHLOCK, 'S');
        putf(fp->f_flag & FEXLOCK, 'X'); putf(fp->f_flag & FASYNC, 'I');
        putf(fp->f_flag & FNONBLOCK, 'n');
        printf("  %3u  %3d  %p  %lld\n", fp->f_count, fp->f_msgcount,
            fp->f_un.f_Data, (long long)fp->f_offset);
    }
    free(kf);
}

static void
doproc(void)
{
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0 };
    struct kinfo_proc *kp;
    struct proc *pp;
    size_t size, count, i;

    kp = snapshot(mib, 4, &size);
    if (kp == NULL) {
        fprintf(stderr, "pstat: KERN_PROC_ALL: %s\n", strerror(errno));
        return;
    }
    count = size / sizeof(*kp);
    if (totflg) {
        printf("%3u/%3d processes\n", (unsigned)count, NPROC);
        free(kp);
        return;
    }
    printf("%u/%d active processes%s\n", (unsigned)count, NPROC,
        allflg ? " (unused slots are not exported)" : "");
    printf("   LOC    S       F PRI      SIG   UID SLP TIM  CPU  NI   PGRP    PID   PPID     ADDR    SADDR    DADDR     SIZE   WCHAN    LINK     SIGM COMMAND\n");
    for (i = 0; i < count; i++) {
        pp = &kp[i].kp_proc;
        printf("%08x %2d %7.1x %3d %8.1lx %5u %3d %3d %4d %3d "
            "%6d %6d %6d %8x %8x %8x %6x %p %p %8.1lx %s\n",
            (unsigned)kp[i].kp_eproc.e_paddr, pp->p_stat, pp->p_flag,
            pp->p_pri, pp->p_sig, pp->p_uid, pp->p_slptime, pp->p_time,
            pp->p_cpu & 0377, pp->p_nice, pp->p_pgrp, pp->p_pid,
            pp->p_ppid, (unsigned)pp->p_addr, (unsigned)pp->p_saddr,
            (unsigned)pp->p_daddr, (unsigned)(pp->p_dsize + pp->p_ssize),
            pp->p_wchan, pp->p_link, pp->p_sigmask, kp[i].ki_comm);
    }
    free(kp);
}

static void
dotty(void)
{
    int mib[2] = { CTL_KERN, KERN_TTY };
    struct tty tty;
    size_t size;

    size = sizeof(tty);
    if (sysctl(mib, 2, &tty, &size, NULL, 0) < 0 || size != sizeof(tty)) {
        fprintf(stderr, "pstat: KERN_TTY: %s\n", strerror(errno));
        return;
    }
    printf("cn line\n");
    printf(" # RAW CAN OUT         MODE     ADDR  DEL  COL     STATE       PGRP\n");
    ttyprt(&tty, 0);
}

static void
ttyprt(struct tty *tp, int line)
{
    printf("%2d%4d%4d%4d %12.1lo %p %4d %4d ", line,
        tp->t_rawq.c_cc, tp->t_canq.c_cc, tp->t_outq.c_cc,
        tp->t_flags, tp->t_addr, tp->t_delct, tp->t_col);
    putf(tp->t_state & TS_TIMEOUT, 'T'); putf(tp->t_state & TS_WOPEN, 'W');
    putf(tp->t_state & TS_ISOPEN, 'O'); putf(tp->t_state & TS_FLUSH, 'F');
    putf(tp->t_state & TS_CARR_ON, 'C'); putf(tp->t_state & TS_BUSY, 'B');
    putf(tp->t_state & TS_ASLEEP, 'A'); putf(tp->t_state & TS_XCLUDE, 'X');
    putf(tp->t_state & TS_TTSTOP, 'S'); putf(tp->t_state & TS_HUPCLS, 'H');
    putf(tp->t_state & TS_TBLOCK, 'b'); putf(tp->t_state & TS_RCOLL, 'r');
    putf(tp->t_state & TS_WCOLL, 'w'); putf(tp->t_state & TS_ASYNC, 'a');
    printf("%6d\n", tp->t_pgrp);
}

static void
doswap(void)
{
    int totalmib[2] = { CTL_VM, VM_SWAPTOTAL };
    int freemib[2] = { CTL_VM, VM_SWAPFREE };
    long totalbytes;
    long freebytes;
    size_t totalsize;

    totalsize = sizeof(totalbytes);
    if (sysctl(totalmib, 2, &totalbytes, &totalsize, NULL, 0) < 0) {
        fprintf(stderr, "pstat: VM_SWAPTOTAL: %s\n", strerror(errno));
        return;
    }
    totalsize = sizeof(freebytes);
    if (sysctl(freemib, 2, &freebytes, &totalsize, NULL, 0) < 0) {
        fprintf(stderr, "pstat: VM_SWAPFREE: %s\n", strerror(errno));
        return;
    }
    printf("%lu kbytes swap used, %lu kbytes free\n",
        (unsigned long)((totalbytes - freebytes) / 1024),
        (unsigned long)(freebytes / 1024));
}

static int
vmvalue(int leaf, long *value)
{
    int mib[2] = { CTL_VM, leaf };
    size_t size = sizeof(*value);

    return sysctl(mib, 2, value, &size, NULL, 0) == 0 &&
        size == sizeof(*value) ? 0 : -1;
}

static void
dovm(void)
{
    long total, freep, reserved, bad, objects, anon, resident, swapped;
    long mappings, pmapresident, faults, waits, wouldblock, attempts, failures;
    long shmobjects, shmmappings;

    if (vmvalue(VM_PHYSPAGES, &total) || vmvalue(VM_FREEPAGES, &freep) ||
        vmvalue(VM_RESERVEDPAGES, &reserved) || vmvalue(VM_BADPAGES, &bad) ||
        vmvalue(VM_OBJECTS, &objects) || vmvalue(VM_ANONPAGES, &anon) ||
        vmvalue(VM_OBJECTRESIDENT, &resident) ||
        vmvalue(VM_OBJECTSWAPPED, &swapped) ||
        vmvalue(VM_PMAPMAPPINGS, &mappings) ||
        vmvalue(VM_PMAPRESIDENT, &pmapresident) ||
        vmvalue(VM_OBJECTFAULTS, &faults) || vmvalue(VM_OBJECTWAITS, &waits) ||
        vmvalue(VM_FAULTWOULDBLOCK, &wouldblock) ||
        vmvalue(VM_RECLAIMATTEMPTS, &attempts) ||
        vmvalue(VM_RECLAIMFAILURES, &failures) ||
        vmvalue(VM_SHMOBJECTS, &shmobjects) ||
        vmvalue(VM_SHMMAPPINGS, &shmmappings)) {
        fprintf(stderr, "pstat: VM statistics are unavailable\n");
        return;
    }
    printf("%ld/%ld physical pages free, %ld reserved, %ld bad\n",
        freep, total, reserved, bad);
    printf("%ld VM objects, %ld anon, %ld resident, %ld swapped\n",
        objects, anon, resident, swapped);
    printf("%ld pmap mappings, %ld resident mappings\n", mappings,
        pmapresident);
    printf("%ld object faults, %ld waits, %ld nowait rejects\n", faults,
        waits, wouldblock);
    printf("%ld reclaim attempts, %ld failures\n", attempts, failures);
    printf("%ld shared-memory objects, %ld current-process mappings\n",
        shmobjects, shmmappings);
}
