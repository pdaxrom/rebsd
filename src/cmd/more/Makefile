#
# Copyright (c) 1980 Regents of the University of California.
# All rights reserved.  The Berkeley software License Agreement
# specifies the terms and conditions for redistribution.
#
TOPSRC		= $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk
MORE_SRCDIR	= $(if $(REBSD_SRCDIR),$(REBSD_SRCDIR),.)

CFLAGS		+= -Werror

SRCS		= more.c
OBJS		= more.o

all:            more

more:        	${OBJS}
		${LD} ${LDFLAGS} -o more.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S more.elf > more.dis
		${SIZE} more.elf
		${ELF2AOUT} more.elf $@ && rm more.elf

clean:
		rm -f *.o *.elf ${MAN} more *.elf *.dis tags *~

install:        all $(MORE_SRCDIR)/more.help
		install more ${DESTDIR}/bin/more
		@-rm -f ${DESTDIR}/share/misc/more.help
		cp $(MORE_SRCDIR)/more.help ${DESTDIR}/share/misc/more.help
