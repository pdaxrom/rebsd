PROG            = ci20-ftdi-uart
CC              ?= cc
PKG_CONFIG      ?= pkg-config
LIBUSB_CFLAGS   = $(shell $(PKG_CONFIG) --cflags libusb-1.0 2>/dev/null)
LIBUSB_LIBS     = $(shell $(PKG_CONFIG) --libs libusb-1.0 2>/dev/null)
CFLAGS          ?= -O2 -g
CFLAGS          += -Wall -Wextra $(LIBUSB_CFLAGS)

all: $(PROG)

depend: all

$(PROG): ci20-ftdi-uart.o
	$(CC) $(LDFLAGS) -o $@ ci20-ftdi-uart.o $(LIBUSB_LIBS)

ci20-ftdi-uart.o: ci20-ftdi-uart.c
	$(CC) $(CFLAGS) -c -o $@ ci20-ftdi-uart.c

openocd:
	./openocd/build-openocd-xburst.sh

install: $(PROG)
	install -s $(PROG) ${DESTDIR}/bin/$(PROG)

clean:
	rm -f $(PROG) *.o *~ *.dSYM

.PHONY: all depend openocd install clean
