#
# 2.11BSD ifconfig, currently limited to AF_INET for the MIPS network port.
#
TOPSRC		= $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk

CFLAGS		+= -Wall -Werror -fno-builtin \
		   -Wno-implicit-function-declaration -Wno-implicit-int \
		   -Wno-return-type -Wno-return-mismatch \
		   -Wno-unused-variable -Wno-parentheses \
		   -Wno-comment -Wno-endif-labels \
		   -I$(TOPSRC)/sys \
		   -I$(TOPSRC)/sys/include

BIN		= ifconfig
SRCS		= $(BIN).c
OBJS		= $(BIN).o
MAN		= $(BIN).0

all:		$(BIN) $(MAN)

$(BIN):		$(OBJS)
		$(LD) $(LDFLAGS) -o $@.elf $(OBJS) $(LIBS)
		$(OBJDUMP) -S $@.elf > $@.dis
		$(SIZE) $@.elf
		$(ELF2AOUT) $@.elf $@ && rm $@.elf

.SUFFIXES:	.0 .8

.8.0:
		$(MANROFF) $< > $@

clean:
		rm -f *.o *.elf $(MAN) *.dis tags *~ $(BIN)

install:	all
		cp $(MAN) $(DESTDIR)/share/man/cat8/
		install -m 755 $(BIN) $(DESTDIR)/sbin

lint:		$(SRCS)
		lint -hax $(SRCS)

tags:		$(SRCS)
		ctags $(SRCS)
