TOPSRC		= $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk

CFLAGS          += -Wall -Werror

SRCS		= init.c
OBJS		= init.o
MAN		= init.0 inittab.0
MANSRC		= init.8 inittab.5

all:		init ${MAN}

init:		${OBJS}
		${LD} ${LDFLAGS} -o init.elf ${OBJS} -lc
		${OBJDUMP} -S init.elf > init.dis
		${SIZE} init.elf
		${ELF2AOUT} init.elf $@ && rm init.elf

init.0:		init.8
		${MANROFF} $< > $@

inittab.0:	inittab.5
		${MANROFF} $< > $@

clean:
		rm -f *.o *~ *.elf *.dis ${MAN} init tags

depend:		${SRCS}
		mkdep ${CFLAGS} ${SRCS}

install:	all
		cp init.0 ${DESTDIR}/share/man/cat8/
		cp init.0 ${DESTDIR}/share/man/cat8/telinit.0
		cp inittab.0 ${DESTDIR}/share/man/cat5/
		install -m 755 init ${DESTDIR}/sbin/init
		ln -sf ../sbin/init ${DESTDIR}/etc/init
		ln -sf ../sbin/init ${DESTDIR}/etc/telinit

lint:		${SRCS}
		lint -hax ${SRCS}

tags:		${SRCS}
		ctags ${SRCS}

# DO NOT DELETE THIS LINE -- mkdep uses it.
# DO NOT PUT ANYTHING AFTER THIS LINE, IT WILL GO AWAY.
