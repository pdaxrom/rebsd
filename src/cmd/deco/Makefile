#
# Demos Commander file manager.
#
TOPSRC		= $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk

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
SRCS		= cap.c choice.c cmd.c com.c dir.c draw.c edit.c env.c \
		  ex.c help.c hexview.c key.c main.c menu.c mh.c pw.c \
		  rec.c run.c scr.c tty.c ut.c view.c
ASM_SRCS	= deco_setpgid.S
OBJS		= $(SRCS:.c=.o) $(ASM_SRCS:.S=.o)
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
		$(MANROFF) $*.1 > $@

.S.o:
		$(CC) $(CFLAGS) -x assembler-with-cpp -c $< -o $@

clean:
		rm -f *.o *.elf $(MAN) *.dis tags *~ $(BIN)

install:	all
		install -m 755 $(BIN) $(DESTDIR)/bin
		cp $(MAN) $(DESTDIR)/share/man/cat1/
		mkdir -p $(DESTDIR)/lib/deco/help
		cp menu profile $(DESTDIR)/lib/deco/
		cp $(HELP) $(DESTDIR)/lib/deco/help/

lint:		$(SRCS)
		lint -hax $(SRCS)

tags:		$(SRCS)
		ctags $(SRCS)
