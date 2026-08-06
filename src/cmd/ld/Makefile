#
# Version 2.2 January 24, 1998
#
TOPSRC		= $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk
CMD_PARENT_SRC  = $(if $(REBSD_SRCDIR),$(REBSD_SRCDIR)/..,..)
#include $(TOPSRC)/cross.mk

CFLAGS		+= -Werror -Wall -Os
LDFLAGS		+=

SRCS		= ld.c ../aoutio.c
OBJS            = ld.o aoutio.o
MAN		= ld.0
MANSRC		= ld.1

ld.o:           $(TOPSRC)/include/elf32.h

all:            ld $(MAN)

ld:             ${OBJS}
		${LD} ${LDFLAGS} -o ld.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S ld.elf > ld.dis
		${SIZE} ld.elf
		${ELF2AOUT} ld.elf $@ && rm ld.elf

aoutio.o:       $(CMD_PARENT_SRC)/aoutio.c $(CMD_PARENT_SRC)/aoutio.h
		${CC} ${CFLAGS} -c -o $@ $<

$(MAN):         $(MANSRC)
		${MANROFF} $< > $@

clean:
		rm -f *.o *.0 *.elf ${MAN} ld *.elf *.dis a.out tags *~

install:        all
		install ld ${DESTDIR}/bin/ld
		cp ${MAN} ${DESTDIR}/share/man/cat1/
