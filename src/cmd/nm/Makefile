TOPSRC		= $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk
CMD_PARENT_SRC  = $(if $(REBSD_SRCDIR),$(REBSD_SRCDIR)/..,..)
#include $(TOPSRC)/cross.mk

CFLAGS		+= -Werror -Wall -I$(if $(REBSD_SRCDIR),$(REBSD_SRCDIR)/../ar,../ar)

SRCS		= nm.c ../aoutio.c
OBJS		= nm.o aoutio.o
MAN		= nm.0
MANSRC		= nm.1

all:            nm $(MAN)

nm:          	${OBJS}
		${LD} ${LDFLAGS} -o nm.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S nm.elf > nm.dis
		${SIZE} nm.elf
		${ELF2AOUT} nm.elf $@ && rm nm.elf

aoutio.o:       $(CMD_PARENT_SRC)/aoutio.c $(CMD_PARENT_SRC)/aoutio.h
		${CC} ${CFLAGS} -c -o $@ $<

nm.o:           $(TOPSRC)/include/elf32.h

$(MAN):         $(MANSRC)
		${MANROFF} $< > $@

clean:
		rm -f *.o *.elf ${MAN} nm *.elf *.dis tags *~

install:        all
		install nm ${DESTDIR}/bin/nm
		cp ${MAN} ${DESTDIR}/share/man/cat1/

lint:
		lint -havx -I$(if $(REBSD_SRCDIR),$(REBSD_SRCDIR)/../ar,../ar) ${SRCS}
