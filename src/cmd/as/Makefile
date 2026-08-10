#
# as      - Assembler
#
TOPSRC		= $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk
CMD_PARENT_SRC  = $(if $(REBSD_SRCDIR),$(REBSD_SRCDIR)/..,..)
#include $(TOPSRC)/cross.mk

CFLAGS		+= -Werror -Wall -Os
LDFLAGS         +=

OBJS            = as.o endianio.o
MAN             = as.0
MANSRC          = as.1

as.o:           $(TOPSRC)/include/elf32.h

all:            as $(MAN)

as:             ${OBJS}
		${LD} ${LDFLAGS} -o as.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S as.elf > as.dis
		${SIZE} as.elf
		${ELF2AOUT} as.elf $@ && rm as.elf

endianio.o:     $(CMD_PARENT_SRC)/endianio.c $(CMD_PARENT_SRC)/endianio.h
		${CC} ${CFLAGS} -c -o $@ $<

$(MAN):         $(MANSRC)
		${MANROFF} $< > $@

clean:
		rm -f *.o *.0 *.elf ${MAN} as tags *~ *.dis tests/*.dis tests/*.gcc-dis tests/*.o

install:        all
		install as $(DESTDIR)/bin/
		cp ${MAN} ${DESTDIR}/share/man/cat1/
