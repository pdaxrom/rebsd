#
# aout    - Display information from a.out files
#
TOPSRC		= $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk
#include $(TOPSRC)/cross.mk

CFLAGS		+= -Werror -Wall -Os
LDFLAGS         +=

AOUTOBJS        = aout.o mips-dis.o aoutio.o
CMD_PARENT_SRC  = $(if $(REBSD_SRCDIR),$(REBSD_SRCDIR)/..,..)

all:            aout

aout:           $(AOUTOBJS)
		${LD} ${LDFLAGS} -o aout.elf $(AOUTOBJS) ${LIBS}
		${OBJDUMP} -S aout.elf > aout.dis
		${SIZE} aout.elf
		${ELF2AOUT} aout.elf $@ && rm aout.elf

aoutio.o:       $(CMD_PARENT_SRC)/aoutio.c $(CMD_PARENT_SRC)/aoutio.h
		${CC} ${CFLAGS} -c -o $@ $<

clean:
		rm -f *.o *.0 *.elf aout tags *~ *.dis

install:        all
		install aout $(DESTDIR)/bin/
