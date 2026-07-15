/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include <sys/param.h>
#include <sys/mount.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mntopts.h"

static struct mntopt mopts[] = {
	MOPT_STDOPTS,
	MOPT_UPDATE,
	{ NULL }
};

static void
fat_usage(void)
{
	(void)fprintf(stderr, "usage: mount -t fat [-o options] special node\n");
	exit(1);
}

int
mount_fat(int argc, char *argv[])
{
	extern int optreset;
	int ch, mntflags;

	mntflags = 0;
	optind = optreset = 1;
	while ((ch = getopt(argc, argv, "o:")) != EOF)
		switch (ch) {
		case 'o':
			getmntopts(optarg, mopts, &mntflags);
			break;
		default:
			fat_usage();
		}
	argc -= optind;
	argv += optind;
	if (argc != 2)
		fat_usage();

	mntflags |= MNT_SET_FSTYPE(MOUNT_FAT);
	if (mount(argv[0], argv[1], mntflags) < 0) {
		(void)fprintf(stderr, "%s on %s: %s\n", argv[0], argv[1],
		    strerror(errno));
		fflush(stderr);
		return 1;
	}
	return 0;
}
