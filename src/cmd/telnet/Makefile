#
# Small TELNET client for the MIPS network ports.
#
TOPSRC = $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk

CFLAGS += -Wall -Werror -fno-builtin \
    -Wno-implicit-function-declaration -Wno-implicit-int \
    -Wno-return-type -Wno-return-mismatch \
    -Wno-unused-variable -Wno-parentheses \
    -Wno-deprecated-non-prototype \
    -Wno-comment -Wno-endif-labels \
    -I../telnetlib \
    -I$(TOPSRC)/sys -I$(TOPSRC)/sys/include

BIN=telnet
MAN=telnet.0
MANSRC=telnet.1
OBJS=telnet.o telcrypto.o

all: $(BIN) $(MAN)

$(BIN): $(OBJS)
	$(LD) $(LDFLAGS) -o $(BIN).elf $(OBJS) $(LIBS)
	$(SIZE) $(BIN).elf
	$(ELF2AOUT) $(BIN).elf $@ && rm $(BIN).elf

telcrypto.o: ../telnetlib/telcrypto.c ../telnetlib/telcrypto.h
	$(CC) $(CFLAGS) -c -o $@ ../telnetlib/telcrypto.c

$(MAN): $(MANSRC)
	nroff -man $(MANSRC) > $(MAN)

install: all
	cp $(MAN) $(DESTDIR)/share/man/cat1/
	install -m 755 $(BIN) $(DESTDIR)/usr/bin

clean:
	rm -f *.o *.elf $(BIN) $(MAN) *.dis
