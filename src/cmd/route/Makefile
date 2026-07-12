TOPSRC = $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk

CFLAGS += -Wall -Werror -fno-builtin \
    -Wno-implicit-function-declaration -Wno-implicit-int \
    -Wno-return-type -Wno-return-mismatch \
    -Wno-unused-variable -Wno-parentheses \
    -Wno-comment -Wno-endif-labels \
    -I$(TOPSRC)/sys -I$(TOPSRC)/sys/include

BIN=route
MAN=route.0
MANSRC=route.8
OBJS=route.o

all: $(BIN) $(MAN)

$(BIN): $(OBJS)
	$(LD) $(LDFLAGS) -o $(BIN).elf $(OBJS) $(LIBS)
	$(SIZE) $(BIN).elf
	$(ELF2AOUT) $(BIN).elf $@ && rm $(BIN).elf

$(MAN): $(MANSRC)
	nroff -man $< > $(MAN)

install: all
	cp $(MAN) $(DESTDIR)/share/man/cat8/
	install -m 755 $(BIN) $(DESTDIR)/sbin

clean:
	rm -f *.o *.elf $(BIN) $(MAN) *.dis
