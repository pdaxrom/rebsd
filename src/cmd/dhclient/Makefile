#
# Small DHCPv4 client for the MIPS network port.
#
TOPSRC = $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk

CFLAGS += -Wall -Werror -fno-builtin \
    -Wno-implicit-function-declaration -Wno-implicit-int \
    -Wno-return-type -Wno-return-mismatch \
    -Wno-unused-variable -Wno-parentheses \
    -Wno-deprecated-non-prototype \
    -Wno-comment -Wno-endif-labels \
    -I$(TOPSRC)/sys -I$(TOPSRC)/sys/include

BIN=dhclient
OBJS=dhclient.o

all: $(BIN)

$(BIN): $(OBJS)
	$(LD) $(LDFLAGS) -o $(BIN).elf $(OBJS) $(LIBS)
	$(SIZE) $(BIN).elf
	$(ELF2AOUT) $(BIN).elf $@ && rm $(BIN).elf

install: all
	install -m 755 $(BIN) $(DESTDIR)/sbin

clean:
	rm -f *.o *.elf $(BIN) *.dis
