TOPSRC		= $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk

ALL		= gen sys

i386.a:		$(ALL)
		mkdir -p tmp
		for i in $(ALL); do (cd tmp; $(AR) x ../$$i/$$i.a); done
		$(AR) cr $@ `ls tmp/*.o | sort`
		rm -rf tmp

$(ALL):		FRC
		$(MAKE) -C $@ $(MFLAGS) DEFS="$(DEFS)"

FRC:

clean:
		for i in $(ALL); do $(MAKE) -C $$i $(MFLAGS) clean; done
		rm -rf *.a tmp *~
