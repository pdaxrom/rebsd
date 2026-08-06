#
# as-i386 - AT&T syntax assembler for ELF32/i386
#
TOPSRC          = $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk

CFLAGS         += -Werror -Wall -Os
HOST_CC        ?= cc
I686_PREFIX    ?= i686-elf-
BYTE_TESTS      = basic pcc pic resolve reloc16 isa simd-full x87 legacy vendor \
		  options cpu-options

OBJS            = as.o
MAN             = as.0
MANSRC          = as.1

as.o:           $(TOPSRC)/include/elf32.h

all:            as $(MAN)

as:             $(OBJS)
		$(LD) $(LDFLAGS) -o as.elf $(OBJS) $(LIBS)
		$(OBJDUMP) -S as.elf > as.dis
		$(SIZE) as.elf
		$(ELF2AOUT) as.elf $@ && rm as.elf

$(MAN):         $(MANSRC)
		$(MANROFF) $< > $@

clean:
		rm -f *.o *.0 *.elf as as-host tags *~ *.dis tests/*.o tests/*.dis tests/*.bin

install:        all
		install as $(DESTDIR)/bin/
		cp $(MAN) $(DESTDIR)/share/man/cat1/

# Host-side format and encoder regression tests.
test:
		$(HOST_CC) -DCROSS -Wall -Wextra -Werror -O2 \
		    -idirafter $(TOPSRC)/include -o as-host as.c
		@set -e; for t in $(BYTE_TESTS); do \
		    ./as-host --32 -o tests/$$t.o tests/$$t.s; \
		    $(I686_PREFIX)as --32 -o tests/$$t.gnu.o tests/$$t.s; \
		    $(I686_PREFIX)objcopy -O binary --only-section=.text \
		        tests/$$t.o tests/$$t.bin; \
		    $(I686_PREFIX)objcopy -O binary --only-section=.text \
		        tests/$$t.gnu.o tests/$$t.gnu.bin; \
		    cmp tests/$$t.bin tests/$$t.gnu.bin; \
		done
		./as-host --32 -march=cyrix_m2 -o tests/cyrix.o tests/cyrix.s
		./as-host --32 -march=i686,+padlockxmodx \
		    -o tests/vendor-netbsd.o tests/vendor-netbsd.s
		@if ./as-host --32 -march=i686 -o tests/options-negative.o \
		    tests/options.s >/dev/null 2>&1; then \
		    echo "ISA feature gate test unexpectedly succeeded"; exit 1; \
		fi
		./as-host --32 \
		    -march=i686,+sse4.2,+vmx,+cyrix,+padlock,+prfchw,+clflush,+xsaveopt \
		    -mtune=core2 -o tests/options-gated.o tests/options.s
		$(I686_PREFIX)readelf -h -S -s -r tests/basic.o
		$(I686_PREFIX)readelf -h -S -s -r tests/pcc.o
		$(I686_PREFIX)readelf -h -S -s -r tests/pic.o
		@$(I686_PREFIX)readelf -r tests/reloc16.o | grep -q R_386_16
		@$(I686_PREFIX)readelf -r tests/reloc16.o | grep -q R_386_8
		@set -e; for t in $(BYTE_TESTS) cyrix vendor-netbsd; do \
		    $(I686_PREFIX)objdump -dr tests/$$t.o > tests/$$t.dis; \
		done
		rm -f as-host
