#
# Copyright (c) 1980 Regents of the University of California.
# All rights reserved.  The Berkeley software License Agreement
# specifies the terms and conditions for redistribution.
#
TOPSRC		= $(shell cd ..; pwd)
include $(TOPSRC)/target.mk

# Programs that live in subdirectories, and have makefiles of their own.
#
LIBS_ALL	= startup-$(MACHINE) libc libm libutil libtermlib libcurses \
                  libvmf libwiznet libreadline libgpanel libtcl libmagic
SUBDIR_ALL	= share cmd games man

# Platforms can restrict the source tree without duplicating this makefile.
# Empty SRC_ONLY_* and SRC_EXCLUDE_* variables preserve the full default set.
SRC_FILTER	= $(filter-out $(SRC_EXCLUDE_$(1)),$(if $(SRC_ONLY_$(1)),$(filter $(SRC_ONLY_$(1)),$($(1)_ALL)),$($(1)_ALL)))
LIBS		= $(call SRC_FILTER,LIBS)
SUBDIR		= $(call SRC_FILTER,SUBDIR)

all:		$(LIBS) $(SUBDIR)

$(LIBS):	FRC
	$(MAKE) -C $@

$(SUBDIR):	FRC | $(LIBS)
		$(MAKE) -C $@

FRC:

# Installation is ordered after the complete build.  Do not pass the two
# goals "all install" to recursive makes: under -j they are independent goals
# unless every child makefile restates the ordering, and installation can
# otherwise race generation of programs and manual pages.
install: all
		for i in $(SUBDIR); do \
			$(MAKE) -C $$i DESTDIR=$(DESTDIR) install || exit $$?; \
		done

clean:
		rm -f a.out core *.s *.o *.a *~
		-for i in $(SUBDIR); do $(MAKE) -C $$i clean; done
		-for i in $(LIBS); do $(MAKE) -C $$i clean; done
