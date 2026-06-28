TOPSRC = $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk

CFLAGS += -Wall -Werror -fno-builtin \
    -Wno-implicit-function-declaration -Wno-implicit-int \
    -Wno-return-type -Wno-return-mismatch \
    -Wno-unused-variable -Wno-parentheses \
    -Wno-comment -Wno-endif-labels \
    -Wno-pointer-sign \
    -I$(TOPSRC)/sys -I$(TOPSRC)/sys/include -I$(TOPSRC)/include

BIN=netstat
MAN=netstat.0
MANSRC=netstat.1
OBJS=main.o inet.o if.o route.o mbuf.o unix.o

all: $(BIN) $(MAN)

$(BIN): $(OBJS)
	$(LD) $(LDFLAGS) -o $(BIN).elf $(OBJS) $(LIBS)
	$(SIZE) $(BIN).elf
	$(ELF2AOUT) $(BIN).elf $@ && rm $(BIN).elf

$(MAN): $(MANSRC)
	nroff -man $(MANSRC) > $(MAN)

install: all
	cp $(MAN) $(DESTDIR)/share/man/cat1/
	install -m 755 $(BIN) $(DESTDIR)/usr/bin

clean:
	rm -f *.o *.elf $(BIN) $(MAN) *.dis
