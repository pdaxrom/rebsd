#
# N64cart ROMFS diagnostic utility.
#
TOPSRC		= $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk

CFLAGS		+= -Wall

BIN		= romfsctl
SRCS		= romfsctl.c romfs.c
OBJS		= romfsctl.o romfs.o
MAN		= $(BIN).0

all:		$(BIN) $(MAN)

$(BIN):		$(OBJS)
		$(LD) $(LDFLAGS) -o $@.elf $(OBJS) $(LIBS)
		$(OBJDUMP) -S $@.elf > $@.dis
		$(SIZE) $@.elf
		$(ELF2AOUT) $@.elf $@ && rm $@.elf

.SUFFIXES:	.0 .1

.1.0:
		$(MANROFF) $*.1 > $@

clean:
		rm -f *.o *.elf $(MAN) *.dis tags *~ $(BIN)

install:	all
		cp $(MAN) $(DESTDIR)/share/man/cat1/
		install -m 755 $(BIN) $(DESTDIR)/bin

lint:		$(SRCS)
		lint -hax $(SRCS)

tags:		$(SRCS)
		ctags $(SRCS)
