#
# Copyright (c) 1980 Regents of the University of California.
# All rights reserved.  The Berkeley software License Agreement
# specifies the terms and conditions for redistribution.
#
TOPSRC		= $(shell cd ../..; pwd)
include $(TOPSRC)/target.mk

CFLAGS          += -Werror

# Programs that live in subdirectories, and have makefiles of their own.
# /bin
SUBDIR_ALL	= adb adc-demo aout ar as awk basic calendar cc chflags \
                  chpass cpp dc diff emg env fdisk find fold forth \
                  fstat glcdtest hostname id la lcc lcpp ld ls login make \
                  man md5 med more netstat nm passwd pdc picoc ping portio printf pwm \
                  ranlib re renice retroforth scm sed setty sh sl smallc \
                  smlrc smux stty sysctl telnet telnetd test uname wget wiznet xargs \
                  zmodem gtest msec cron compress date2 tip \
                  talloc uucp viless virus yacc

# /sbin
SUBDIR_ALL    += chown chroot dhclient disktool fsck getty ifconfig init \
                  mkfs mknod mkpasswd mount pstat \
                  reboot route shutdown umount update vipw

# TODO: ccom lex m4 reloc sre pforth

# Shell scripts that need only be installed and are never removed.
#
SCRIPT_ALL	= false nohup true #lorder mkdep

# C programs that live in the current directory and do not need
# explicit make lines.
#
STD_ALL		= basename cal cat cb chgrp chmod cmp col comm cp dd \
                  diskspeed du echo ed fgrep file grep head hostid join \
                  kill last ln mesg mkdir mv nice od pagesize pr printenv \
                  pwd rev rm rmail rmdir size sleep sort split sum sync \
                  tail tar tee time touch tr tsort tty uniq w wc whereis who

# C programs that live in the current directory and need explicit make lines.
#
NSTD_ALL	= bc expr egrep

# Programs that must run setuid to root
#
SETUID_ALL	= mail su

# Programs that run set-group-id operator
#
OPERATOR_ALL	= df

# Programs that must run set-group-id kmem.
#
KMEM_ALL	= ps iostat vmstat

# Programs that must run set-group-id tty.
#
TTY_ALL		= wall write

# Platforms can restrict or extend the command set without duplicating this
# makefile. Empty CMD_ONLY_*, CMD_EXCLUDE_*, and CMD_EXTRA_* variables preserve
# the full default set.
SUBDIR_ALL	+= $(CMD_EXTRA_SUBDIR)
SCRIPT_ALL	+= $(CMD_EXTRA_SCRIPT)
STD_ALL		+= $(CMD_EXTRA_STD)
NSTD_ALL	+= $(CMD_EXTRA_NSTD)
SETUID_ALL	+= $(CMD_EXTRA_SETUID)
OPERATOR_ALL	+= $(CMD_EXTRA_OPERATOR)
KMEM_ALL	+= $(CMD_EXTRA_KMEM)
TTY_ALL		+= $(CMD_EXTRA_TTY)
CMD_FILTER	= $(filter-out $(CMD_EXCLUDE_$(1)),$(if $(CMD_ONLY_$(1)),$(filter $(CMD_ONLY_$(1)),$($(1)_ALL)),$($(1)_ALL)))
SUBDIR		= $(call CMD_FILTER,SUBDIR)
SCRIPT		= $(call CMD_FILTER,SCRIPT)
STD		= $(call CMD_FILTER,STD)
NSTD		= $(call CMD_FILTER,NSTD)
SETUID		= $(call CMD_FILTER,SETUID)
OPERATOR	= $(call CMD_FILTER,OPERATOR)
KMEM		= $(call CMD_FILTER,KMEM)
TTY		= $(call CMD_FILTER,TTY)

# 'strip' is handled specially because 'install -s' now uses 'strip' and
# thus we can't do a 'install -s strip /bin/strip' without an error.
#
CMD_BUILD_STRIP ?= strip
CMD_BIN_PROGS	= $(STD) $(NSTD) $(CMD_BUILD_STRIP)
CMD_PATTERN_PROGS = $(filter-out size strip,$(CMD_BIN_PROGS))
BINS		= $(CMD_BIN_PROGS) $(SETUID) $(OPERATOR) $(KMEM) $(TTY) $(SCRIPT)

all:		$(SUBDIR) $(BINS)

$(SUBDIR):	FRC
		$(MAKE) -C $@ $(MFLAGS)

FRC:

# $(STD) $(SETUID) $(KMEM) $(OPERATOR) $(TTY) strip
$(CMD_PATTERN_PROGS) $(SETUID) $(KMEM) $(OPERATOR) $(TTY): %: %.c
		$(CC) $(CFLAGS) -c $<
		$(LD) $(LDFLAGS) -o $@.elf $@.o $(LIBS)
		$(OBJDUMP) -S $@.elf > $@.dis
		$(SIZE) $@.elf
		$(ELF2AOUT) $@.elf $@ && /bin/rm $@.elf

# Files listed in $(NSTD) have explicit make lines given below.

# bc egrep expr
%.c: %.y
		$(YACC) $(YFLAGS) -o $@ $<

size strip: %: %.o aoutio.o
		$(LD) $(LDFLAGS) -o $@.elf $@.o aoutio.o $(LIBS)
		$(OBJDUMP) -S $@.elf > $@.dis
		$(SIZE) $@.elf
		$(ELF2AOUT) $@.elf $@ && /bin/rm $@.elf

aoutio.o:	aoutio.c aoutio.h
		$(CC) $(CFLAGS) -c aoutio.c

$(SCRIPT):
		install -c $@.sh $@

install:	$(BINS)
		@if [ -n "$(strip $(CMD_BIN_PROGS))" ]; then \
			install $(CMD_BIN_PROGS) $(DESTDIR)/bin/; \
		fi
		-for i in $(SUBDIR); do \
			$(MAKE) -C $$i $(MFLAGS) DESTDIR=$(DESTDIR) install; done
		-for i in $(SCRIPT); do install $$i.sh $(DESTDIR)/bin/$$i; done
		-for i in $(SETUID); do \
			install -m 4751 $$i $(DESTDIR)/bin/$$i; done
		-for i in $(OPERATOR); do \
			install -m 2751 $$i $(DESTDIR)/bin/$$i; done
		-for i in $(KMEM); do \
			install -m 2751 $$i $(DESTDIR)/bin/$$i; done
		-for i in $(TTY); do \
			install -m 2751 $$i $(DESTDIR)/bin/$$i; done

clean:
		/bin/rm -f $(BINS) bc.c egrep.c expr.c a.out core *.s *.o *.dis *.elf *~ y.tab.c errs
		for i in $(SUBDIR); do (cd $$i; $(MAKE) $(MFLAGS) clean); done

depend:		expr.c
		for i in $(BINS); do \
		    cc -M $(INCPATH) $$i.c | sed 's/\.o//' | \
		    awk ' ( if ($$1 != prev) \
			( if (rec != "") print rec; rec = $$0; prev = $$1; ) \
			else ( if (length(rec $$2) > 78) ( print rec; rec = $$0; ) \
			else rec = rec " " $$2 ) ) \
			END ( print rec ) ' >> makedep; done
		echo '/^# DO NOT DELETE THIS LINE/+2,$$d' >eddep
		echo '$$r makedep' >>eddep
		echo 'w' >>eddep
		cp Makefile Makefile.bak
		ed - Makefile < eddep
		/bin/rm eddep makedep
		echo '# DEPENDENCIES MUST END AT END OF FILE' >> Makefile
		echo '# IF YOU PUT STUFF HERE IT WILL GO AWAY' >> Makefile
		echo '# see make depend above' >> Makefile

# DO NOT DELETE THIS LINE -- make depend uses it
