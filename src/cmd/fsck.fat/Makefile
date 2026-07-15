#
# Compact FAT16/FAT32 filesystem checker.
#
TOPSRC          = $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk

PROG            = fsck.fat
OBJS            = main.o fat_subr.o
MAN             = fsck.fat.0
CFLAGS          += -Werror -Wall -Os -I$(TOPSRC)/sys

all:            $(PROG) $(MAN)

$(PROG):       $(OBJS)
		$(LD) $(LDFLAGS) -o $(PROG).elf $(OBJS) $(LIBS)
		$(OBJDUMP) -S $(PROG).elf > $(PROG).dis
		$(SIZE) $(PROG).elf
		$(ELF2AOUT) $(PROG).elf $(PROG) && rm -f $(PROG).elf

fat_subr.o:     $(TOPSRC)/sys/fs/fat/fat_subr.c
		$(CC) $(CFLAGS) -c $(TOPSRC)/sys/fs/fat/fat_subr.c -o $@

.SUFFIXES:      .0 .8

.8.0:
		$(MANROFF) $< > $@

install:        all
		install -m 755 $(PROG) $(DESTDIR)/sbin/$(PROG)
		cp $(MAN) $(DESTDIR)/share/man/cat8/$(MAN)

clean:
		rm -f $(OBJS) $(PROG) $(PROG).elf $(PROG).dis $(MAN) core *~
