TOPSRC		= $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk
#include $(TOPSRC)/cross.mk

OBJS            = cc.o

CC_STDINC	?= /usr/include
CC_LIBDIR	?= /usr/lib
CC_LIBEXECDIR	?= /usr/libexec
CC_CRT0FILE	?= /usr/lib/crt0.o
CC_CRT0FILE_PROFILE ?= /usr/lib/mcrt0.o

CFLAGS		+= -Werror -Wall -DGCC_COMPAT
CFLAGS          += -DVERSSTR=\"1.0\" \
                   -DSTDINC=\"$(CC_STDINC)\" \
                   -DLIBDIR=\"$(CC_LIBDIR)\" \
                   -DLIBEXECDIR=\"$(CC_LIBEXECDIR)\" \
                   -DCRT0FILE=\"$(CC_CRT0FILE)\" \
                   -DCRT0FILE_PROFILE=\"$(CC_CRT0FILE_PROFILE)\"

all:            cc

cc:             $(OBJS)
		${LD} ${LDFLAGS} -o cc.elf $(OBJS) ${LIBS}
		${OBJDUMP} -S cc.elf > cc.dis
		${SIZE} cc.elf
		${ELF2AOUT} cc.elf $@ && rm cc.elf

clean:
		rm -f *.o *.0 *.elf cc *.elf *.dis tags *~

install:        all
		install cc $(DESTDIR)/bin/
		install cc $(DESTDIR)/bin/scc
		install cc $(DESTDIR)/bin/lcc
