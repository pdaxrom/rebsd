TOPSRC		= $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk

CFLAGS		+= -Os

SRCS		= sbrk.c execl.c execle.c execlp.c execv.c mmap.c off64.c
OBJS		= sbrk.o execl.o execle.o execlp.o execv.o mmap.o off64.o

all:		sys.a

sys.a:		$(OBJS)
		$(AR) cru $@ $(OBJS)

clean:
		rm -f *.a *.o *~
