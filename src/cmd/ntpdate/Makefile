TOPSRC = $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk

CFLAGS += -Wall -Werror -fno-builtin \
    -I$(TOPSRC)/sys -I$(TOPSRC)/sys/include

BIN=ntpdate
MAN=ntpdate.0
MANSRC=ntpdate.8
OBJS=ntpdate.o ntp_proto.o

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
