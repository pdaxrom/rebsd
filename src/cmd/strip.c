/*
 * Copyright (c) 1983 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#ifdef CROSS
#   include </usr/include/stdio.h>
#else
#   include <stdio.h>
#endif
#include <a.out.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include "aoutio.h"

struct  exec head;
int status;

void
strip(char *name)
{
    register int f = -1;
    long size;

    f = open(name, O_RDWR);
    if (f < 0) {
        fprintf(stderr, "strip: "); perror(name);
        status = 1;
        goto out;
    }
    if (!aout_read_exec_fd(f, &head) || N_BADMAG(head)) {
        printf("strip: %s not in a.out format\n", name);
        status = 1;
        goto out;
    }
    if (head.a_syms == 0 && (head.a_magic) != RMAGIC)
        goto out;

    size = N_DATOFF(head) + head.a_data;
    if (ftruncate(f, size) < 0) {
        fprintf(stderr, "strip: ");
        perror(name);
        status = 1;
        goto out;
    }
    head.a_midmag = OMAGIC;
    head.a_reltext = 0;
    head.a_reldata = 0;
    head.a_syms = 0;
    (void) lseek(f, (off_t)0, SEEK_SET);
    if (!aout_write_exec_fd(f, &head))
            /* ignore */;
out:
    if (f >= 0)
        close(f);
}

int
main(int argc, char *argv[])
{
    register int i;

#ifdef TARGET_BIG_ENDIAN
    aout_set_big_endian(1);
#else
    aout_set_big_endian(0);
#endif

    while ((i = getopt(argc, argv, "hE:")) != EOF) {
        switch(i) {
        case 'E':
            if (optarg[0] == 'L' && optarg[1] == 0)
                aout_set_big_endian(0);
            else if (optarg[0] == 'B' && optarg[1] == 0)
                aout_set_big_endian(1);
            else
                goto usage;
            break;
        case 'h':
        default:
usage:                  fprintf(stderr, "Usage:\n");
            fprintf(stderr, "  strip [-EL|-EB] file...\n");
            return(1);
        }
    }
    argc -= optind;
    argv += optind;
    if (argc == 0)
        goto usage;

    signal(SIGHUP, SIG_IGN);
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    for (i = 0; i < argc; i++) {
        strip(argv[i]);
        if (status > 1)
            break;
    }
    return(status);
}
