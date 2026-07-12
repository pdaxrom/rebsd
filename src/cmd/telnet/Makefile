#
# Small TELNET client for the MIPS network ports.
#
TOPSRC = $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk
CMD_PARENT_SRC  = $(if $(REBSD_SRCDIR),$(REBSD_SRCDIR)/..,..)

CFLAGS += -Wall -Werror -fno-builtin \
    -Wno-implicit-function-declaration -Wno-implicit-int \
    -Wno-return-type -Wno-return-mismatch \
    -Wno-unused-variable -Wno-parentheses \
    -Wno-deprecated-non-prototype \
    -Wno-comment -Wno-endif-labels \
    -I$(if $(REBSD_SRCDIR),$(REBSD_SRCDIR)/../telnetlib,../telnetlib) \
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

telcrypto.o: $(CMD_PARENT_SRC)/telnetlib/telcrypto.c $(CMD_PARENT_SRC)/telnetlib/telcrypto.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(MAN): $(MANSRC)
	nroff -man $< > $(MAN)

install: all
	cp $(MAN) $(DESTDIR)/share/man/cat1/
	install -m 755 $(BIN) $(DESTDIR)/usr/bin

clean:
	rm -f *.o *.elf $(BIN) $(MAN) *.dis
