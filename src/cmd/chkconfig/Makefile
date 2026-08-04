TOPSRC = $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk

CFLAGS += -Wall -Werror

BIN = chkconfig
MAN = chkconfig.0
MANSRC = chkconfig.8
OBJS = chkconfig.o

all: $(BIN) $(MAN)

$(BIN): $(OBJS)
	$(LD) $(LDFLAGS) -o $(BIN).elf $(OBJS) $(LIBS)
	$(SIZE) $(BIN).elf
	$(ELF2AOUT) $(BIN).elf $@ && rm $(BIN).elf

$(MAN): $(MANSRC)
	nroff -man $< > $(MAN)

install: all
	cp $(MAN) $(DESTDIR)/share/man/cat8/
	install -m 755 $(BIN) $(DESTDIR)/usr/sbin

clean:
	rm -f *.o *.elf $(BIN) $(MAN) *.dis
