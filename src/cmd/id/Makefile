#
# 1997/6/25 - Steven Schultz
#
TOPSRC		= $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk

CFLAGS		+= -Werror

SRCS		= id.c
OBJS		= id.o
MAN		= id.0 groups.0 whoami.0
ID_SRCDIR       = $(if $(REBSD_SRCDIR),$(REBSD_SRCDIR),.)

all:            id ${MAN} groups whoami

groups:         $(ID_SRCDIR)/groups.sh
		install -c $< $@

whoami:         $(ID_SRCDIR)/whoami.sh
		install -c $< $@

id:             ${OBJS}
		${LD} ${LDFLAGS} -o id.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S id.elf > id.dis
		${SIZE} id.elf
		${ELF2AOUT} id.elf $@ && rm id.elf

.SUFFIXES:      .0 .1

.1.0:
		${MANROFF} $< > $@

clean:
		rm -f *.o *.elf ${MAN} id groups whoami *.elf *.dis tags *~

depend:         ${SRCS}
		mkdep ${CFLAGS} ${SRCS}

install:        all
		cp ${MAN} ${DESTDIR}/share/man/cat1/
		install id ${DESTDIR}/bin/id
		install groups ${DESTDIR}/bin/groups
		install whoami ${DESTDIR}/bin/whoami

lint:           ${SRCS}
		lint -hax ${SRCS}

tags:           ${SRCS}
		ctags ${SRCS}

# DO NOT DELETE THIS LINE -- mkdep uses it.
# DO NOT PUT ANYTHING AFTER THIS LINE, IT WILL GO AWAY.
