#
# Copyright (c) 1987 Regents of the University of California.
# All rights reserved.
#
# Redistribution and use in source and binary forms are permitted
# provided that the above copyright notice and this paragraph are
# duplicated in all such forms and that any documentation,
# advertising materials, and other materials related to such
# distribution and use acknowledge that the software was developed
# by the University of California, Berkeley.  The name of the
# University may not be used to endorse or promote products derived
# from this software without specific prior written permission.
# THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
# WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
#
TOPSRC		= $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk

CFLAGS		+= -Werror

SRCS		= man.c apropos.c
MAN_OBJS	= man.o
APROPOS_OBJS	= apropos.o
OBJS		= ${MAN_OBJS} ${APROPOS_OBJS}

all:            man apropos

man:            ${MAN_OBJS}
		${LD} ${LDFLAGS} -o man.elf ${MAN_OBJS} ${LIBS}
		${OBJDUMP} -S man.elf > man.dis
		${SIZE} man.elf
		${ELF2AOUT} man.elf $@ && rm man.elf

apropos:        ${APROPOS_OBJS}
		${LD} ${LDFLAGS} -o apropos.elf ${APROPOS_OBJS} ${LIBS}
		${OBJDUMP} -S apropos.elf > apropos.dis
		${SIZE} apropos.elf
		${ELF2AOUT} apropos.elf $@ && rm apropos.elf

clean:
		rm -f *.o *.elf ${MAN} man apropos *.elf *.dis tags *~

depend:         ${SRCS}
		mkdep ${CFLAGS} ${SRCS}

install:        all
		@rm -f ${DESTDIR}/bin/whatis ${DESTDIR}/bin/apropos
		install apropos ${DESTDIR}/bin/apropos
		install man ${DESTDIR}/bin/man
		ln ${DESTDIR}/bin/apropos ${DESTDIR}/bin/whatis

lint:           ${SRCS}
		lint ${CFLAGS} ${SRCS}

tags:           ${SRCS}
		ctags ${SRCS}
		sort -o tags tags

FRC:

# DO NOT DELETE THIS LINE -- mkdep uses it.
# DO NOT PUT ANYTHING AFTER THIS LINE, IT WILL GO AWAY.
