TOPSRC		= $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk
AWK_SRCDIR      = $(if $(REBSD_SRCDIR),$(REBSD_SRCDIR),.)

CFLAGS		+= -Werror -Os -I. -I$(AWK_SRCDIR)

YACC            = bison -y
YFLAGS          = -d
LIBS            = -lm -lc
FILES           = awk.lx.o b.o main.o tran.o lib.o run.o parse.o proctab.o freeze.o
SOURCE          = awk.def.h awk.g.y awk.lx.l b.c lib.c main.c parse.c \
                  proc.c freeze.c run.c tran.c

awk:            $(FILES) awk.g.o
		${LD} ${LDFLAGS} -o awk.elf $(FILES) awk.g.o ${LIBS}
		${OBJDUMP} -S awk.elf > awk.dis
		${SIZE} awk.elf
		${ELF2AOUT} awk.elf $@ && rm awk.elf

awk.g.stamp:	$(AWK_SRCDIR)/awk.g.y
		${YACC} ${YFLAGS} $<
		mv y.tab.c awk.g.c
		touch $@

awk.g.c y.tab.h:	awk.g.stamp
		@test -f $@

awk.h:          y.tab.h
		-cmp -s y.tab.h awk.h || cp y.tab.h awk.h

$(FILES):	awk.h awk.def.h

awk.lx.o:	CFLAGS += -Wno-error
awk.lx.o:	awk.lx.c
awk.g.o:	awk.g.c y.tab.h

.SECONDARY:	awk.lx.c awk.g.c

token.h:	awk.h tokenscript
		ex - < $(AWK_SRCDIR)/tokenscript

install:        awk
		install awk ${DESTDIR}/bin/

profile:	awk.g.o $(FILES) mon.o
		cc -p -i awk.g.o $(FILES) mon.o -lm

lint:
		lint -spu b.c main.c tran.c run.c lib.c parse.c -lm |\
			egrep -v '^(error|free|malloc)'

proctab.c:	proc.c token.h awk.h
		cc -std=gnu89 -Wall -Werror -I. -I$(AWK_SRCDIR) -o proc $<
		./proc > proctab.c

clean:
		-rm -f *.o *.tmp awk.lx.c awk.g.c awk.g.stamp *.out *.elf *.dis y.tab.* awk.h proc proctab.c token.h awk
