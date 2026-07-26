TOPSRC          = $(shell cd ../..; pwd)
include $(TOPSRC)/target.mk

CFLAGS          += -Os
OBJS            = entry.o crt0-common.o

all:            crt0.o
		cp crt0.o ../

entry.o:        entry.S
		$(CC) $(CFLAGS) -x assembler-with-cpp -c $< -o $@

crt0-common.o:  ../startup/crt0.c
		$(CC) $(CFLAGS) -c $< -o $@

crt0.o:         $(OBJS)
		$(LD) -r -z noexecstack -o $@ $(OBJS)

install:        all

clean:
		rm -f *.o *~ core errs
