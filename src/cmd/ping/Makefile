TOPSRC = $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk

CFLAGS += -Wall -Werror -fno-builtin \
    -Wno-implicit-function-declaration -Wno-implicit-int \
    -Wno-return-type -Wno-return-mismatch \
    -Wno-unused-variable -Wno-parentheses \
    -Wno-deprecated-non-prototype \
    -Wno-comment -Wno-endif-labels \
    -I$(TOPSRC)/sys -I$(TOPSRC)/sys/include

BIN=ping
MAN=ping.0
MANSRC=ping.8
OBJS=ping.o

all: $(BIN) $(MAN)

$(BIN): $(OBJS)
	$(LD) $(LDFLAGS) -o $(BIN).elf $(OBJS) $(LIBS)
	$(SIZE) $(BIN).elf
	$(ELF2AOUT) $(BIN).elf $@ && rm $(BIN).elf

$(MAN): $(MANSRC)
	nroff -man $< > $(MAN)

install: all
	cp $(MAN) $(DESTDIR)/share/man/cat8/
	install -m 755 $(BIN) $(DESTDIR)/usr/bin

clean:
	rm -f *.o *.elf $(BIN) $(MAN) *.dis
