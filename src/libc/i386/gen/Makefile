TOPSRC		= $(shell cd ../../../..; pwd)
include $(TOPSRC)/target.mk

ASFLAGS		+= $(DEFS)
OBJS		= byteorder.o setjmp.o

all:		gen.a

gen.a:		$(OBJS)
		$(AR) cru $@ $(OBJS)

clean:
		rm -f gen.a *.o *~
