#
# N64 framebuffer JPEG viewer.
#
TOPSRC		= $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk

N64_STB_DIR	?= /Users/sash/Work/N64/N64cart/rom/src/stb

CFLAGS		+= -Wall -Werror
STB_CFLAGS	= $(CFLAGS) -I$(N64_STB_DIR) -Wno-error \
		  -Wno-unused-function -Wno-unused-variable \
		  -Wno-maybe-uninitialized -Wno-sign-compare \
		  -Wno-missing-braces -Wno-misleading-indentation

BIN		= fbview
SRCS		= fbview.c fbview_stb.c
OBJS		= fbview.o fbview_stb.o
MAN		= $(BIN).0
STB_HDRS	= $(N64_STB_DIR)/stb_image.h

all:		$(BIN) $(MAN)

$(BIN):		$(OBJS)
		$(LD) $(LDFLAGS) -o $@.elf $(OBJS) $(LIBS)
		$(OBJDUMP) -S $@.elf > $@.dis
		$(SIZE) $@.elf
		$(ELF2AOUT) $@.elf $@ && rm $@.elf

fbview_stb.o:	fbview_stb.c fbview_stb.h $(STB_HDRS)
		$(CC) $(STB_CFLAGS) -c $<

.SUFFIXES:	.0 .1

.1.0:
		$(MANROFF) $< > $@

clean:
		rm -f *.o *.elf $(MAN) *.dis tags *~ $(BIN)

install:	all
		cp $(MAN) $(DESTDIR)/share/man/cat1/
		install -m 755 $(BIN) $(DESTDIR)/bin

lint:		$(SRCS)
		lint -hax $(SRCS)

tags:		$(SRCS)
		ctags $(SRCS)
