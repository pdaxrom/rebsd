#
# Compact FAT16/FAT32 filesystem formatter.
#
TOPSRC          = $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk

PROG            = mkfs.fat
OBJS            = main.o
MAN             = mkfs.fat.0
CFLAGS          += -Werror -Wall -Os

all:            $(PROG) $(MAN)

$(PROG):       $(OBJS)
		$(LD) $(LDFLAGS) -o $(PROG).elf $(OBJS) $(LIBS)
		$(OBJDUMP) -S $(PROG).elf > $(PROG).dis
		$(SIZE) $(PROG).elf
		$(ELF2AOUT) $(PROG).elf $(PROG) && rm -f $(PROG).elf

.SUFFIXES:      .0 .8

.8.0:
		$(MANROFF) $< > $@

install:        all
		install -m 755 $(PROG) $(DESTDIR)/sbin/$(PROG)
		cp $(MAN) $(DESTDIR)/share/man/cat8/$(MAN)

clean:
		rm -f $(OBJS) $(PROG) $(PROG).elf $(PROG).dis $(MAN) core *~
