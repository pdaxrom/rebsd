TOPSRC = $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk

CFLAGS += -Wall -Werror

BIN = dmesg
MAN = dmesg.0
MANSRC = dmesg.8
OBJS = dmesg.o

all: $(BIN) $(MAN)

$(BIN): $(OBJS)
	$(LD) $(LDFLAGS) -o $(BIN).elf $(OBJS) $(LIBS)
	$(OBJDUMP) -S $(BIN).elf > $(BIN).dis
	$(SIZE) $(BIN).elf
	$(ELF2AOUT) $(BIN).elf $@ && rm $(BIN).elf

$(MAN): $(MANSRC)
	$(MANROFF) $< > $@

install: all
	cp $(MAN) $(DESTDIR)/share/man/cat8/
	install -m 755 $(BIN) $(DESTDIR)/sbin/$(BIN)

clean:
	rm -f *.o *.elf *.dis $(BIN) $(MAN)
