/*
 * Copyright (c) 1980, 1990, 1993, 1994
 *  The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *  This product includes software developed by the University of
 *  California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/file.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

int bread(off_t off, void *buf, register int cnt);
char *getmntpt(char *name);
void prtstat(register struct statfs *sfsp, register int maxwidth);
void ufs_df(char *file, int maxwidth);
const char *fstypename(int type);
void humanize(char *buf, unsigned long long bytes);
void     usage();

int iflag;
int tflag;
int hflag;

int
main(
    int argc,
    register char *argv[])
{
    struct stat stbuf;
    struct statfs statfsbuf, *mntbuf;
    int mntsize;
    int ch, err, i, maxwidth, width;
    char *mntpt;

    while ((ch = getopt(argc, argv, "hiT")) != EOF)
        switch (ch) {
        case 'h':
            hflag = 1;
            break;
        case 'i':
            iflag = 1;
            break;
        case 'T':
            tflag = 1;
            break;
        case '?':
        default:
            usage();
        }
    argc -= optind;
    argv += optind;

    mntsize = getmntinfo(&mntbuf, MNT_NOWAIT);
    maxwidth = 0;
    for (i = 0; i < mntsize; i++) {
        width = strlen(mntbuf[i].f_mntfromname);
        if (width > maxwidth)
            maxwidth = width;
    }

    if (!*argv) {
        for (i = 0; i < mntsize; i++)
            prtstat(&mntbuf[i], maxwidth);
        exit(0);
    }

    for (; *argv; argv++) {
        if (stat(*argv, &stbuf) < 0) {
            err = errno;
            if ((mntpt = getmntpt(*argv)) == 0) {
                warn("%s", *argv);
                continue;
            }
        } else if ((stbuf.st_mode & S_IFMT) == S_IFCHR) {
            ufs_df(*argv, maxwidth);
            continue;
        } else if ((stbuf.st_mode & S_IFMT) == S_IFBLK) {
            if ((mntpt = getmntpt(*argv)) == 0) {
                ufs_df(*argv, maxwidth);
                continue;
            }
        } else
            mntpt = *argv;
        /*
         * Statfs does not take a `wait' flag, so we cannot
         * implement nflag here.
         */
        if (statfs(mntpt, &statfsbuf) < 0) {
            warn("%s", mntpt);
            continue;
        }
        if (argc == 1)
            maxwidth = strlen(statfsbuf.f_mntfromname) + 1;
        prtstat(&statfsbuf, maxwidth);
    }
    return (0);
}

char *
getmntpt(
    char *name)
{
    register int i;
    int mntsize;
    struct statfs *mntbuf;

    mntsize = getmntinfo(&mntbuf, MNT_NOWAIT);
    for (i = 0; i < mntsize; i++) {
        if (!strcmp(mntbuf[i].f_mntfromname, name))
            return (mntbuf[i].f_mntonname);
    }
    return (0);
}

/*
 * Convert statfs returned filesystem size into BLOCKSIZE units.
 * Attempts to avoid overflow for large filesystems.
 */
long
fsbtoblk(
    long    num,
    register int    fsbs,
    int bs)
{
    return((fsbs != 0 && fsbs < bs) ?
        num / (bs / fsbs) : (num) * (fsbs / bs));
}

/*
 * Format a byte count using binary units.  Keep the result compact enough
 * for df's fixed-width columns.
 */
void
humanize(
    char *buf,
    unsigned long long bytes)
{
    static const char units[] = "BKMGTPE";
    unsigned long long value;
    unsigned long remainder;
    unsigned unit;
    unsigned tenth;

    value = bytes;
    remainder = 0;
    unit = 0;
    while (value >= 1024 && unit < sizeof(units) - 2) {
        remainder = (unsigned long)(value % 1024);
        value /= 1024;
        unit++;
    }
    if (unit != 0 && value < 10 && remainder != 0) {
        tenth = (remainder * 10 + 512) / 1024;
        if (tenth == 10) {
            value++;
            tenth = 0;
        }
        if (tenth != 0) {
            sprintf(buf, "%lu.%u%c", (unsigned long)value, tenth,
                units[unit]);
            return;
        }
    } else if (remainder >= 512) {
        value++;
    }
    sprintf(buf, "%lu%c", (unsigned long)value, units[unit]);
}

/*
 * Print out status about a filesystem.
 */
void
prtstat(
    register struct statfs *sfsp,
    register int maxwidth)
{
    static int blocksize;
    static int headerlen, timesthrough;
    static char *header;
    long used, availblks;
    char sizebuf[12], usedbuf[12], availbuf[12];
    ino_t   inodes, iused;

    if (maxwidth < 11)
        maxwidth = 11;
    if (++timesthrough == 1) {
/*      header = getbsize(&headerlen, &blocksize); */
        header = hflag ? "Size" : "1K-blocks";
        blocksize = 1024;
        headerlen = hflag ? 6 : 9;

        (void)printf("%-*.*s ", maxwidth, maxwidth, "Filesystem");
        if (tflag)
            (void)printf("%-6s ", "Type");
        if (hflag)
            (void)printf("%6s %6s %6s Capacity", header, "Used",
                "Avail");
        else
            (void)printf("%s     Used    Avail Capacity", header);
        if (iflag)
            (void)printf(" iused   ifree  %%iused");
        (void)printf("  Mounted on\n");
    }
    (void)printf("%-*.*s", maxwidth, maxwidth, sfsp->f_mntfromname);
    if (tflag)
        (void)printf(" %-6s", fstypename(sfsp->f_type));
    used = sfsp->f_blocks - sfsp->f_bfree;
    availblks = sfsp->f_bavail + used;
    if (hflag) {
        humanize(sizebuf, (unsigned long long)sfsp->f_blocks *
            sfsp->f_bsize);
        humanize(usedbuf, (unsigned long long)used * sfsp->f_bsize);
        humanize(availbuf, (unsigned long long)sfsp->f_bavail *
            sfsp->f_bsize);
        (void)printf(" %6s %6s %6s", sizebuf, usedbuf, availbuf);
    } else {
        (void)printf(" %*ld %8ld %8ld", headerlen,
            fsbtoblk(sfsp->f_blocks, sfsp->f_bsize, blocksize),
            fsbtoblk(used, sfsp->f_bsize, blocksize),
            fsbtoblk(sfsp->f_bavail, sfsp->f_bsize, blocksize));
    }
    (void)printf(" %5lu%%",
        availblks == 0 ? 100 : (unsigned long)used * 100 / availblks);
    if (iflag) {
        inodes = sfsp->f_files;
        iused = inodes - sfsp->f_ffree;
        (void)printf(" %7u %7u %5u%% ", iused, sfsp->f_ffree,
           inodes == 0 ? 100 : iused * 100 / inodes);
    } else
        (void)printf("  ");
    (void)printf("  %s\n", sfsp->f_mntonname);
}

const char *
fstypename(int type)
{
    static const char *names[] = INITMOUNTNAMES;

    if (type >= 0 && type <= MOUNT_MAXTYPE && names[type] != 0)
        return names[type];
    return "unknown";
}

/*
 * This code constitutes the pre-system call Berkeley df code for extracting
 * information from filesystem superblocks.
 */
#include <sys/fs.h>
#include <fstab.h>

union {
    struct fs fs;
    char dummy[SBSIZE];
} sb;
#define sblock sb.fs

int rfd;

void
ufs_df(
    char *file,
    int maxwidth)
{
    struct statfs statfsbuf;
    register struct statfs *sfsp;
    register char *mntpt;
    static int synced;

    if (synced++ == 0)
        sync();

    if ((rfd = open(file, O_RDONLY)) < 0) {
        warn("%s", file);
        return;
    }
    if (bread((off_t)SUPERB * DEV_BSIZE, &sblock, SBSIZE) == 0) {
        (void)close(rfd);
        return;
    }
    sfsp = &statfsbuf;
    sfsp->f_type = MOUNT_UFS;
    sfsp->f_flags = 0;
    sfsp->f_bsize = MAXBSIZE;
    sfsp->f_iosize = MAXBSIZE;
    sfsp->f_blocks = sblock.fs_fsize - sblock.fs_isize;
    sfsp->f_bfree = sblock.fs_tfree;
    sfsp->f_bavail = sblock.fs_tfree;
    if (sfsp->f_bavail < 0)
        sfsp->f_bavail = 0;
    sfsp->f_files =  (sblock.fs_isize - 2) * INOPB;
    sfsp->f_ffree = sblock.fs_tinode;
    sfsp->f_fsid[0] = 0;
    sfsp->f_fsid[1] = 0;
    if ((mntpt = getmntpt(file)) == 0)
        mntpt = "";
    bcopy(mntpt, &sfsp->f_mntonname[0], MNAMELEN);
    bcopy(file, &sfsp->f_mntfromname[0], MNAMELEN);
    prtstat(sfsp, maxwidth);
    (void)close(rfd);
}

int
bread(
    off_t off,
    void *buf,
    register int cnt)
{
    register int nr;

    (void)lseek(rfd, off, L_SET);
    if ((nr = read(rfd, buf, cnt)) != cnt) {
        /* Probably a dismounted disk if errno == EIO. */
        if (errno != EIO)
            (void)fprintf(stderr, "\ndf: %ld: %s\n",
                off, strerror(nr > 0 ? EIO : errno));
        return (0);
    }
    return (1);
}

void
usage()
{
    (void)fprintf(stderr, "usage: df [-hiT] [file | file_system ...]\n");
    exit(1);
}
