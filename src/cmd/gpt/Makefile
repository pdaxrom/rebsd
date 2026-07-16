# Public Domain.

TOPSRC          = $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk

CFLAGS          += -Werror -I$(TOPSRC)/sys/include
BIN             = gpt
SRCS            = gpt.c
OBJS            = gpt.o
MAN             = gpt.0

all:            $(BIN) $(MAN)

$(BIN):         $(OBJS)
		$(LD) $(LDFLAGS) -o $@.elf $(OBJS) $(LIBS)
		$(OBJDUMP) -S $@.elf > $@.dis
		$(SIZE) $@.elf
		$(ELF2AOUT) $@.elf $@

.SUFFIXES:      .0 .8

.8.0:
		$(MANROFF) $< > $@

clean:
		rm -f *.o *.elf $(MAN) *.dis tags *~ $(BIN)

depend:         $(SRCS)
		mkdep $(CFLAGS) $(SRCS)

install:        all
		install -m 755 $(BIN) $(DESTDIR)/sbin
		cp $(MAN) $(DESTDIR)/share/man/cat8

.PHONY: all clean depend install
