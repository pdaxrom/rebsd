#
# This is the Makefile for 'stdlib'.  New routines ported from 4.4BSD's
# libc/stdlib directory go here but existing libc/gen files are being left
# where they are.
#
TOPSRC		= $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk

DEFS		=
CFLAGS		+= ${DEFS} -Os

SRCS		= div.c getopt_long.c getsubopt.c inttypes.c reallocarray.c \
		  strtol.c strtoul.c strtoll.c strtoull.c strtod.c
OBJS		= div.o getopt_long.o getsubopt.o inttypes.o reallocarray.o \
		  strtol.o strtoul.o strtoll.o strtoull.o strtod.o

all:		stdlib.a

stdlib.a:	${OBJS}
		@echo "building stdlib"
		@${AR} cru stdlib.a ${OBJS}

clean:
		rm -f *.o *~ profiled/*.o tags Makefile.bak stdlib.a stdlib_p.s

depend:		${SRCS}
		mkdep ${CFLAGS} ${SRCS}

tags:
		cwd=`pwd`; \
		for i in ${SRCS}; do \
			ctags -a -f ${TAGSFILE} $$cwd/$$i; \
		done

# DO NOT DELETE THIS LINE -- mkdep uses it.
# DO NOT PUT ANYTHING AFTER THIS LINE, IT WILL GO AWAY.
