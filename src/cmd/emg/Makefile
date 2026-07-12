# emg Makefile
# for RetroBSD

TOPSRC =	$(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk
EMG_SRCDIR =	$(if $(REBSD_SRCDIR),$(REBSD_SRCDIR),.)

# Some basic CFLAGS.
CFLAGS =	-Os -Wall -Werror

# With the extra LDFLAGS, save some bytes.
CFLAGS +=	-ffunction-sections -fdata-sections

# This reduces code size significantly.
#CFLAGS +=	-mips16

# with CFLAGS+=	-ffunction-sections -fdata-sections
#LDFLAGS +=	-Wl,--gc-sections

LIBS =		-ltermcap -lc

MAN =		emg.0
MANSRC =	emg.1

OBJS =	basic.o buffer.o display.o file.o fileio.o line.o main.o \
	random.o region.o search.o tcap.o ttyio.o word.o

all:	emg ${MAN}

emg:	${OBJS}
	${LD} ${LDFLAGS} -o emg.elf ${OBJS} ${LIBS}
	${OBJDUMP} -S emg.elf > emg.dis
	${SIZE} emg.elf
	${ELF2AOUT} emg.elf $@ && rm emg.elf

${MAN}:	${MANSRC}
	${MANROFF} $< > ${MAN}

install: all
	install emg ${DESTDIR}/bin/emg
	cp ${MAN} ${DESTDIR}/share/man/cat1/
	cp -p $(EMG_SRCDIR)/emg.keys ${DESTDIR}/share/emg.keys

clean:
	rm -f *.o *~ *.core *.bak *.dis emg ${MAN}
