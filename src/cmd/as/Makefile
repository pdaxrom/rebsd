#
# as      - Assembler
#
TOPSRC		= $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk
#include $(TOPSRC)/cross.mk

CFLAGS		+= -Werror -Wall -Os
LDFLAGS         +=

OBJS            = as.o aoutio.o

all:            as

as:             ${OBJS}
		${LD} ${LDFLAGS} -o as.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S as.elf > as.dis
		${SIZE} as.elf
		${ELF2AOUT} as.elf $@ && rm as.elf

aoutio.o:       ../aoutio.c ../aoutio.h
		${CC} ${CFLAGS} -c -o $@ ../aoutio.c

clean:
		rm -f *.o *.0 *.elf as tags *~ *.dis tests/*.dis tests/*.gcc-dis tests/*.o

install:        all
		install as $(DESTDIR)/bin/
