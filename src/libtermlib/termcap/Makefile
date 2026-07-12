#
# Copyright (c) 1980 Regents of the University of California.
# All rights reserved.  The Berkeley software License Agreement
# specifies the terms and conditions for redistribution.
#
#	@(#)Makefile	5.2.4 (2.11BSD GTE) 1997/9/22
#
# reorder gives an editor command for most common terminals
# (in reverse order from n'th to 1'st most commonly used)
# to move them to the front of termcap
#
DESTDIR = ../../..
TERMCAP_SRCDIR = $(if $(REBSD_SRCDIR),$(REBSD_SRCDIR),.)
TERMCAP_REORDER = $(TERMCAP_SRCDIR)/reorder
TERMCAP_SOURCE = $(TERMCAP_SRCDIR)/termcap.src
TERMCAP_LOCAL = $(TERMCAP_SRCDIR)/termcap.local
TERMCAP_SMALL = $(TERMCAP_SRCDIR)/termcap.small

termcap.full: $(TERMCAP_REORDER) $(TERMCAP_SOURCE) $(TERMCAP_LOCAL)
	sed -e 's|termcap\.local|$(TERMCAP_LOCAL)|' \
	    -e 's|termcap\.full|$(abspath $@)|' $(TERMCAP_REORDER) > reorder.obj
	ex - $(TERMCAP_SOURCE) < reorder.obj

install: termcap.small termcap.full
	install -c -m 444 $(TERMCAP_SMALL) ${DESTDIR}/etc/termcap
	install -c -m 444 termcap.full ${DESTDIR}/etc/termcap.full

tabset: FRC
	rm -fr ${DESTDIR}/usr/share/tabset
	mkdir ${DESTDIR}/usr/share/tabset
	chmod 755 ${DESTDIR}/usr/share/tabset
	install -c -m 444 tabset/* ${DESTDIR}/usr/share/tabset

clean:
	rm -f termcap.full reorder.obj

FRC:
