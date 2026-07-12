TOPSRC		= $(shell cd ../../..; pwd)
include $(TOPSRC)/target.mk

CFLAGS		+= -Werror -Wall

SRCS		= retro.c
OBJS		= retro.o
BIN             = retroforth
LIBS            += -ltermcap
HOST_CC         ?= cc
RETROFORTH_SRCDIR = $(if $(REBSD_SRCDIR),$(REBSD_SRCDIR),.)

ifneq (,$(findstring -DTARGET_BIG_ENDIAN,$(CFLAGS)))
CFLAGS          += -DRXBE
RETRO_IMAGE     = retroImageBE
else
RETRO_IMAGE     = retroImage
endif

all:            $(BIN)

$(BIN):         ${SRCS}
		${CC} ${CFLAGS} -c -o $(OBJS) $<
		${LD} ${LDFLAGS} -o $(BIN).elf $(OBJS) ${LIBS}
		${SIZE} $(BIN).elf
		${OBJDUMP} -S $(BIN).elf > $(BIN).dis
		${ELF2AOUT} $(BIN).elf $@ && rm $(BIN).elf

retroImage:     $(RETROFORTH_SRCDIR)/retroImage
		cp -p $< $@

retroImageBE:   retroImage retroImage-convert
		./retroImage-convert

clean:
		rm -f *.o *.elf $(BIN) *.elf *.dis *~ retroImage-convert \
		    retroImage16 retroImage64 retroImage16BE retroImageBE \
		    retroImage64BE

retroImage-convert: $(RETROFORTH_SRCDIR)/tools/convert.c
		${HOST_CC} -O2 -Wall -Werror -o $@ $<

install:        all $(RETRO_IMAGE)
		install $(BIN) $(DESTDIR)/bin/
		cp $(RETRO_IMAGE) $(DESTDIR)/lib/

image:          $(BIN)
		cat $(RETROFORTH_SRCDIR)/image/meta.rx \
		    $(RETROFORTH_SRCDIR)/image/kernel.rx > core.rx
		./$(BIN) --shrink --image retroImage --with core.rx
		rm core.rx
