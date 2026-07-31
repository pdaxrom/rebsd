#
# Demos Commander file manager.
#
TOPSRC		= $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk
DECO_SRCDIR	= $(if $(REBSD_SRCDIR),$(REBSD_SRCDIR),.)

CFLAGS		+= -DHAVE_SYS_WAIT_H=1 -DHAVE_FCNTL_H=1 \
		   -DHAVE_SYS_IOCTL_H=1 -DHAVE_UNISTD_H=1 \
		   -DHAVE_TERMCAP_H=1 -DHAVE_ST_RDEV=1 \
		   -DGETGROUPS_T=int -DRETSIGTYPE=void \
		   -Dvfork=fork -DHAVE_VPRINTF=1 \
		   -DHAVE_GETHOSTNAME=1 -DHAVE_GETGROUPS=1 \
		   -DHAVE_OPENDIR=1 -DHAVE_MEMCPY=1 \
		   -DHAVE_STRTOL=1 -DHAVE_DIRFD=1 \
		   -Wall -Werror -Wno-dangling-pointer
CFLAGS		+= $(if $(filter n64,$(TARGET_PLATFORM)),-DDECO_N64=1)

BIN		= deco
DECO_LIBS	= -ltermcap
SRCS		= cap.c choice.c cmd.c com.c deco_setpgid.c dir.c draw.c edit.c env.c \
		  ex.c help.c hexview.c key.c main.c menu.c mh.c pw.c \
		  rec.c run.c scr.c tty.c ut.c view.c
OBJS		= $(SRCS:.c=.o)
MAN		= deco.0
HELP		= help/e.general help/e.edit help/e.view help/e.hexview

all:		$(BIN) $(MAN)

$(BIN):		$(OBJS)
		$(LD) $(LDFLAGS) -o $@.elf $(OBJS) $(DECO_LIBS) $(LIBS)
		$(OBJDUMP) -S $@.elf > $@.dis
		$(SIZE) $@.elf
		$(ELF2AOUT) $@.elf $@ && rm $@.elf

.SUFFIXES:	.0 .1

.1.0:
		$(MANROFF) $< > $@

clean:
		rm -f *.o *.elf $(MAN) *.dis tags *~ $(BIN)

install:	all
		mkdir -p $(DESTDIR)/usr/bin
		mkdir -p $(DESTDIR)/usr/share/man/cat1
		mkdir -p $(DESTDIR)/usr/lib/deco/help
		install -m 755 $(BIN) $(DESTDIR)/usr/bin
		cp $(MAN) $(DESTDIR)/usr/share/man/cat1/
		cp $(addprefix $(DECO_SRCDIR)/,menu profile) $(DESTDIR)/usr/lib/deco/
		cp $(addprefix $(DECO_SRCDIR)/,$(HELP)) $(DESTDIR)/usr/lib/deco/help/

lint:		$(SRCS)
		lint -hax $(SRCS)

tags:		$(SRCS)
		ctags $(SRCS)
