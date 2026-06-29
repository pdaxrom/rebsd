PROG            = n64usbnet-bridge
CC              ?= cc
PKG_CONFIG      ?= pkg-config
LIBUSB_CFLAGS   = $(shell $(PKG_CONFIG) --cflags libusb-1.0 2>/dev/null)
LIBUSB_LIBS     = $(shell $(PKG_CONFIG) --libs libusb-1.0 2>/dev/null)
CFLAGS          ?= -O2 -g
CFLAGS          += -Wall -Wextra $(LIBUSB_CFLAGS) -I../../sys/mips/common
OBJS            = n64usbnet-bridge.o n64usbnet_proto.o

all: $(PROG)

$(PROG): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBUSB_LIBS)

n64usbnet_proto.o: ../../sys/mips/common/n64usbnet_proto.c \
    ../../sys/mips/common/n64usbnet_proto.h
	$(CC) $(CFLAGS) -c -o $@ ../../sys/mips/common/n64usbnet_proto.c

install: $(PROG)
	install -s $(PROG) ${DESTDIR}/bin/$(PROG)

clean:
	rm -f $(PROG) *.o *~ *.dSYM

.PHONY: all install clean
