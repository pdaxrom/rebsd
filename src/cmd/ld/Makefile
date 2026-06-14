#
# Version 2.2 January 24, 1998
#
TOPSRC		= $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk
#include $(TOPSRC)/cross.mk

CFLAGS		+= -Werror -Wall -Os
LDFLAGS		+=

SRCS		= ld.c ../aoutio.c
OBJS            = ld.o aoutio.o
MAN		= ld.0
MANSRC		= ld.1

all:            ld $(MAN)

ld:             ${OBJS}
		${LD} ${LDFLAGS} -o ld.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S ld.elf > ld.dis
		${SIZE} ld.elf
		${ELF2AOUT} ld.elf $@ && rm ld.elf

aoutio.o:       ../aoutio.c ../aoutio.h
		${CC} ${CFLAGS} -c -o $@ ../aoutio.c

$(MAN):         $(MANSRC)
		${MANROFF} $< > $@

clean:
		rm -f *.o *.0 *.elf ${MAN} ld *.elf *.dis a.out tags *~

install:        all
		install ld ${DESTDIR}/bin/ld
		cp ${MAN} ${DESTDIR}/share/man/cat1/
