TOPSRC = $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk

CFLAGS += -Wall -Werror -I$(TOPSRC)/sys -I$(TOPSRC)/sys/include

BIN = lspci
MAN = lspci.0
MANSRC = lspci.1
OBJS = lspci.o

all: $(BIN) $(MAN)

$(BIN): $(OBJS)
	$(LD) $(LDFLAGS) -o $(BIN).elf $(OBJS) $(LIBS)
	$(SIZE) $(BIN).elf
	$(ELF2AOUT) $(BIN).elf $@ && rm $(BIN).elf

$(MAN): $(MANSRC)
	$(MANROFF) $< > $(MAN)

install: all
	cp $(MAN) $(DESTDIR)/share/man/cat1/
	install -m 755 $(BIN) $(DESTDIR)/usr/bin

clean:
	rm -f *.o *.elf $(BIN) $(MAN) *.dis
