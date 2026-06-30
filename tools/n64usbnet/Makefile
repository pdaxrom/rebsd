PROG            = n64usbnet-bridge
PROGS           = n64usbnet-bridge n64usbnet-echo
CC              ?= cc
PKG_CONFIG      ?= pkg-config
LIBUSB_CFLAGS   = $(shell $(PKG_CONFIG) --cflags libusb-1.0 2>/dev/null)
LIBUSB_LIBS     = $(shell $(PKG_CONFIG) --libs libusb-1.0 2>/dev/null)
CFLAGS          ?= -O2 -g
CFLAGS          += -Wall -Wextra $(LIBUSB_CFLAGS) -I../../sys/mips/common
OBJS            = n64usbnet-bridge.o n64usbnet_proto.o

all: $(PROGS)

$(PROG): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBUSB_LIBS)

n64usbnet-echo: n64usbnet-echo.o
	$(CC) $(LDFLAGS) -o $@ n64usbnet-echo.o

n64usbnet_proto.o: ../../sys/mips/common/n64usbnet_proto.c \
    ../../sys/mips/common/n64usbnet_proto.h
	$(CC) $(CFLAGS) -c -o $@ ../../sys/mips/common/n64usbnet_proto.c

install: $(PROGS)
	for prog in $(PROGS); do install -s $$prog ${DESTDIR}/bin/$$prog; done

clean:
	rm -f $(PROGS) *.o *~ *.dSYM

.PHONY: all install clean
