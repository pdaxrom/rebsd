#
# Copyright (c) 1988 Regents of the University of California.
# All rights reserved.
#
# Redistribution and use in source and binary forms are permitted
# provided that the above copyright notice and this paragraph are
# duplicated in all such forms and that any documentation, advertising
# materials, and other materials related to such redistribution and
# use acknowledge that the software was developed by the University
# of California, Berkeley.  The name of the University may not be
# used to endorse or promote products derived from this software
# without specific prior written permission.  THIS SOFTWARE IS PROVIDED
# ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
# WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND
# FITNESS FOR A PARTICULAR PURPOSE.
#
TOPSRC		= $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk

CFLAGS          += ${DEFS}

PCC_CC          = $(findstring pcc,$(notdir $(firstword $(CC))))

# PCC owns these compiler helper ABIs through libpcc.a.  Do not also archive
# same-name helpers into a PCC-built libc.a: libc.a is scanned before the
# trailing -lpcc and would otherwise win archive resolution for objects pulled
# from libc itself.
ifeq ($(PCC_CC),)
LIBPCC_HELPER_OBJS = divdi3.o \
                  fixdfdi.o \
                  floatdidf.o \
                  moddi3.o \
                  udivdi3.o \
                  udivmoddi4.o \
                  umoddi3.o
else
LIBPCC_HELPER_OBJS =
endif

OBJS            = adddf3.o \
                  addsf3.o \
                  comparedf2.o \
                  comparesf2.o \
                  divdf3.o \
                  divsf3.o \
                  extendsfdf2.o \
                  fixdfsi.o \
                  fixsfsi.o \
                  fixunssfsi.o \
                  fixunsdfsi.o \
                  floatsidf.o \
                  floatsisf.o \
                  floatundidf.o \
                  floatunsisf.o \
                  fp_mode.o \
                  int_helpers.o \
                  $(LIBPCC_HELPER_OBJS) \
                  muldf3.o \
                  mulsf3.o \
                  subdf3.o \
                  subsf3.o \
                  truncdfsf2.o \
		  floatunsidf.o

runtime.a:      ${OBJS}
		@echo "buiding runtime.a"
		@$(AR) cru runtime.a ${OBJS}
clean:
		rm -f *.a *.o *~ profiled/*.o tags

cleandir:       clean
		rm -f .depend
