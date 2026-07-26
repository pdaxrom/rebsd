#
# Copyright (c) 1980 Regents of the University of California.
# All rights reserved.  The Berkeley software License Agreement
# specifies the terms and conditions for redistribution.
#
TOPSRC		= $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk

CFLAGS		+= ${DEFS} -Os -Wall -Werror

SRCS		= ${STDSRC}
OBJS		= ${STDOBJ}

STDSRC		= abort.c abs.c alarm.c atof.c atoi.c atol.c calloc.c closedir.c crypt.c \
		  ctime.c ctime_r.c ctype_.c daemon.c devname.c ecvt.c err.c \
		  execvp.c fakcu.c frexp.c fstab.c gcvt.c getenv.c getgrent.c \
                  getgrgid.c getgrnam.c getlogin.c \
		  getgrouplist.c gethostname.c getpagesize.c \
		  getpass.c getpwent.c getloadavg.c getmntinfo.c \
		  getttyent.c getttynam.c getusershell.c getwd.c labs.c \
		  initgroups.c isatty.c isinff.c isnanf.c ldexp.c locale.c malloc.c mathclass.c \
                  mktime.c mktemp.c modff.c nan.c ndbm.c nlist.c knlist.c opendir.c perror.c popen.c \
                  psignal.c qsort.c random.c readdir.c regex.c regcomp.c regerror.c \
		  regexec.c regfree.c scandir.c \
                  seekdir.c setmode.c sethostname.c setenv.c siglist.c \
                  signal.c siginterrupt.c sigsetops.c \
		  sleep.c strcasecmp.c strftime.c swab.c sysctl.c syslog.c system.c \
		  strcat.c strncat.c strcpy.c strncpy.c strncmp.c \
		  telldir.c time.c timezone.c ttyname.c ttyslot.c ualarm.c usleep.c \
		  strdup.c uname.c wait.c wait3.c waitpid.c

# Portable owners for the primitives which some machine directories replace
# with optimized assembly.  During libc.a assembly, a machine object with the
# same basename replaces this fallback without changing callers.
STDSRC		+= bcmp-disabled.c bcopy-disabled.c bzero-disabled.c \
		   ffs-disabled.c index-disabled.c rindex-disabled.c \
		   strcmp-disabled.c strlen-disabled.c

STDOBJ		= abort.o abs.o alarm.o atof.o atoi.o atol.o calloc.o closedir.o crypt.o \
		  ctime.o ctime_r.o ctype_.o daemon.o devname.o ecvt.o err.o \
		  execvp.o fakcu.o frexp.o fstab.o gcvt.o getenv.o getgrent.o \
                  getgrgid.o getgrnam.o getlogin.o \
		  getgrouplist.o gethostname.o getpagesize.o \
		  getpass.o getpwent.o getloadavg.o getmntinfo.o \
		  getttyent.o getttynam.o getusershell.o getwd.o labs.o \
		  initgroups.o isatty.o isinff.o isnanf.o ldexp.o locale.o malloc.o mathclass.o \
                  mktime.o mktemp.o modff.o nan.o ndbm.o nlist.o knlist.o opendir.o perror.o popen.o \
                  psignal.o qsort.o random.o readdir.o regex.o regcomp.o regerror.o \
		  regexec.o regfree.o scandir.o \
                  seekdir.o setmode.o sethostname.o setenv.o siglist.o \
                  signal.o siginterrupt.o sigsetops.o \
		  sleep.o strcasecmp.o strftime.o swab.o sysctl.o syslog.o system.o \
		  strcat.o strncat.o strcpy.o strncpy.o strncmp.o \
		  telldir.o time.o timezone.o ttyname.o ttyslot.o ualarm.o usleep.o \
		  strdup.o uname.o wait.o wait3.o waitpid.o

STDOBJ		+= bcmp.o bcopy.o bzero.o ffs.o index.o rindex.o strcmp.o strlen.o

all:		gen.a

bcmp.o:		bcmp-disabled.c
		$(CC) $(CFLAGS) -c $< -o $@
bcopy.o:	bcopy-disabled.c
		$(CC) $(CFLAGS) -c $< -o $@
bzero.o:	bzero-disabled.c
		$(CC) $(CFLAGS) -c $< -o $@
ffs.o:		ffs-disabled.c
		$(CC) $(CFLAGS) -c $< -o $@
index.o:	index-disabled.c
		$(CC) $(CFLAGS) -c $< -o $@
rindex.o:	rindex-disabled.c
		$(CC) $(CFLAGS) -c $< -o $@
strcmp.o:	strcmp-disabled.c
		$(CC) $(CFLAGS) -c $< -o $@
strlen.o:	strlen-disabled.c
		$(CC) $(CFLAGS) -c $< -o $@

ifneq ($(filter $(TARGET_PLATFORM),n64 mips i386),)
STDSRC		+= isinf.c isnan.c modf.c
STDOBJ		+= isinf.o isnan.o modf.o
endif

gen.a:		${OBJS}
		@echo "buiding gen.a"
		@$(AR) cru gen.a ${OBJS}

tags:
		cwd=`pwd`; \
		for i in ${SRCS}; do \
			ctags -a -f ${TAGSFILE} $$cwd/$$i; \
		done

clean:
		rm -f gen.a *.o *~ profiled/*.o tags Makefile.bak

depend:
		for i in ${SRCS}; do \
		    cc -M ${CFLAGS} $$i | awk ' { if ($$1 != prev) \
			{ if (rec != "") print rec; rec = $$0; prev = $$1; } \
			else { if (length(rec $$2) > 78) { print rec; rec = $$0; } \
			else rec = rec " " $$2 } } \
			END { print rec } ' >> makedep; done
		echo '/^# DO NOT DELETE THIS LINE/+2,$$d' >eddep
		echo '$$r makedep' >>eddep
		echo 'w' >>eddep
		cp Makefile Makefile.bak
		ed - Makefile < eddep
		rm eddep makedep
		echo '# DEPENDENCIES MUST END AT END OF FILE' >> Makefile
		echo '# IF YOU PUT STUFF HERE IT WILL GO AWAY' >> Makefile
		echo '# see make depend above' >> Makefile

# DO NOT DELETE THIS LINE -- make depend uses it
