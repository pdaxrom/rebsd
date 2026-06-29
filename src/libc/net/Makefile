#
# Copyright (c) 1988 Regents of the University of California.
# All rights reserved.
#
# Redistribution and use in source and binary forms are permitted
# provided that this notice is preserved and that due credit is given
# to the University of California at Berkeley. The name of the University
# may not be used to endorse or promote products derived from this
# software without specific prior written permission. This software
# is provided ``as is'' without express or implied warranty.
#
#	@(#)Makefile	5.16 (Berkeley) 3/14/88
#
TOPSRC=	$(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk

SRCS=	getnbyaddr.c getnbyname.c getnent.c getpent.c getpname.c	\
	getproto.c getsbyname.c getsbyport.c getsent.c herror.c 	\
	rcmd.c res_comp.c res_debug.c res_init.c res_mkquery.c		\
	res_query.c res_send.c	\
	rexec.c ruserpass.c

OBJS=	getnbyaddr.o getnbyname.o getnent.o getpent.o getpname.o	\
	getproto.o getsbyname.o getsbyport.o getsent.o rcmd.o herror.o	\
	res_comp.o res_debug.o res_init.o res_mkquery.o		\
	res_query.o res_send.o rexec.o ruserpass.o
HOSTOBJS= named/gethnamadr.o named/sethostent.o

CFLAGS+= ${DEFS} -Os \
	-Wno-comment -Wno-endif-labels \
	-Wno-builtin-declaration-mismatch \
	-Wno-implicit-function-declaration -Wno-implicit-int \
	-Wno-missing-braces \
	-Wno-return-type -Wno-return-mismatch \
	-Wno-parentheses
TAGSFILE=tags

all: net.a

net.a: ${OBJS} ${HOSTOBJS}
	@echo "building net.a"
	@${AR} cru net.a ${OBJS} ${HOSTOBJS}

named/gethnamadr.o: named/gethnamadr.c
	${CC} ${CFLAGS} -c named/gethnamadr.c -o $@

named/sethostent.o: named/sethostent.c
	${CC} ${CFLAGS} -c named/sethostent.c -o $@

netlib netlib_p: ${OBJS}
	@echo "building profiled netlib"
	@cd profiled; ${AR} cru ../netlib_p ${OBJS}
	@echo "building normal netlib"
	@${AR} cru netlib ${OBJS}

tags:
	cwd=`pwd`; \
	for i in ${SRCS}; do \
		ctags -a -f ${TAGSFILE} $$cwd/$$i; \
	done

clean:
	rm -f *.o named/*.o profiled/*.o errs a.out core net.a netlib \
	    netlib_p tags

depend:
	mkdep ${CFLAGS} ${SRCS}

# DO NOT DELETE THIS LINE -- mkdep uses it.
# DO NOT PUT ANYTHING AFTER THIS LINE, IT WILL GO AWAY.
